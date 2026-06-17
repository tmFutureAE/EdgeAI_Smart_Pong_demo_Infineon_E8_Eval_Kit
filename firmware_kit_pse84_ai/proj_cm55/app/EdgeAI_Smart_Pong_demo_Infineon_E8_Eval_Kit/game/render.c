#include "game/render.h"

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#include "edgeai_config.h"

#include "platform/display_hal.h"
#include "platform/time_hal.h"
#include "text5x7.h"

#include "game/ui_layout.h"

static uint16_t s_tile[EDGEAI_TILE_MAX_W * EDGEAI_TILE_MAX_H];

#define EDGEAI_END_PROMPT_DELAY_FRAMES 120u
#define EDGEAI_CONFETTI_COUNT 56
#define EDGEAI_CONFETTI_TIME_SCALE 2.30f
#define EDGEAI_CONFETTI_MAX_T_S 4.0f
#define EDGEAI_COUNTDOWN_STEP_US 1000000u
#define EDGEAI_COUNTDOWN_3_US (3u * EDGEAI_COUNTDOWN_STEP_US)
#define EDGEAI_COUNTDOWN_2_US (2u * EDGEAI_COUNTDOWN_STEP_US)
#define EDGEAI_COUNTDOWN_1_US EDGEAI_COUNTDOWN_STEP_US

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float clamp01f(float v)
{
    return clampf(v, 0.0f, 1.0f);
}

static inline uint8_t clamp_u8(int32_t v)
{
    if (v < 0) return 0u;
    if (v > 255) return 255u;
    return (uint8_t)v;
}

static void text_u3(char out[3], uint32_t v)
{
    if (!out) return;
    if (v > 999u) v = 999u;
    out[0] = (char)('0' + ((v / 100u) % 10u));
    out[1] = (char)('0' + ((v / 10u) % 10u));
    out[2] = (char)('0' + (v % 10u));
}

static bool rect_intersects(int32_t ax0, int32_t ay0, int32_t ax1, int32_t ay1,
                            int32_t bx0, int32_t by0, int32_t bx1, int32_t by1)
{
    if (ax1 < bx0 || bx1 < ax0) return false;
    if (ay1 < by0 || by1 < ay0) return false;
    return true;
}

static inline uint32_t render_hash_u32(uint32_t x)
{
    x ^= x >> 16;
    x *= 0x7FEB352Du;
    x ^= x >> 15;
    x *= 0x846CA68Bu;
    x ^= x >> 16;
    return x;
}

static bool render_end_prompt_visible(const pong_game_t *g)
{
    if (!g) return false;
    if (!g->match_over) return false;
    if (g->end_prompt_dismissed) return false;
    return ((g->frame - g->match_over_frame) >= EDGEAI_END_PROMPT_DELAY_FRAMES);
}

static void render_countdown(uint16_t *dst, uint32_t w, uint32_t h, int32_t tile_x0, int32_t tile_y0,
                             const pong_game_t *g)
{
    if (!g || !g->countdown_active) return;

    uint32_t r = g->countdown_us_left;
    int digit = 1;
    uint16_t c = sw_pack_rgb565_u8(30, 220, 40); /* green */

    if (r > EDGEAI_COUNTDOWN_2_US)
    {
        digit = 3;
        c = sw_pack_rgb565_u8(240, 30, 30); /* red */
    }
    else if (r > EDGEAI_COUNTDOWN_1_US)
    {
        digit = 2;
        c = sw_pack_rgb565_u8(245, 220, 40); /* yellow */
    }

    char s[2] = {(char)('0' + digit), 0};
    const int32_t scale = 12;
    int32_t tw = edgeai_text5x7_width(scale, s);
    int32_t th = 7 * scale;
    int32_t x = (EDGEAI_LCD_W - tw) / 2;
    int32_t y = (EDGEAI_LCD_H - th) / 2;
    uint16_t shadow = sw_pack_rgb565_u8(8, 8, 10);

    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x + 4, y + 4, scale, s, shadow);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, scale, s, c);
}

static void render_project(const render_state_t *rs, float x, float y, float z, int32_t *sx, int32_t *sy, float *out_scale)
{
    float scale = 1.0f / (1.0f + z * rs->persp);
    if (out_scale) *out_scale = scale;

    float dx = (x - 0.5f) * (float)rs->world_scale_x;
    float dy = (y - 0.5f) * (float)rs->world_scale_y;

    int32_t px = rs->cx + (int32_t)(dx * scale);
    int32_t py = rs->cy + (int32_t)(dy * scale);
    if (sx) *sx = px;
    if (sy) *sy = py;
}

static void render_compute_box(render_state_t *rs)
{
    /* Near (z=0) corners: (0,0), (1,0), (1,1), (0,1) */
    render_project(rs, 0.0f, 0.0f, 0.0f, &rs->near_corners[0].x, &rs->near_corners[0].y, NULL);
    render_project(rs, 1.0f, 0.0f, 0.0f, &rs->near_corners[1].x, &rs->near_corners[1].y, NULL);
    render_project(rs, 1.0f, 1.0f, 0.0f, &rs->near_corners[2].x, &rs->near_corners[2].y, NULL);
    render_project(rs, 0.0f, 1.0f, 0.0f, &rs->near_corners[3].x, &rs->near_corners[3].y, NULL);

    /* Far (z=1) corners. */
    render_project(rs, 0.0f, 0.0f, 1.0f, &rs->far_corners[0].x, &rs->far_corners[0].y, NULL);
    render_project(rs, 1.0f, 0.0f, 1.0f, &rs->far_corners[1].x, &rs->far_corners[1].y, NULL);
    render_project(rs, 1.0f, 1.0f, 1.0f, &rs->far_corners[2].x, &rs->far_corners[2].y, NULL);
    render_project(rs, 0.0f, 1.0f, 1.0f, &rs->far_corners[3].x, &rs->far_corners[3].y, NULL);
}

void render_init(render_state_t *rs)
{
    if (!rs) return;

    rs->cx = EDGEAI_LCD_W / 2;
    rs->cy = EDGEAI_LCD_H / 2;
    /* Scale the 3D court to the active panel so it fills the 4.3-inch display. */
    rs->world_scale_x = (EDGEAI_LCD_W * 9) / 10;
    rs->world_scale_y = (EDGEAI_LCD_H * 17) / 20;
    rs->persp = 1.20f;

    render_compute_box(rs);
}

static void render_draw_center_dashes(uint16_t *dst, uint32_t w, uint32_t h, int32_t x0, int32_t y0, const render_state_t *rs)
{
    (void)rs;
    const uint16_t c_dash = sw_pack_rgb565_u8(200, 201, 204);

    const float x = 0.5f;
    const float y = 1.0f;
    const float dash_len = 0.05f;
    const float gap_len = 0.05f;

    float z = 0.05f;
    for (int i = 0; i < 14; i++)
    {
        float z0 = z;
        float z1 = z + dash_len;
        if (z1 > 0.98f) z1 = 0.98f;

        int32_t ax = 0, ay = 0, bx = 0, by = 0;
        render_project(rs, x, y, z0, &ax, &ay, NULL);
        render_project(rs, x, y, z1, &bx, &by, NULL);

        sw_render_line(dst, w, h, x0, y0, ax, ay, bx, by, c_dash);
        sw_render_line(dst, w, h, x0, y0, ax + 1, ay, bx + 1, by, c_dash);

        z = z1 + gap_len;
        if (z > 0.98f) break;
    }
}

static void render_draw_floor_grid(uint16_t *dst, uint32_t w, uint32_t h, int32_t x0, int32_t y0, const render_state_t *rs)
{
    const uint16_t c_grid = sw_pack_rgb565_u8(24, 25, 28);

    const float y = 1.0f;

    /* Depth cross-lines. */
    static const float z_lines[] = {0.12f, 0.24f, 0.36f, 0.48f, 0.60f, 0.72f, 0.84f, 0.96f};
    for (size_t i = 0; i < (sizeof(z_lines) / sizeof(z_lines[0])); i++)
    {
        float z = z_lines[i];
        int32_t ax = 0, ay = 0, bx = 0, by = 0;
        render_project(rs, 0.0f, y, z, &ax, &ay, NULL);
        render_project(rs, 1.0f, y, z, &bx, &by, NULL);
        sw_render_line(dst, w, h, x0, y0, ax, ay, bx, by, c_grid);
    }

    /* Perspective rails. */
    static const float x_lines[] = {0.25f, 0.75f};
    for (size_t i = 0; i < (sizeof(x_lines) / sizeof(x_lines[0])); i++)
    {
        float x = x_lines[i];
        int32_t ax = 0, ay = 0, bx = 0, by = 0;
        render_project(rs, x, y, 0.05f, &ax, &ay, NULL);
        render_project(rs, x, y, 0.98f, &bx, &by, NULL);
        sw_render_line(dst, w, h, x0, y0, ax, ay, bx, by, c_grid);
    }
}

static void render_bg_tile(uint16_t *dst, uint32_t w, uint32_t h, int32_t x0, int32_t y0, const render_state_t *rs)
{
    const uint16_t c_bg = sw_pack_rgb565_u8(10, 13, 18);
    const uint16_t c_wall_dark = sw_pack_rgb565_u8(18, 23, 31);
    const uint16_t c_wall_mid = sw_pack_rgb565_u8(33, 44, 58);
    const uint16_t c_wall_side = sw_pack_rgb565_u8(24, 31, 41);
    const uint16_t c_back_glow = sw_pack_rgb565_u8(46, 66, 88);

    sw_render_clear(dst, w, h, c_bg);

    /* Back wall. */
    sw_render_fill_quad(dst, w, h, x0, y0,
                        rs->far_corners[0], rs->far_corners[1], rs->far_corners[2], rs->far_corners[3],
                        c_wall_dark);

    /* Ceiling. */
    sw_render_fill_quad(dst, w, h, x0, y0,
                        rs->near_corners[0], rs->near_corners[1], rs->far_corners[1], rs->far_corners[0],
                        c_wall_dark);

    /* Side walls. */
    sw_render_fill_quad(dst, w, h, x0, y0,
                        rs->near_corners[0], rs->near_corners[3], rs->far_corners[3], rs->far_corners[0],
                        c_wall_side);
    sw_render_fill_quad(dst, w, h, x0, y0,
                        rs->near_corners[1], rs->near_corners[2], rs->far_corners[2], rs->far_corners[1],
                        c_wall_side);

    /* Floor. */
    sw_render_fill_quad(dst, w, h, x0, y0,
                        rs->near_corners[3], rs->near_corners[2], rs->far_corners[2], rs->far_corners[3],
                        c_wall_mid);

    /* Back-wall soft glow panel to deepen perspective contrast. */
    {
        sw_point_t bl = rs->far_corners[0];
        sw_point_t br = rs->far_corners[1];
        sw_point_t tr = rs->far_corners[2];
        sw_point_t tl = rs->far_corners[3];
        bl.x += 26; bl.y += 20;
        br.x -= 26; br.y += 20;
        tr.x -= 26; tr.y -= 20;
        tl.x += 26; tl.y -= 20;
        sw_render_fill_quad(dst, w, h, x0, y0, bl, br, tr, tl, c_back_glow);
    }

    render_draw_floor_grid(dst, w, h, x0, y0, rs);
    render_draw_center_dashes(dst, w, h, x0, y0, rs);

    /* Box edges: increase wall readability while keeping the monochrome style. */
    const uint16_t c_box_edge = sw_pack_rgb565_u8(88, 128, 170);
    const uint16_t c_box_far = sw_pack_rgb565_u8(56, 79, 104);

    /* Far frame outline. */
    sw_render_line(dst, w, h, x0, y0, rs->far_corners[0].x, rs->far_corners[0].y, rs->far_corners[1].x, rs->far_corners[1].y, c_box_far);
    sw_render_line(dst, w, h, x0, y0, rs->far_corners[1].x, rs->far_corners[1].y, rs->far_corners[2].x, rs->far_corners[2].y, c_box_far);
    sw_render_line(dst, w, h, x0, y0, rs->far_corners[2].x, rs->far_corners[2].y, rs->far_corners[3].x, rs->far_corners[3].y, c_box_far);
    sw_render_line(dst, w, h, x0, y0, rs->far_corners[3].x, rs->far_corners[3].y, rs->far_corners[0].x, rs->far_corners[0].y, c_box_far);

    /* Connecting edges from near to far. */
    sw_render_line(dst, w, h, x0, y0, rs->near_corners[0].x, rs->near_corners[0].y, rs->far_corners[0].x, rs->far_corners[0].y, c_box_edge);
    sw_render_line(dst, w, h, x0, y0, rs->near_corners[1].x, rs->near_corners[1].y, rs->far_corners[1].x, rs->far_corners[1].y, c_box_edge);
    sw_render_line(dst, w, h, x0, y0, rs->near_corners[2].x, rs->near_corners[2].y, rs->far_corners[2].x, rs->far_corners[2].y, c_box_edge);
    sw_render_line(dst, w, h, x0, y0, rs->near_corners[3].x, rs->near_corners[3].y, rs->far_corners[3].x, rs->far_corners[3].y, c_box_edge);

    /* Near frame outline. */
    const uint16_t c_frame = sw_pack_rgb565_u8(18, 26, 36);
    sw_render_line(dst, w, h, x0, y0, rs->near_corners[0].x, rs->near_corners[0].y, rs->near_corners[1].x, rs->near_corners[1].y, c_frame);
    sw_render_line(dst, w, h, x0, y0, rs->near_corners[1].x, rs->near_corners[1].y, rs->near_corners[2].x, rs->near_corners[2].y, c_frame);
    sw_render_line(dst, w, h, x0, y0, rs->near_corners[2].x, rs->near_corners[2].y, rs->near_corners[3].x, rs->near_corners[3].y, c_frame);
    sw_render_line(dst, w, h, x0, y0, rs->near_corners[3].x, rs->near_corners[3].y, rs->near_corners[0].x, rs->near_corners[0].y, c_frame);
}

static void render_draw_paddle(uint16_t *dst, uint32_t w, uint32_t h, int32_t x0, int32_t y0,
                               const render_state_t *rs, const pong_paddle_t *p)
{
    const uint16_t c_shadow = sw_pack_rgb565_u8(8, 8, 10);
    const uint16_t c_pad = sw_pack_rgb565_u8(214, 215, 217);
    const uint16_t c_edge_near = sw_pack_rgb565_u8(246, 247, 249);
    const uint16_t c_edge_far = sw_pack_rgb565_u8(156, 157, 160);
    const uint16_t c_edge_side = sw_pack_rgb565_u8(206, 207, 209);

    float hy = p->size_y * 0.5f;
    float hz = p->size_z * 0.5f;

    float y0n = p->y - hy;
    float y1n = p->y + hy;
    float z0n = p->z - hz;
    float z1n = p->z + hz;

    sw_point_t q[4];
    render_project(rs, p->x_plane, y0n, z0n, &q[0].x, &q[0].y, NULL);
    render_project(rs, p->x_plane, y1n, z0n, &q[1].x, &q[1].y, NULL);
    render_project(rs, p->x_plane, y1n, z1n, &q[2].x, &q[2].y, NULL);
    render_project(rs, p->x_plane, y0n, z1n, &q[3].x, &q[3].y, NULL);

    /* Drop shadow. */
    sw_point_t qs[4] = {q[0], q[1], q[2], q[3]};
    for (int i = 0; i < 4; i++)
    {
        qs[i].x += 2;
        qs[i].y += 2;
    }
    sw_render_fill_quad(dst, w, h, x0, y0, qs[0], qs[1], qs[2], qs[3], c_shadow);
    sw_render_fill_quad(dst, w, h, x0, y0, q[0], q[1], q[2], q[3], c_pad);

    /* Depth edges: make z extent readable at a glance. */
    sw_render_line(dst, w, h, x0, y0, q[0].x, q[0].y, q[1].x, q[1].y, c_edge_near);
    sw_render_line(dst, w, h, x0, y0, q[0].x + 1, q[0].y, q[1].x + 1, q[1].y, c_edge_near);
    sw_render_line(dst, w, h, x0, y0, q[3].x, q[3].y, q[2].x, q[2].y, c_edge_far);
    sw_render_line(dst, w, h, x0, y0, q[0].x, q[0].y, q[3].x, q[3].y, c_edge_side);
    sw_render_line(dst, w, h, x0, y0, q[1].x, q[1].y, q[2].x, q[2].y, c_edge_side);
}

static float render_absf(float v)
{
    return (v < 0.0f) ? -v : v;
}

static void render_target_wall(float *p, float *v, float r)
{
    if (!p || !v) return;
    if ((*p - r) < 0.0f)
    {
        *p = r;
        *v = render_absf(*v);
    }
    if ((*p + r) > 1.0f)
    {
        *p = 1.0f - r;
        *v = -render_absf(*v);
    }
}

static bool render_predict_paddle_target(const pong_game_t *g, bool right_side, float *out_y, float *out_z, float *out_t)
{
    if (!g || !out_y || !out_z || !out_t) return false;

    *out_y = 0.5f;
    *out_z = 0.5f;
    *out_t = 0.0f;

    if (right_side)
    {
        if (g->ball.vx <= 0.0f) return false;
    }
    else
    {
        if (g->ball.vx >= 0.0f) return false;
    }

    float x = g->ball.x;
    float y = g->ball.y;
    float z = g->ball.z;
    float vx = g->ball.vx;
    float vy = g->ball.vy;
    float vz = g->ball.vz;
    float r = g->ball.r;

    float x_hit = right_side ? (g->paddle_r.x_plane - r) : (g->paddle_l.x_plane + r);
    float dt = 1.0f / (float)EDGEAI_FIXED_FPS;
    int max_steps = EDGEAI_FIXED_FPS * 4;
    if (max_steps < 1) max_steps = 1;

    for (int i = 0; i < max_steps; i++)
    {
        x += vx * dt;
        y += vy * dt;
        z += vz * dt;
        render_target_wall(&y, &vy, r);
        render_target_wall(&z, &vz, r);

        if ((right_side && (x >= x_hit)) || (!right_side && (x <= x_hit)))
        {
            *out_y = clampf(y, 0.0f, 1.0f);
            *out_z = clampf(z, 0.0f, 1.0f);
            *out_t = (float)(i + 1) * dt;
            return true;
        }
    }

    return false;
}

static bool render_target_inside_paddle(const pong_paddle_t *p, float y, float z)
{
    if (!p) return false;
    float hy = p->size_y * 0.5f;
    float hz = p->size_z * 0.5f;
    return (render_absf(y - p->y) <= hy) && (render_absf(z - p->z) <= hz);
}

static void render_draw_target_mark(uint16_t *dst, uint32_t w, uint32_t h, int32_t x0, int32_t y0,
                                    const render_state_t *rs, const pong_paddle_t *p,
                                    float mark_y, float mark_z)
{
    if (!dst || !rs || !p) return;

    float hy = p->size_y * 0.5f;
    float hz = p->size_z * 0.5f;
    float inset = 0.004f;
    float y = clampf(mark_y, p->y - hy + inset, p->y + hy - inset);
    float z = clampf(mark_z, p->z - hz + inset, p->z + hz - inset);

    int32_t sx = 0, sy = 0;
    float sc = 1.0f;
    render_project(rs, p->x_plane, y, z, &sx, &sy, &sc);

    int32_t arm = (int32_t)(3.0f + 4.0f * sc);
    if (arm < 3) arm = 3;
    if (arm > 8) arm = 8;

    const int32_t thick = 2;
    const uint16_t c = sw_pack_rgb565_u8(6, 6, 8);
    for (int32_t t = 0; t < thick; t++)
    {
        sw_render_line(dst, w, h, x0, y0, sx - arm, sy + t, sx + arm, sy + t, c);
        sw_render_line(dst, w, h, x0, y0, sx + t, sy - arm, sx + t, sy + arm, c);
    }
}

static void render_draw_target_guides(uint16_t *dst, uint32_t w, uint32_t h, int32_t x0, int32_t y0,
                                      const render_state_t *rs, const pong_game_t *g)
{
    if (!dst || !rs || !g) return;
    if (!g->target_overlay_enabled) return;
    if (g->match_over) return;

    float ly = g->paddle_l.y;
    float lz = g->paddle_l.z;
    float lt = 0.0f;
    bool l_valid = render_predict_paddle_target(g, false, &ly, &lz, &lt);

    float ry = g->paddle_r.y;
    float rz = g->paddle_r.z;
    float rt = 0.0f;
    bool r_valid = render_predict_paddle_target(g, true, &ry, &rz, &rt);

    if (l_valid && (lt >= 0.0f) && render_target_inside_paddle(&g->paddle_l, ly, lz))
    {
        render_draw_target_mark(dst, w, h, x0, y0, rs, &g->paddle_l, ly, lz);
    }
    if (r_valid && (rt >= 0.0f) && render_target_inside_paddle(&g->paddle_r, ry, rz))
    {
        render_draw_target_mark(dst, w, h, x0, y0, rs, &g->paddle_r, ry, rz);
    }
}

static void render_draw_ball(uint16_t *dst, uint32_t w, uint32_t h, int32_t x0, int32_t y0,
                             const render_state_t *rs, const pong_ball_t *b)
{
    const uint16_t c_shadow = sw_pack_rgb565_u8(8, 10, 14);

    /* Ball color ramp (speed): cyan -> lime -> amber. */
    float v2 = (b->vx * b->vx) + (b->vy * b->vy) + (b->vz * b->vz);
    const float vmin = 0.90f;
    const float vmax = 4.00f;
    float t = clamp01f((v2 - (vmin * vmin)) / ((vmax * vmax) - (vmin * vmin)));
    float hue = 190.0f - (t * 140.0f);

    /* HSV-like blend (S~0.92,V~0.96) for h in [50,190]. */
    float v = 0.96f;
    float rf = 0.0f, gf = 0.0f, bf = 0.0f;
    if (hue < 60.0f)
    {
        rf = v;
        gf = v * (hue * (1.0f / 60.0f));
        bf = v * 0.08f;
    }
    else if (hue < 120.0f)
    {
        gf = v;
        rf = v * ((120.0f - hue) * (1.0f / 60.0f));
        bf = v * ((hue - 60.0f) * (1.0f / 60.0f)) * 0.30f;
    }
    else
    {
        gf = v * ((190.0f - hue) * (1.0f / 70.0f));
        bf = v;
        rf = v * 0.18f;
    }

    uint8_t r = clamp_u8((int32_t)(rf * 255.0f + 0.5f));
    uint8_t g = clamp_u8((int32_t)(gf * 255.0f + 0.5f));
    uint8_t b8 = clamp_u8((int32_t)(bf * 255.0f + 0.5f));

    const uint16_t c_ball = sw_pack_rgb565_u8(r, g, b8);
    const uint16_t c_hi = sw_pack_rgb565_u8(
        clamp_u8((int32_t)r + 40),
        clamp_u8((int32_t)g + 40),
        clamp_u8((int32_t)b8 + 40));
    const uint16_t c_rim = sw_pack_rgb565_u8(
        clamp_u8((int32_t)r + 22),
        clamp_u8((int32_t)g + 22),
        clamp_u8((int32_t)b8 + 22));

    int32_t sx = 0, sy = 0;
    float s = 1.0f;
    render_project(rs, b->x, b->y, b->z, &sx, &sy, &s);

    /* Use x scale so the visible radius matches paddle-plane collision distance in screen space. */
    int32_t r_px = (int32_t)(b->r * (float)rs->world_scale_x * s);
    if (r_px < 2) r_px = 2;
    if (r_px > 40) r_px = 40;

    int32_t fx = 0, fy = 0;
    render_project(rs, b->x, 1.0f, b->z, &fx, &fy, NULL);
    /* Shadow size grows near floor (y -> 1) and shrinks with height (y -> 0). */
    float floor_prox = clamp01f(b->y);
    float sh_scale = 0.22f + (0.98f * floor_prox);
    int32_t sh_r = (int32_t)((float)r_px * sh_scale);
    if (sh_r < 1) sh_r = 1;
    sw_render_filled_circle(dst, w, h, x0, y0, fx, fy, sh_r, c_shadow);

    sw_render_filled_circle(dst, w, h, x0, y0, sx, sy, r_px, c_ball);
    if (r_px >= 5)
    {
        sw_render_filled_circle(dst, w, h, x0, y0, sx, sy, r_px - 2, c_rim);
    }
    sw_render_filled_circle(dst, w, h, x0, y0, sx - (r_px / 3), sy - (r_px / 3), r_px / 3, c_hi);
}

static void render_digit7seg(uint16_t *dst, uint32_t w, uint32_t h, int32_t tile_x0, int32_t tile_y0,
                             int32_t x, int32_t y, int32_t seg_len, int32_t t, int digit, uint16_t c)
{
    static const uint8_t masks[10] = {
        0x3Fu, /* 0 */
        0x06u, /* 1 */
        0x5Bu, /* 2 */
        0x4Fu, /* 3 */
        0x66u, /* 4 */
        0x6Du, /* 5 */
        0x7Du, /* 6 */
        0x07u, /* 7 */
        0x7Fu, /* 8 */
        0x6Fu, /* 9 */
    };
    if (digit < 0 || digit > 9) return;
    uint8_t m = masks[digit];

    int32_t w_d = seg_len + 2 * t;
    int32_t h_d = 2 * seg_len + 3 * t;

    int32_t x0 = x - (w_d / 2);
    int32_t y0 = y - (h_d / 2);

    /* a */
    if (m & (1u << 0)) sw_render_fill_rect(dst, w, h, tile_x0, tile_y0, x0 + t, y0, x0 + t + seg_len - 1, y0 + t - 1, c);
    /* b */
    if (m & (1u << 1)) sw_render_fill_rect(dst, w, h, tile_x0, tile_y0, x0 + t + seg_len, y0 + t, x0 + t + seg_len + t - 1, y0 + t + seg_len - 1, c);
    /* c */
    if (m & (1u << 2)) sw_render_fill_rect(dst, w, h, tile_x0, tile_y0, x0 + t + seg_len, y0 + 2 * t + seg_len, x0 + t + seg_len + t - 1, y0 + 2 * t + 2 * seg_len - 1, c);
    /* d */
    if (m & (1u << 3)) sw_render_fill_rect(dst, w, h, tile_x0, tile_y0, x0 + t, y0 + 2 * seg_len + 2 * t, x0 + t + seg_len - 1, y0 + 2 * seg_len + 3 * t - 1, c);
    /* e */
    if (m & (1u << 4)) sw_render_fill_rect(dst, w, h, tile_x0, tile_y0, x0, y0 + 2 * t + seg_len, x0 + t - 1, y0 + 2 * t + 2 * seg_len - 1, c);
    /* f */
    if (m & (1u << 5)) sw_render_fill_rect(dst, w, h, tile_x0, tile_y0, x0, y0 + t, x0 + t - 1, y0 + t + seg_len - 1, c);
    /* g */
    if (m & (1u << 6)) sw_render_fill_rect(dst, w, h, tile_x0, tile_y0, x0 + t, y0 + seg_len + t, x0 + t + seg_len - 1, y0 + seg_len + 2 * t - 1, c);
}

static void render_score_value(uint16_t *dst, uint32_t w, uint32_t h, int32_t tile_x0, int32_t tile_y0,
                               int32_t cx, int32_t cy, int32_t seg_len, int32_t t, int score,
                               uint16_t c_shadow, uint16_t c_digit)
{
    int val = score;
    if (val < 0) val = 0;
    if (val > 999) val = 999;

    int digit_w = seg_len + (2 * t);
    int gap = t + 2;

    if (val >= 100)
    {
        int hundreds = (val / 100) % 10;
        int tens = (val / 10) % 10;
        int ones = val % 10;

        int total_w = (3 * digit_w) + (2 * gap);
        int cx_hundreds = cx - (total_w / 2) + (digit_w / 2);
        int cx_tens = cx_hundreds + digit_w + gap;
        int cx_ones = cx_tens + digit_w + gap;

        render_digit7seg(dst, w, h, tile_x0, tile_y0, cx_hundreds + 2, cy + 2, seg_len, t, hundreds, c_shadow);
        render_digit7seg(dst, w, h, tile_x0, tile_y0, cx_tens + 2, cy + 2, seg_len, t, tens, c_shadow);
        render_digit7seg(dst, w, h, tile_x0, tile_y0, cx_ones + 2, cy + 2, seg_len, t, ones, c_shadow);
        render_digit7seg(dst, w, h, tile_x0, tile_y0, cx_hundreds, cy, seg_len, t, hundreds, c_digit);
        render_digit7seg(dst, w, h, tile_x0, tile_y0, cx_tens, cy, seg_len, t, tens, c_digit);
        render_digit7seg(dst, w, h, tile_x0, tile_y0, cx_ones, cy, seg_len, t, ones, c_digit);
        return;
    }
    if (val >= 10)
    {
        int tens = (val / 10) % 10;
        int ones = val % 10;

        int total_w = (2 * digit_w) + gap;
        int cx_tens = cx - (total_w / 2) + (digit_w / 2);
        int cx_ones = cx_tens + digit_w + gap;

        render_digit7seg(dst, w, h, tile_x0, tile_y0, cx_tens + 2, cy + 2, seg_len, t, tens, c_shadow);
        render_digit7seg(dst, w, h, tile_x0, tile_y0, cx_ones + 2, cy + 2, seg_len, t, ones, c_shadow);
        render_digit7seg(dst, w, h, tile_x0, tile_y0, cx_tens, cy, seg_len, t, tens, c_digit);
        render_digit7seg(dst, w, h, tile_x0, tile_y0, cx_ones, cy, seg_len, t, ones, c_digit);
        return;
    }

    render_digit7seg(dst, w, h, tile_x0, tile_y0, cx + 2, cy + 2, seg_len, t, val, c_shadow);
    render_digit7seg(dst, w, h, tile_x0, tile_y0, cx, cy, seg_len, t, val, c_digit);
}

typedef enum
{
    kSideRoleHuman = 0,
    kSideRoleAlgo = 1,
    kSideRoleEdgeAi = 2,
} side_role_t;

static side_role_t render_side_role(const pong_game_t *g, bool right_side)
{
    if (!g) return kSideRoleHuman;

    bool ai_side = right_side ? g->ai_right_active : g->ai_left_active;
    if (!ai_side) return kSideRoleHuman;

    if (!g->ai_enabled) return kSideRoleAlgo;
    if ((g->ai_learn_mode == kAiLearnModeAiAlgo) && right_side) return kSideRoleAlgo;
    if ((g->ai_learn_mode == kAiLearnModeAlgoAi) && !right_side) return kSideRoleAlgo;
    return kSideRoleEdgeAi;
}

static void render_side_role_text(uint16_t *dst, uint32_t w, uint32_t h, int32_t tile_x0, int32_t tile_y0,
                                  int32_t cx, int32_t cy, side_role_t role)
{
    const char *txt = "HUMAN";
    uint16_t c_role = sw_pack_rgb565_u8(255, 198, 90);
    const uint16_t c_shadow = sw_pack_rgb565_u8(10, 10, 12);
    const int32_t scale = 3;

    if (role == kSideRoleAlgo)
    {
        txt = "ALGO";
        c_role = sw_pack_rgb565_u8(102, 198, 255);
    }
    else if (role == kSideRoleEdgeAi)
    {
        txt = "EdgeAI";
        c_role = sw_pack_rgb565_u8(94, 255, 150);
    }

    int32_t tw = edgeai_text5x7_width(scale, txt);
    int32_t tx = cx - (tw / 2);
    int32_t ty = cy - (7 * scale) / 2;
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx + 1, ty + 1, scale, txt, c_shadow);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx, ty, scale, txt, c_role);
}

static void render_scores(uint16_t *dst, uint32_t w, uint32_t h, int32_t x0, int32_t y0,
                          const render_state_t *rs, const pong_game_t *g)
{
    const uint16_t c_shadow = sw_pack_rgb565_u8(6, 6, 8);
    const uint16_t c_digit = sw_pack_rgb565_u8(214, 215, 217);
    int32_t lx = 0, ly = 0, rx = 0, ry = 0;

    if (g->match_over)
    {
        float s_near = 1.0f / (1.0f + 0.38f * rs->persp);
        int32_t seg_big = (int32_t)(60.0f * s_near);
        int32_t th_big = (int32_t)(10.0f * s_near);
        int32_t seg_small = (int32_t)(38.0f * s_near);
        int32_t th_small = (int32_t)(7.0f * s_near);
        if (seg_big < 22) seg_big = 22;
        if (th_big < 4) th_big = 4;
        if (seg_small < 14) seg_small = 14;
        if (th_small < 3) th_small = 3;

        render_project(rs, 0.22f, 0.16f, 0.38f, &lx, &ly, NULL);
        render_project(rs, 0.78f, 0.16f, 0.38f, &rx, &ry, NULL);

        bool flash_on = (((g->frame / 8u) & 1u) == 0u);
        const uint16_t c_win = flash_on ? sw_pack_rgb565_u8(80, 255, 90) : sw_pack_rgb565_u8(28, 120, 36);
        const uint16_t c_lose = sw_pack_rgb565_u8(230, 35, 35);

        if (g->winner_left)
        {
            render_score_value(dst, w, h, x0, y0, lx, ly, seg_big, th_big, (int)g->score.left, c_shadow, c_win);
            render_score_value(dst, w, h, x0, y0, rx, ry + 4, seg_small, th_small, (int)g->score.right, c_shadow, c_lose);
        }
        else
        {
            render_score_value(dst, w, h, x0, y0, lx, ly + 4, seg_small, th_small, (int)g->score.left, c_shadow, c_lose);
            render_score_value(dst, w, h, x0, y0, rx, ry, seg_big, th_big, (int)g->score.right, c_shadow, c_win);
        }

        return;
    }

    float s_far = 1.0f / (1.0f + 1.0f * rs->persp);
    int32_t seg_len = (int32_t)(50.0f * s_far);
    int32_t t = (int32_t)(8.0f * s_far);
    if (seg_len < 12) seg_len = 12;
    if (t < 3) t = 3;

    render_project(rs, 0.20f, 0.16f, 1.0f, &lx, &ly, NULL);
    render_project(rs, 0.80f, 0.16f, 1.0f, &rx, &ry, NULL);

    render_score_value(dst, w, h, x0, y0, lx, ly, seg_len, t, (int)g->score.left, c_shadow, c_digit);
    render_score_value(dst, w, h, x0, y0, rx, ry, seg_len, t, (int)g->score.right, c_shadow, c_digit);

}

static void render_fill_round_rect(uint16_t *dst, uint32_t w, uint32_t h, int32_t tile_x0, int32_t tile_y0,
                                   int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t r, uint16_t c)
{
    if (x1 < x0 || y1 < y0) return;
    if (r < 0) r = 0;

    int32_t cy = (y0 + y1) / 2;
    int32_t rr = r;
    int32_t xl = x0 + rr;
    int32_t xr = x1 - rr;
    if (xl > xr) xl = xr = (x0 + x1) / 2;

    sw_render_fill_rect(dst, w, h, tile_x0, tile_y0, xl, y0, xr, y1, c);
    if (rr > 0)
    {
        sw_render_filled_circle(dst, w, h, tile_x0, tile_y0, xl, cy, rr, c);
        sw_render_filled_circle(dst, w, h, tile_x0, tile_y0, xr, cy, rr, c);
    }
}

static void render_ui(uint16_t *dst, uint32_t w, uint32_t h, int32_t tile_x0, int32_t tile_y0,
                      const pong_game_t *g)
{
    if (!g) return;

    const uint16_t c_pill_bg = sw_pack_rgb565_u8(g->menu_open ? 34 : 24, g->menu_open ? 35 : 25, g->menu_open ? 39 : 29);
    const uint16_t c_pill_border = sw_pack_rgb565_u8(62, 64, 68);
    const uint16_t c_text = sw_pack_rgb565_u8(214, 215, 217);
    const uint16_t c_text_dim = sw_pack_rgb565_u8(150, 152, 156);

    const int32_t pill_x0 = EDGEAI_UI_PILL_X;
    const int32_t pill_y0 = EDGEAI_UI_PILL_Y;
    const int32_t pill_x1 = pill_x0 + EDGEAI_UI_PILL_W - 1;
    const int32_t pill_y1 = pill_y0 + EDGEAI_UI_PILL_H - 1;
    const int32_t pill_r = EDGEAI_UI_PILL_H / 2;

    render_fill_round_rect(dst, w, h, tile_x0, tile_y0, pill_x0, pill_y0, pill_x1, pill_y1, pill_r, c_pill_bg);
    /* Border. */
    render_fill_round_rect(dst, w, h, tile_x0, tile_y0, pill_x0, pill_y0, pill_x1, pill_y0 + 1, 0, c_pill_border);
    render_fill_round_rect(dst, w, h, tile_x0, tile_y0, pill_x0, pill_y1 - 1, pill_x1, pill_y1, 0, c_pill_border);

    /* Icon: three short horizontal bars. */
    int32_t ix = pill_x0 + 12;
    int32_t icon_h = EDGEAI_UI_PILL_H / 10;
    if (icon_h < 2) icon_h = 2;
    int32_t icon_gap = EDGEAI_UI_PILL_H / 8;
    if (icon_gap < 4) icon_gap = 4;
    int32_t icon_block_h = (3 * icon_h) + (2 * icon_gap);
    int32_t iy = pill_y0 + (EDGEAI_UI_PILL_H - icon_block_h) / 2;
    for (int i = 0; i < 3; i++)
    {
        int32_t y = iy + i * (icon_h + icon_gap);
        sw_render_fill_rect(dst, w, h, tile_x0, tile_y0, ix, y, ix + 20, y + icon_h - 1, c_text_dim);
    }

    /* Label: P{0,1,2} D{1,2,3} */
    char players = (g->mode == kGameModeZeroPlayer) ? '0' : (g->mode == kGameModeSinglePlayer) ? '1' : '2';
    char diff = (g->difficulty < 1) ? '1' : (g->difficulty > 3) ? '3' : (char)('0' + g->difficulty);
    /* Keep D? anchored near the help button while moving P? left toward '='. */
    char s[12] = {'P', players, ' ', ' ', ' ', 'D', diff, 0};

    const int32_t scale = 3;
    int32_t tx = EDGEAI_UI_HELP_BTN_X - edgeai_text5x7_width(scale, s) - 8;
    if (tx < (pill_x0 + 42)) tx = pill_x0 + 42;
    int32_t ty = pill_y0 + (EDGEAI_UI_PILL_H - 7 * scale) / 2;
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx, ty, scale, s, c_text);

    /* Help icon: right-aligned inside the pill. */
    {
        const uint16_t c_help_bg = sw_pack_rgb565_u8(g->help_open ? 36 : 20, g->help_open ? 37 : 21, g->help_open ? 41 : 25);

        const int32_t hx0 = EDGEAI_UI_HELP_BTN_X;
        const int32_t hy0 = EDGEAI_UI_HELP_BTN_Y;
        const int32_t hx1 = hx0 + EDGEAI_UI_HELP_BTN_W - 1;
        const int32_t hy1 = hy0 + EDGEAI_UI_HELP_BTN_H - 1;

        /* Segment background + divider. */
        render_fill_round_rect(dst, w, h, tile_x0, tile_y0, hx0, hy0, hx1, hy1, EDGEAI_UI_PILL_H / 2, c_help_bg);
        sw_render_line(dst, w, h, tile_x0, tile_y0, hx0, hy0 + 3, hx0, hy1 - 3, c_pill_border);

        const int32_t qscale = 3;
        const char *q = "?";
        int32_t qw = edgeai_text5x7_width(qscale, q);
        int32_t qx = hx0 + (EDGEAI_UI_HELP_BTN_W - qw) / 2;
        int32_t qy = hy0 + (EDGEAI_UI_HELP_BTN_H - 7 * qscale) / 2;
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, qx, qy, qscale, q, c_text);
    }

    /* Side role labels shown on the same top line as menu/? controls. */
    {
        const int32_t role_y = pill_y0 + EDGEAI_UI_PILL_H / 2;
        const int32_t left_cx = EDGEAI_UI_PILL_X - 58;
        const int32_t right_cx = EDGEAI_UI_PILL_X + EDGEAI_UI_PILL_W + 58;
        render_side_role_text(dst, w, h, tile_x0, tile_y0, left_cx, role_y, render_side_role(g, false));
        render_side_role_text(dst, w, h, tile_x0, tile_y0, right_cx, role_y, render_side_role(g, true));
    }

    /* Tilt indicator (P0): shows accel vector used to nudge the ball.
     * Helps confirm the accelerometer is alive without needing a serial console.
     */
    if (g->mode == kGameModeZeroPlayer)
    {
        const int32_t bx0 = 12;
        const int32_t by0 = pill_y0;
        const int32_t bw = 28;
        const int32_t bh = EDGEAI_UI_PILL_H;
        const int32_t bx1 = bx0 + bw - 1;
        const int32_t by1 = by0 + bh - 1;

        const uint16_t c_ind_bg = sw_pack_rgb565_u8(20, 21, 24);
        const uint16_t c_ind_border = sw_pack_rgb565_u8(52, 54, 58);
        const uint16_t c_ind_dot = sw_pack_rgb565_u8(214, 215, 217);
        const uint16_t c_ind_dim = sw_pack_rgb565_u8(120, 122, 126);

        render_fill_round_rect(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by1, bh / 2, c_ind_bg);
        sw_render_line(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by0, c_ind_border);
        sw_render_line(dst, w, h, tile_x0, tile_y0, bx0, by1, bx1, by1, c_ind_border);

        int32_t cx = bx0 + bw / 2;
        int32_t cy = by0 + bh / 2;
        int32_t r = 2;

        if (g->accel_active)
        {
            float ax = clampf(g->accel_ax, -1.0f, 1.0f);
            float ay = clampf(g->accel_ay, -1.0f, 1.0f);

            int32_t max_dx = (bw / 2) - 6;
            int32_t max_dy = (bh / 2) - 6;
            if (max_dx < 0) max_dx = 0;
            if (max_dy < 0) max_dy = 0;

            int32_t dx = (int32_t)(ax * (float)max_dx);
            int32_t dy = (int32_t)(ay * (float)max_dy);
            sw_render_filled_circle(dst, w, h, tile_x0, tile_y0, cx + dx, cy + dy, r, c_ind_dot);
        }
        else
        {
            /* No accel present: draw a subtle X. */
            sw_render_line(dst, w, h, tile_x0, tile_y0, bx0 + 6, by0 + 6, bx1 - 6, by1 - 6, c_ind_dim);
            sw_render_line(dst, w, h, tile_x0, tile_y0, bx1 - 6, by0 + 6, bx0 + 6, by1 - 6, c_ind_dim);
        }
    }

    if (!g->menu_open && !g->help_open) return;

    if (g->menu_open)
    {
        const uint16_t c_panel = sw_pack_rgb565_u8(18, 19, 22);
        const uint16_t c_panel_border = sw_pack_rgb565_u8(52, 54, 58);
        const uint16_t c_opt = sw_pack_rgb565_u8(26, 27, 31);
        const uint16_t c_opt_sel = sw_pack_rgb565_u8(214, 215, 217);
        const uint16_t c_opt_text = sw_pack_rgb565_u8(214, 215, 217);
        const uint16_t c_opt_text_sel = sw_pack_rgb565_u8(10, 10, 12);

        const int32_t panel_x0 = EDGEAI_UI_PANEL_X;
        const int32_t panel_y0 = EDGEAI_UI_PANEL_Y;
        const int32_t panel_x1 = panel_x0 + EDGEAI_UI_PANEL_W - 1;
        const int32_t panel_y1 = panel_y0 + EDGEAI_UI_PANEL_H - 1;

        render_fill_round_rect(dst, w, h, tile_x0, tile_y0, panel_x0, panel_y0, panel_x1, panel_y1, 10, c_panel);
        sw_render_line(dst, w, h, tile_x0, tile_y0, panel_x0, panel_y0, panel_x1, panel_y0, c_panel_border);
        sw_render_line(dst, w, h, tile_x0, tile_y0, panel_x0, panel_y1, panel_x1, panel_y1, c_panel_border);
        sw_render_line(dst, w, h, tile_x0, tile_y0, panel_x0, panel_y0, panel_x0, panel_y1, c_panel_border);
        sw_render_line(dst, w, h, tile_x0, tile_y0, panel_x1, panel_y0, panel_x1, panel_y1, c_panel_border);

        const int32_t title_scale = 1;
        const int32_t opt_scale = 2;
        const int32_t label_yoff = (EDGEAI_UI_ROW_H - 7 * title_scale) / 2;
        const int32_t opt_yoff = (EDGEAI_UI_ROW_H - EDGEAI_UI_OPT_H) / 2;
        const int32_t new_yoff = (EDGEAI_UI_ROW_H - EDGEAI_UI_NEW_H) / 2;

        /* Row labels. */
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, EDGEAI_UI_LABEL_X, EDGEAI_UI_ROW0_Y + label_yoff, title_scale, "PLAYERS", c_opt_text);
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, EDGEAI_UI_LABEL_X, EDGEAI_UI_ROW1_Y + label_yoff, title_scale, "LEVEL", c_opt_text);
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, EDGEAI_UI_LABEL_X, EDGEAI_UI_ROW2_Y + label_yoff, title_scale, "NPU", c_opt_text);
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, EDGEAI_UI_LABEL_X, EDGEAI_UI_ROW3_Y + label_yoff, title_scale, "SKILL", c_opt_text);
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, EDGEAI_UI_LABEL_X, EDGEAI_UI_ROW4_Y + label_yoff, title_scale, "PERSIST", c_opt_text);
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, EDGEAI_UI_LABEL_X, EDGEAI_UI_ROW5_Y + label_yoff, title_scale, "MATCH", c_opt_text);
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, EDGEAI_UI_LABEL_X, EDGEAI_UI_ROW6_Y + label_yoff, title_scale, "TARGET", c_opt_text);
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, EDGEAI_UI_LABEL_X, EDGEAI_UI_ROW7_Y + label_yoff, title_scale, "SPEED++", c_opt_text);
        {
            const int32_t volume_scale = 2;
            const int32_t volume_label_yoff = (EDGEAI_UI_ROW_H - 7 * volume_scale) / 2;
            edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0,
                                          EDGEAI_UI_LABEL_X, EDGEAI_UI_ROW8_Y + volume_label_yoff,
                                          volume_scale, "VOL", c_opt_text);
        }
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, EDGEAI_UI_LABEL_X, EDGEAI_UI_ROW9_Y + label_yoff, title_scale, "NEW GAME", c_opt_text);

        /* Players: 0/1/2 */
        for (int i = 0; i < 3; i++)
        {
            int32_t bx0 = EDGEAI_UI_OPT_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
            int32_t by0 = EDGEAI_UI_ROW0_Y + opt_yoff;
            int32_t bx1 = bx0 + EDGEAI_UI_OPT_W - 1;
            int32_t by1 = by0 + EDGEAI_UI_OPT_H - 1;

            bool sel = (g->mode == (i == 0 ? kGameModeZeroPlayer : i == 1 ? kGameModeSinglePlayer : kGameModeTwoPlayer));
            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by1, EDGEAI_UI_OPT_H / 2, sel ? c_opt_sel : c_opt);

            char t[2] = {(char)('0' + i), 0};
            int32_t tw = edgeai_text5x7_width(opt_scale, t);
            int32_t tx0 = bx0 + (EDGEAI_UI_OPT_W - tw) / 2;
            int32_t ty0 = by0 + (EDGEAI_UI_OPT_H - 7 * opt_scale) / 2;
            edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx0, ty0, opt_scale, t, sel ? c_opt_text_sel : c_opt_text);
        }

        /* Difficulty: 1/2/3 */
        for (int i = 0; i < 3; i++)
        {
            int32_t bx0 = EDGEAI_UI_OPT_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
            int32_t by0 = EDGEAI_UI_ROW1_Y + opt_yoff;
            int32_t bx1 = bx0 + EDGEAI_UI_OPT_W - 1;
            int32_t by1 = by0 + EDGEAI_UI_OPT_H - 1;

            uint8_t d = (uint8_t)(i + 1);
            bool sel = (g->difficulty == d);
            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by1, EDGEAI_UI_OPT_H / 2, sel ? c_opt_sel : c_opt);

            char t[2] = {(char)('0' + (i + 1)), 0};
            int32_t tw = edgeai_text5x7_width(opt_scale, t);
            int32_t tx0 = bx0 + (EDGEAI_UI_OPT_W - tw) / 2;
            int32_t ty0 = by0 + (EDGEAI_UI_OPT_H - 7 * opt_scale) / 2;
            edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx0, ty0, opt_scale, t, sel ? c_opt_text_sel : c_opt_text);
        }

        /* AI: ON/OFF */
        for (int i = 0; i < 2; i++)
        {
            int32_t bx0 = EDGEAI_UI_OPT2_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
            int32_t by0 = EDGEAI_UI_ROW2_Y + opt_yoff;
            int32_t bx1 = bx0 + EDGEAI_UI_OPT_W - 1;
            int32_t by1 = by0 + EDGEAI_UI_OPT_H - 1;

            bool en = (i == 0);
            bool sel = (g->ai_enabled == en);
            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by1, EDGEAI_UI_OPT_H / 2, sel ? c_opt_sel : c_opt);

            const char *t = en ? "ON" : "OFF";
            int32_t tw = edgeai_text5x7_width(opt_scale, t);
            int32_t tx0 = bx0 + (EDGEAI_UI_OPT_W - tw) / 2;
            int32_t ty0 = by0 + (EDGEAI_UI_OPT_H - 7 * opt_scale) / 2;
            edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx0, ty0, opt_scale, t, sel ? c_opt_text_sel : c_opt_text);
        }

        /* Skill mode: 2AI, AI/ALGO (left AI), ALGO/AI (right AI). */
        for (int i = 0; i < 3; i++)
        {
            int32_t bx0 = EDGEAI_UI_OPT_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
            int32_t by0 = EDGEAI_UI_ROW3_Y + opt_yoff;
            int32_t bx1 = bx0 + EDGEAI_UI_OPT_W - 1;
            int32_t by1 = by0 + EDGEAI_UI_OPT_H - 1;

            ai_learn_mode_t mode =
                (i == 0) ? kAiLearnModeBoth : ((i == 1) ? kAiLearnModeAiAlgo : kAiLearnModeAlgoAi);
            bool sel = (g->ai_learn_mode == mode);
            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by1, EDGEAI_UI_OPT_H / 2, sel ? c_opt_sel : c_opt);

            const int32_t learn_scale = 1;
            const char *t = (i == 0) ? "2AI" : ((i == 1) ? "AI/ALGO" : "ALGO/AI");
            int32_t tw = edgeai_text5x7_width(learn_scale, t);
            int32_t tx0 = bx0 + (EDGEAI_UI_OPT_W - tw) / 2;
            int32_t ty0 = by0 + (EDGEAI_UI_OPT_H - 7 * learn_scale) / 2;
            edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx0, ty0, learn_scale, t, sel ? c_opt_text_sel : c_opt_text);
        }

        /* Persistent learning: ON/OFF */
        for (int i = 0; i < 2; i++)
        {
            int32_t bx0 = EDGEAI_UI_OPT2_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
            int32_t by0 = EDGEAI_UI_ROW4_Y + opt_yoff;
            int32_t bx1 = bx0 + EDGEAI_UI_OPT_W - 1;
            int32_t by1 = by0 + EDGEAI_UI_OPT_H - 1;

            bool en = (i == 0);
            bool sel = (g->persistent_learning == en);
            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by1, EDGEAI_UI_OPT_H / 2, sel ? c_opt_sel : c_opt);

            const char *t = en ? "ON" : "OFF";
            int32_t tw = edgeai_text5x7_width(opt_scale, t);
            int32_t tx0 = bx0 + (EDGEAI_UI_OPT_W - tw) / 2;
            int32_t ty0 = by0 + (EDGEAI_UI_OPT_H - 7 * opt_scale) / 2;
            edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx0, ty0, opt_scale, t, sel ? c_opt_text_sel : c_opt_text);
        }

        /* Match target: 11, 100, 1K(999). */
        for (int i = 0; i < 3; i++)
        {
            int32_t bx0 = EDGEAI_UI_OPT_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
            int32_t by0 = EDGEAI_UI_ROW5_Y + opt_yoff;
            int32_t bx1 = bx0 + EDGEAI_UI_OPT_W - 1;
            int32_t by1 = by0 + EDGEAI_UI_OPT_H - 1;

            uint16_t tgt = (i == 0) ? 11u : ((i == 1) ? 100u : 999u);
            bool sel = (g->match_target == tgt);
            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by1, EDGEAI_UI_OPT_H / 2, sel ? c_opt_sel : c_opt);

            const char *t = (i == 0) ? "11" : ((i == 1) ? "100" : "1K");
            int32_t tw = edgeai_text5x7_width(opt_scale, t);
            int32_t tx0 = bx0 + (EDGEAI_UI_OPT_W - tw) / 2;
            int32_t ty0 = by0 + (EDGEAI_UI_OPT_H - 7 * opt_scale) / 2;
            edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx0, ty0, opt_scale, t, sel ? c_opt_text_sel : c_opt_text);
        }

        /* Target guide: ON/OFF */
        for (int i = 0; i < 2; i++)
        {
            int32_t bx0 = EDGEAI_UI_OPT2_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
            int32_t by0 = EDGEAI_UI_ROW6_Y + opt_yoff;
            int32_t bx1 = bx0 + EDGEAI_UI_OPT_W - 1;
            int32_t by1 = by0 + EDGEAI_UI_OPT_H - 1;

            bool en = (i == 0);
            bool sel = (g->target_overlay_enabled == en);
            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by1, EDGEAI_UI_OPT_H / 2, sel ? c_opt_sel : c_opt);

            const char *t = en ? "ON" : "OFF";
            int32_t tw = edgeai_text5x7_width(opt_scale, t);
            int32_t tx0 = bx0 + (EDGEAI_UI_OPT_W - tw) / 2;
            int32_t ty0 = by0 + (EDGEAI_UI_OPT_H - 7 * opt_scale) / 2;
            edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx0, ty0, opt_scale, t, sel ? c_opt_text_sel : c_opt_text);
        }

        /* SPEED++: ON/OFF */
        for (int i = 0; i < 2; i++)
        {
            int32_t bx0 = EDGEAI_UI_OPT2_BLOCK_X + i * (EDGEAI_UI_OPT_W + EDGEAI_UI_OPT_GAP);
            int32_t by0 = EDGEAI_UI_ROW7_Y + opt_yoff;
            int32_t bx1 = bx0 + EDGEAI_UI_OPT_W - 1;
            int32_t by1 = by0 + EDGEAI_UI_OPT_H - 1;

            bool en = (i == 0);
            bool sel = (g->speedpp_enabled == en);
            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by1, EDGEAI_UI_OPT_H / 2, sel ? c_opt_sel : c_opt);

            const char *t = en ? "ON" : "OFF";
            int32_t tw = edgeai_text5x7_width(opt_scale, t);
            int32_t tx0 = bx0 + (EDGEAI_UI_OPT_W - tw) / 2;
            int32_t ty0 = by0 + (EDGEAI_UI_OPT_H - 7 * opt_scale) / 2;
            edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx0, ty0, opt_scale, t, sel ? c_opt_text_sel : c_opt_text);
        }

        /* Volume: large DOWN / UP buttons with centered numeric value. */
        {
            const int32_t vol_x = EDGEAI_UI_PANEL_X + 12;
            const int32_t vol_left_w = 88;
            const int32_t vol_center_w = 60;
            const int32_t vol_right_w = 88;

            int32_t bx0 = vol_x;
            int32_t by0 = EDGEAI_UI_ROW8_Y + opt_yoff;
            int32_t bx1 = bx0 + vol_left_w - 1;
            int32_t by1 = by0 + EDGEAI_UI_OPT_H - 1;
            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by1, EDGEAI_UI_OPT_H / 2, c_opt);
            {
                const int32_t vol_text_scale = 2;
                edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0,
                                              bx0 + (vol_left_w - edgeai_text5x7_width(vol_text_scale, "VOL DN")) / 2,
                                              by0 + (EDGEAI_UI_OPT_H - 7 * vol_text_scale) / 2,
                                              vol_text_scale, "VOL DN", c_opt_text);
            }

            int32_t cbx0 = vol_x + vol_left_w;
            int32_t cbx1 = cbx0 + vol_center_w - 1;
            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, cbx0, by0, cbx1, by1, EDGEAI_UI_OPT_H / 2, c_panel);

            int32_t rbx0 = cbx1 + 1;
            int32_t rbx1 = rbx0 + vol_right_w - 1;
            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, rbx0, by0, rbx1, by1, EDGEAI_UI_OPT_H / 2, c_opt);
            {
                const int32_t vol_text_scale = 2;
                edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0,
                                              rbx0 + (vol_right_w - edgeai_text5x7_width(vol_text_scale, "UP")) / 2,
                                              by0 + (EDGEAI_UI_OPT_H - 7 * vol_text_scale) / 2,
                                              vol_text_scale, "UP", c_opt_text);
            }

            char t[4];
            t[0] = (char)('0' + ((g->audio_volume / 100u) % 10u));
            t[1] = (char)('0' + ((g->audio_volume / 10u) % 10u));
            t[2] = (char)('0' + (g->audio_volume % 10u));
            t[3] = 0;
            const int32_t vol_value_scale = 2;
            int32_t tw = edgeai_text5x7_width(vol_value_scale, t);
            int32_t tx0 = cbx0 + (vol_center_w - tw) / 2;
            int32_t ty0 = by0 + (EDGEAI_UI_OPT_H - 7 * vol_value_scale) / 2;
            edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx0, ty0, vol_value_scale, t, c_opt_text);
        }

        /* New game. */
        {
            int32_t bx0 = EDGEAI_UI_NEW_X;
            int32_t by0 = EDGEAI_UI_ROW9_Y + new_yoff;
            int32_t bx1 = bx0 + EDGEAI_UI_NEW_W - 1;
            int32_t by1 = by0 + EDGEAI_UI_NEW_H - 1;

            render_fill_round_rect(dst, w, h, tile_x0, tile_y0, bx0, by0, bx1, by1, EDGEAI_UI_NEW_H / 2, c_opt);
            const int32_t tscale = 2;
            const char *t = "0:0";
            int32_t tw = edgeai_text5x7_width(tscale, t);
            int32_t tx0 = bx0 + (EDGEAI_UI_NEW_W - tw) / 2;
            int32_t ty0 = by0 + (EDGEAI_UI_NEW_H - 7 * tscale) / 2;
            edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx0, ty0, tscale, t, c_opt_text);
        }
        return;
    }

    /* Help panel. */
    {
        const uint16_t c_panel = sw_pack_rgb565_u8(18, 19, 22);
        const uint16_t c_panel_border = sw_pack_rgb565_u8(52, 54, 58);
        const uint16_t c_body = sw_pack_rgb565_u8(214, 215, 217);
        const uint16_t c_dim = sw_pack_rgb565_u8(150, 152, 156);

        const int32_t panel_x0 = EDGEAI_UI_HELP_PANEL_X;
        const int32_t panel_y0 = EDGEAI_UI_HELP_PANEL_Y;
        const int32_t panel_x1 = panel_x0 + EDGEAI_UI_HELP_PANEL_W - 1;
        const int32_t panel_y1 = panel_y0 + EDGEAI_UI_HELP_PANEL_H - 1;

        render_fill_round_rect(dst, w, h, tile_x0, tile_y0, panel_x0, panel_y0, panel_x1, panel_y1, 12, c_panel);
        sw_render_line(dst, w, h, tile_x0, tile_y0, panel_x0, panel_y0, panel_x1, panel_y0, c_panel_border);
        sw_render_line(dst, w, h, tile_x0, tile_y0, panel_x0, panel_y1, panel_x1, panel_y1, c_panel_border);
        sw_render_line(dst, w, h, tile_x0, tile_y0, panel_x0, panel_y0, panel_x0, panel_y1, c_panel_border);
        sw_render_line(dst, w, h, tile_x0, tile_y0, panel_x1, panel_y0, panel_x1, panel_y1, c_panel_border);

        const int32_t xpad = 14;
        const int32_t ypad = 10;
        int32_t x = panel_x0 + xpad;
        int32_t y = panel_y0 + ypad;

        const int32_t s1 = 2;
        const int32_t lh = 15;

        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, 2, "GAME RULES", c_body);
        y += 16;

        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, s1, "MATCH: 11 / 100 / 1K", c_body);
        y += lh;
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, s1, "MISS = OPP +1", c_body);
        y += lh;
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, s1, "TOP/BOTTOM: BOUNCE", c_body);
        y += lh;
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, s1, "EDGE HIT = STEEPER", c_body);
        y += lh;
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, s1, "BALL SPEEDS UP", c_body);
        y += lh + 4;

        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, 2, "CONTROLS", c_dim);
        y += 16;
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, s1, "EDGE STRIPS: P1 / P2", c_body);
        y += lh;
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, s1, "TOUCH Y=UP/DOWN", c_body);
        y += lh;
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, s1, "TOUCH X=DEPTH", c_body);
        y += lh;
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, s1, "SKILL: 2AI AI/ALGO", c_body);
        y += lh + 4;

        (void)panel_y1;
    }
}

static void render_confetti(uint16_t *dst, uint32_t w, uint32_t h, int32_t tile_x0, int32_t tile_y0,
                            const pong_game_t *g)
{
    if (!g || !g->match_over) return;

    float t = 0.0f;
    if (g->match_over_start_cycles != 0u)
    {
        uint32_t elapsed_us = time_hal_elapsed_us(g->match_over_start_cycles);
        t = ((float)elapsed_us * 1.0e-6f) * EDGEAI_CONFETTI_TIME_SCALE;
    }
    else
    {
        /* Fallback path if match-over start cycles are not initialized. */
        t = (float)(g->frame - g->match_over_frame) * (1.0f / (float)EDGEAI_FIXED_FPS) * EDGEAI_CONFETTI_TIME_SCALE;
    }
    if (t < 0.0f) t = 0.0f;
    if (t > EDGEAI_CONFETTI_MAX_T_S) t = EDGEAI_CONFETTI_MAX_T_S;

    static const uint16_t pal[] = {
        0xF800u, /* red */
        0x07E0u, /* green */
        0x001Fu, /* blue */
        0xFFE0u, /* yellow */
        0xF81Fu, /* magenta */
        0x07FFu, /* cyan */
    };

    for (uint32_t i = 0; i < EDGEAI_CONFETTI_COUNT; i++)
    {
        uint32_t h0 = render_hash_u32(0x9E3779B9u * (i + 1u));
        uint32_t h1 = render_hash_u32(h0 ^ 0xA5A5A5A5u);

        float x0f = (float)(h0 % (uint32_t)EDGEAI_LCD_W);
        float y0f = -(float)(h0 % 120u);
        float vxf = ((float)((h0 >> 8) & 0xFFu) * (1.0f / 255.0f)) * 160.0f - 80.0f;
        float vyf = 55.0f + ((float)((h0 >> 16) & 0xFFu) * (1.0f / 255.0f)) * 150.0f;
        float gacc = 160.0f;

        int32_t x = (int32_t)(x0f + vxf * t);
        int32_t y = (int32_t)(y0f + vyf * t + 0.5f * gacc * t * t);
        int32_t s = 2 + (int32_t)((h1 >> 29) & 0x3u);
        uint16_t c = pal[h1 % (sizeof(pal) / sizeof(pal[0]))];

        if (x < -s || y < -s || x >= EDGEAI_LCD_W || y >= EDGEAI_LCD_H) continue;
        sw_render_fill_rect(dst, w, h, tile_x0, tile_y0, x, y, x + s - 1, y + s - 1, c);
    }
}

static void render_end_popup(uint16_t *dst, uint32_t w, uint32_t h, int32_t tile_x0, int32_t tile_y0,
                             const pong_game_t *g)
{
    if (!render_end_prompt_visible(g)) return;

    const uint16_t c_panel = sw_pack_rgb565_u8(18, 19, 22);
    const uint16_t c_border = sw_pack_rgb565_u8(64, 66, 70);
    const uint16_t c_text = sw_pack_rgb565_u8(234, 235, 238);
    const uint16_t c_yes = sw_pack_rgb565_u8(46, 146, 62);
    const uint16_t c_no = sw_pack_rgb565_u8(156, 36, 36);
    const uint16_t c_btn_text = sw_pack_rgb565_u8(240, 240, 242);

    int32_t px0 = EDGEAI_END_PANEL_X;
    int32_t py0 = EDGEAI_END_PANEL_Y;
    int32_t px1 = px0 + EDGEAI_END_PANEL_W - 1;
    int32_t py1 = py0 + EDGEAI_END_PANEL_H - 1;

    render_fill_round_rect(dst, w, h, tile_x0, tile_y0, px0, py0, px1, py1, 10, c_panel);
    sw_render_line(dst, w, h, tile_x0, tile_y0, px0, py0, px1, py0, c_border);
    sw_render_line(dst, w, h, tile_x0, tile_y0, px0, py1, px1, py1, c_border);
    sw_render_line(dst, w, h, tile_x0, tile_y0, px0, py0, px0, py1, c_border);
    sw_render_line(dst, w, h, tile_x0, tile_y0, px1, py0, px1, py1, c_border);

    {
        const char *title = "NEW GAME?";
        int32_t ts = 2;
        int32_t tw = edgeai_text5x7_width(ts, title);
        int32_t tx = px0 + (EDGEAI_END_PANEL_W - tw) / 2;
        int32_t ty = py0 + 14;
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, tx, ty, ts, title, c_text);
    }

    {
        int32_t yx0 = EDGEAI_END_BTN_YES_X;
        int32_t yy0 = EDGEAI_END_BTN_Y;
        int32_t yx1 = yx0 + EDGEAI_END_BTN_W - 1;
        int32_t yy1 = yy0 + EDGEAI_END_BTN_H - 1;
        render_fill_round_rect(dst, w, h, tile_x0, tile_y0, yx0, yy0, yx1, yy1, 8, c_yes);
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, yx0 + 20, yy0 + 5, 2, "YES", c_btn_text);
    }

    {
        int32_t nx0 = EDGEAI_END_BTN_NO_X;
        int32_t ny0 = EDGEAI_END_BTN_Y;
        int32_t nx1 = nx0 + EDGEAI_END_BTN_W - 1;
        int32_t ny1 = ny0 + EDGEAI_END_BTN_H - 1;
        render_fill_round_rect(dst, w, h, tile_x0, tile_y0, nx0, ny0, nx1, ny1, 8, c_no);
        edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, nx0 + 24, ny0 + 5, 2, "NO", c_btn_text);
    }
}

static void render_ai_telemetry(uint16_t *dst, uint32_t w, uint32_t h, int32_t tile_x0, int32_t tile_y0,
                                const pong_game_t *g)
{
    if (!g || !g->ai_enabled) return;
    if (!(g->mode == kGameModeZeroPlayer || g->mode == kGameModeSinglePlayer)) return;

    const int32_t x = 8;
    const int32_t tscale = 2;
    const int32_t line_h = 7 * tscale;
    const int32_t line_gap = 2;
    const int32_t y = EDGEAI_LCD_H - (2 * line_h + line_gap + 2);
    const int32_t ow = 210;
    const int32_t oh = (2 * line_h) + line_gap + 2;
    const int32_t tx0 = tile_x0;
    const int32_t ty0 = tile_y0;
    const int32_t tx1 = tile_x0 + (int32_t)w - 1;
    const int32_t ty1 = tile_y0 + (int32_t)h - 1;
    if (!rect_intersects(x, y, x + ow, y + oh, tx0, ty0, tx1, ty1)) return;

    static uint32_t s_rate_start_cycles = 0u;
    static uint32_t s_prev_invoke = 0u;
    static uint16_t s_n_ms = 0u;
    static uint16_t s_f_ms = 0u;
    static uint32_t s_cached_frame = 0xFFFFFFFFu;
    static char s_line1[] = "N000MS F000MS";
    static char s_line2[] = "L000MS A000MS";

    if (s_cached_frame != g->frame)
    {
        npu_telemetry_t npu_t;
        if (!npu_hal_get_telemetry(&g->npu, &npu_t)) return;

        if (s_rate_start_cycles == 0u)
        {
            s_rate_start_cycles = time_hal_cycles();
            s_prev_invoke = npu_t.invoke_count;
        }
        else
        {
            uint32_t elapsed_us = time_hal_elapsed_us(s_rate_start_cycles);
            if (elapsed_us >= 250000u)
            {
                uint32_t dinv = npu_t.invoke_count - s_prev_invoke;
                uint32_t n_ms = 0u;
                uint32_t f_ms = 0u;
                if (dinv > 0u)
                {
                    uint64_t num = (uint64_t)elapsed_us + ((uint64_t)dinv * 500ull);
                    n_ms = (uint32_t)(num / ((uint64_t)dinv * 1000ull));
                }
                /* F tracks software fallback update period from runtime fallback cadence. */
                if (g->ai_fallback_rate_hz > 0u)
                {
                    uint32_t hz = g->ai_fallback_rate_hz;
                    f_ms = (1000u + (hz / 2u)) / hz;
                }
                if (n_ms > 999u) n_ms = 999u;
                if (f_ms > 999u) f_ms = 999u;
                s_n_ms = (uint16_t)n_ms;
                s_f_ms = (uint16_t)f_ms;
                s_prev_invoke = npu_t.invoke_count;
                s_rate_start_cycles = time_hal_cycles();
            }
        }

        char n_ms[3];
        char f_ms[3];
        char last_ms[3];
        char avg_ms[3];
        text_u3(n_ms, s_n_ms);
        text_u3(f_ms, s_f_ms);
        uint32_t last_v = (npu_t.last_infer_us + 500u) / 1000u;
        uint32_t avg_v = (npu_t.avg_infer_us + 500u) / 1000u;
        if ((last_v == 0u) && (npu_t.last_infer_us > 0u)) last_v = 1u;
        if ((avg_v == 0u) && (npu_t.avg_infer_us > 0u)) avg_v = 1u;
        text_u3(last_ms, last_v);
        text_u3(avg_ms, avg_v);

        s_line1[1] = n_ms[0];
        s_line1[2] = n_ms[1];
        s_line1[3] = n_ms[2];
        s_line1[8] = f_ms[0];
        s_line1[9] = f_ms[1];
        s_line1[10] = f_ms[2];

        s_line2[1] = last_ms[0];
        s_line2[2] = last_ms[1];
        s_line2[3] = last_ms[2];
        s_line2[8] = avg_ms[0];
        s_line2[9] = avg_ms[1];
        s_line2[10] = avg_ms[2];

        s_cached_frame = g->frame;
    }

    const uint16_t c = sw_pack_rgb565_u8(212, 214, 216);
    const uint16_t cs = sw_pack_rgb565_u8(10, 10, 12);

    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x + 1, y + 1, tscale, s_line1, cs);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x + 1, y + line_h + line_gap + 1, tscale, s_line2, cs);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, tscale, s_line1, c);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y + line_h + line_gap, tscale, s_line2, c);
}

static void render_corner_credit(uint16_t *dst, uint32_t w, uint32_t h, int32_t tile_x0, int32_t tile_y0)
{
    if (!dst) return;

    const char *credit = "\xA9""RICHARD HABERKERN";
    const int32_t scale = 2;
    const int32_t pad_x = 6;
    const int32_t pad_y = 12;
    const int32_t tw = edgeai_text5x7_width(scale, credit);
    const int32_t th = 7 * scale;
    const int32_t x = EDGEAI_LCD_W - tw - pad_x;
    const int32_t y = EDGEAI_LCD_H - th - pad_y;

    const uint16_t c = sw_pack_rgb565_u8(122, 124, 128);
    const uint16_t cs = sw_pack_rgb565_u8(8, 8, 10);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x + 1, y + 1, scale, credit, cs);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, x, y, scale, credit, c);
}

static void render_bottom_win_scores(uint16_t *dst, uint32_t w, uint32_t h, int32_t tile_x0, int32_t tile_y0,
                                     const pong_game_t *g)
{
    if (!dst || !g) return;

    uint32_t wins_l = g->score_total_left;
    uint32_t wins_r = g->score_total_right;
    if (wins_l > 999u) wins_l = 999u;
    if (wins_r > 999u) wins_r = 999u;

    char l_digits[3];
    char r_digits[3];
    text_u3(l_digits, wins_l);
    text_u3(r_digits, wins_r);

    const char *l_label = "WINS";
    char l_num[] = "000";
    char r_num[] = "000";
    l_num[0] = l_digits[0];
    l_num[1] = l_digits[1];
    l_num[2] = l_digits[2];
    r_num[0] = r_digits[0];
    r_num[1] = r_digits[1];
    r_num[2] = r_digits[2];

    /* Place WINS text inside the bottom status row, close to each side of the net. */
    const int32_t bar_y = EDGEAI_LCD_H - 24;
    const int32_t bar_h = 23;
    const int32_t net_x = EDGEAI_LCD_W / 2;
    const int32_t scale = 3;
    const int32_t text_y = bar_y + ((bar_h - (7 * scale)) / 2);
    const uint16_t c = sw_pack_rgb565_u8(232, 234, 238);
    const uint16_t cs = sw_pack_rgb565_u8(8, 8, 10);

    int32_t char_w = edgeai_text5x7_width(scale, "0");
    int32_t num_w = edgeai_text5x7_width(scale, l_num);
    int32_t label_w = edgeai_text5x7_width(scale, l_label);
    int32_t inter_num_gap = 3 * char_w;

    int32_t left_num_cx = net_x - (inter_num_gap / 2) - (num_w / 2);
    int32_t right_num_cx = net_x + (inter_num_gap / 2) + (num_w / 2);
    int32_t left_num_x = left_num_cx - (num_w / 2);
    int32_t right_num_x = right_num_cx - (num_w / 2);
    int32_t left_label_x = left_num_x - (char_w / 2) - label_w;

    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, left_label_x + 1, text_y + 1, scale, l_label, cs);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, left_label_x, text_y, scale, l_label, c);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, left_num_x + 1, text_y + 1, scale, l_num, cs);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, left_num_x, text_y, scale, l_num, c);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, right_num_x + 1, text_y + 1, scale, r_num, cs);
    edgeai_text5x7_draw_scaled_sw(dst, w, h, tile_x0, tile_y0, right_num_x, text_y, scale, r_num, c);
}

typedef struct
{
    float z0; /* near */
    float z1; /* far */
    float zc; /* center */
} render_depth_range_t;

static bool render_depth_behind(const render_depth_range_t r[3], int a, int b, float eps)
{
    return r && (r[a].z0 >= (r[b].z1 + eps));
}

static bool render_depth_overlap(const render_depth_range_t r[3], int a, int b)
{
    return r && (r[a].z0 <= r[b].z1) && (r[a].z1 >= r[b].z0);
}

static uint8_t render_depth_order3(const pong_game_t *g, uint8_t out_order[3])
{
    if (!out_order) return 0u;
    if (!g)
    {
        out_order[0] = 0u;
        out_order[1] = 1u;
        out_order[2] = 2u;
        return 3u;
    }

    float lhz = g->paddle_l.size_z * 0.5f;
    float rhz = g->paddle_r.size_z * 0.5f;

    render_depth_range_t r[3] = {
        {g->paddle_l.z - lhz, g->paddle_l.z + lhz, g->paddle_l.z},
        {g->paddle_r.z - rhz, g->paddle_r.z + rhz, g->paddle_r.z},
        {g->ball.z - g->ball.r, g->ball.z + g->ball.r, g->ball.z},
    };

    const float eps = 1e-4f;

    static const uint8_t perms[6][3] = {
        {0u, 1u, 2u},
        {0u, 2u, 1u},
        {1u, 0u, 2u},
        {1u, 2u, 0u},
        {2u, 0u, 1u},
        {2u, 1u, 0u},
    };

    int best_i = 0;
    int best_score = 0x7FFFFFFF;

    for (int pi = 0; pi < 6; pi++)
    {
        const uint8_t *p = perms[pi];

        int pos[3] = {0, 0, 0};
        pos[p[0]] = 0;
        pos[p[1]] = 1;
        pos[p[2]] = 2;

        int score = 0;

        /* Hard constraints: if A is fully behind B, A must be drawn first. */
        for (int a = 0; a < 3; a++)
        {
            for (int b = a + 1; b < 3; b++)
            {
                if (render_depth_behind(r, a, b, eps) && (pos[a] > pos[b])) score += 1000;
                if (render_depth_behind(r, b, a, eps) && (pos[b] > pos[a])) score += 1000;
            }
        }

        /* Soft preference: if z ranges overlap, draw the ball on top (last) for readability. */
        if (render_depth_overlap(r, 2, 0) && (pos[2] < pos[0])) score += 1;
        if (render_depth_overlap(r, 2, 1) && (pos[2] < pos[1])) score += 1;

        /* Tie-breaker: prefer far->near by center z when possible. */
        for (int a = 0; a < 3; a++)
        {
            for (int b = a + 1; b < 3; b++)
            {
                if ((r[a].zc > r[b].zc) && (pos[a] > pos[b])) score += 1;
                if ((r[b].zc > r[a].zc) && (pos[b] > pos[a])) score += 1;
            }
        }

        if (score < best_score)
        {
            best_score = score;
            best_i = pi;
        }
    }

    out_order[0] = perms[best_i][0];
    out_order[1] = perms[best_i][1];
    out_order[2] = perms[best_i][2];
    return 3u;
}

void render_draw_frame(render_state_t *rs, const pong_game_t *g)
{
    if (!rs || !g) return;

    uint8_t draw_order[3] = {0u, 1u, 2u};
    (void)render_depth_order3(g, draw_order);

    for (int32_t x0 = 0; x0 < EDGEAI_LCD_W; x0 += EDGEAI_TILE_MAX_W)
    {
        int32_t x1 = x0 + EDGEAI_TILE_MAX_W - 1;
        if (x1 >= EDGEAI_LCD_W) x1 = EDGEAI_LCD_W - 1;
        int32_t w = x1 - x0 + 1;

        for (int32_t y0 = 0; y0 < EDGEAI_LCD_H; y0 += EDGEAI_TILE_MAX_H)
        {
            int32_t y1 = y0 + EDGEAI_TILE_MAX_H - 1;
            if (y1 >= EDGEAI_LCD_H) y1 = EDGEAI_LCD_H - 1;
            int32_t h = y1 - y0 + 1;

            render_bg_tile(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, rs);

            render_scores(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, rs, g);

            /* Painter's algorithm by depth (z): far -> near, with simple z-range handling. */
            for (int i = 0; i < 3; i++)
            {
                switch (draw_order[i])
                {
                    case 0u:
                        render_draw_paddle(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, rs, &g->paddle_l);
                        break;
                    case 1u:
                        render_draw_paddle(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, rs, &g->paddle_r);
                        break;
                    default:
                        render_draw_ball(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, rs, &g->ball);
                        break;
                }
            }

            render_draw_target_guides(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, rs, g);
            render_confetti(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, g);
            render_ui(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, g);
            render_ai_telemetry(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, g);
            render_bottom_win_scores(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, g);
            render_countdown(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, g);
            render_end_popup(s_tile, (uint32_t)w, (uint32_t)h, x0, y0, g);
            render_corner_credit(s_tile, (uint32_t)w, (uint32_t)h, x0, y0);

            display_hal_blit_rect(x0, y0, x1, y1, s_tile);
        }
    }
    display_hal_present_frame();
}
