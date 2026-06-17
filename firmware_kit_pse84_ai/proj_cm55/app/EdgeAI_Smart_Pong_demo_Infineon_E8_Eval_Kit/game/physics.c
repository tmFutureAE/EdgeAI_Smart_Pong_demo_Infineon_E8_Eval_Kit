#include "game/physics.h"

#include <math.h>
#include <stddef.h>

#include "game/ai.h"

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline float absf(float v)
{
    return (v < 0.0f) ? -v : v;
}

static inline float signf(float v)
{
    return (v < 0.0f) ? -1.0f : 1.0f;
}

static inline uint32_t xorshift32(uint32_t x)
{
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return x;
}

static float rand_f01(pong_game_t *g)
{
    g->rng = xorshift32(g->rng ? g->rng : 1u);
    /* 24-bit mantissa fraction. */
    uint32_t u = (g->rng >> 8) & 0x00FFFFFFu;
    return (float)u * (1.0f / 16777215.0f);
}

static float rand_f(pong_game_t *g, float lo, float hi)
{
    return lo + (hi - lo) * rand_f01(g);
}

static float physics_biased_sign(pong_game_t *g, float bias)
{
    if (bias > 0.12f) return 1.0f;
    if (bias < -0.12f) return -1.0f;
    return (rand_f01(g) < 0.5f) ? -1.0f : 1.0f;
}

static void physics_pick_ai_serve_lateral(pong_game_t *g, float dir, float serve_speed, float *vy, float *vz)
{
    if (!g || !vy || !vz)
    {
        return;
    }

    const pong_paddle_t *recv = (dir < 0.0f) ? &g->paddle_l : &g->paddle_r;
    float bias_y = clampf(0.5f - recv->y, -1.0f, 1.0f);
    float bias_z = clampf(0.5f - recv->z, -1.0f, 1.0f);

    float max_y = clampf(0.17f * serve_speed, 0.18f, 0.52f);
    float max_z = clampf(0.14f * serve_speed, 0.14f, 0.44f);
    float sy = physics_biased_sign(g, bias_y);
    float sz = physics_biased_sign(g, bias_z);
    float r = rand_f01(g);

    /* Mix straight, single-axis, and diagonal serve styles. */
    if (r < 0.28f)
    {
        *vy = 0.0f;
        *vz = 0.0f;
        return;
    }
    if (r < 0.66f)
    {
        if (absf(bias_y) >= absf(bias_z))
        {
            *vy = sy * rand_f(g, 0.42f * max_y, max_y);
            *vz = rand_f(g, -0.10f * max_z, 0.10f * max_z);
        }
        else
        {
            *vy = rand_f(g, -0.10f * max_y, 0.10f * max_y);
            *vz = sz * rand_f(g, 0.42f * max_z, max_z);
        }
        return;
    }

    *vy = sy * rand_f(g, 0.34f * max_y, 0.86f * max_y);
    *vz = sz * rand_f(g, 0.34f * max_z, 0.86f * max_z);
}

static float physics_ball_speed_mag(const pong_game_t *g)
{
    if (!g) return 0.0f;
    float vx = g->ball.vx;
    float vy = g->ball.vy;
    float vz = g->ball.vz;
    return sqrtf(vx * vx + vy * vy + vz * vz);
}

static void physics_speedpp_track_peak(pong_game_t *g)
{
    if (!g) return;
    float s = physics_ball_speed_mag(g);
    if (s > g->speedpp_peak_speed)
    {
        g->speedpp_peak_speed = s;
    }
}

static void physics_speedpp_on_score(pong_game_t *g, bool point_to_left)
{
    if (!g || !g->speedpp_enabled) return;

    uint16_t scored = point_to_left ? g->score.left : g->score.right;
    if (scored == 0u || (scored % 11u) != 0u) return;

    float base = g->speedpp_serve_speed_target;
    if (base <= 0.0f)
    {
        float peak = g->speedpp_peak_speed;
        if (peak > 0.0f)
        {
            base = peak;
        }
    }

    if (base > 0.0f)
    {
        g->speedpp_serve_speed_target = base * 1.05f;
    }

    if (g->speedpp_stage < 255u)
    {
        g->speedpp_stage++;
    }
}

static int physics_substeps(const pong_game_t *g, float dt)
{
    if (!g) return 1;
    if (dt <= 0.0f) return 1;

    float vmax = absf(g->ball.vx);
    float avy = absf(g->ball.vy);
    float avz = absf(g->ball.vz);
    if (avy > vmax) vmax = avy;
    if (avz > vmax) vmax = avz;

    /* Keep per-substep motion small to avoid tunneling through paddles at higher speeds. */
    float limit = g->ball.r * 0.5f;
    if (limit < 0.001f) limit = 0.001f;

    float ratio = (vmax * dt) / limit;
    int n = (int)ratio;
    if ((float)n < ratio) n++;
    if (n < 1) n = 1;
    if (n > 8) n = 8;
    return n;
}

static float physics_ball_speed_scale(const pong_game_t *g)
{
    uint8_t d = g ? g->difficulty : 2;
    if (d < 1) d = 1;
    if (d > 3) d = 3;
    switch (d)
    {
        case 1: return 1.95f;
        case 2: return 2.14f;
        default: return 2.32f;
    }
}

static void physics_get_tuning(const pong_game_t *g, float *serve_speed, float *speed_up, float *vlim)
{
    uint8_t d = g ? g->difficulty : 2;
    if (d < 1) d = 1;
    if (d > 3) d = 3;

    if (serve_speed) *serve_speed = 1.00f;
    if (speed_up) *speed_up = 1.025f;
    if (vlim) *vlim = 2.30f;

    switch (d)
    {
        case 1:
            if (serve_speed) *serve_speed = 0.82f;
            if (speed_up) *speed_up = 1.018f;
            if (vlim) *vlim = 1.90f;
            break;
        case 2:
            if (serve_speed) *serve_speed = 1.02f;
            if (speed_up) *speed_up = 1.025f;
            if (vlim) *vlim = 2.30f;
            break;
        default:
            if (serve_speed) *serve_speed = 1.18f;
            if (speed_up) *speed_up = 1.035f;
            if (vlim) *vlim = 2.80f;
            break;
    }

    /* Scale overall ball speeds per difficulty preset. */
    float s = physics_ball_speed_scale(g);
    if (serve_speed) *serve_speed *= s;
    if (vlim) *vlim *= s;

    /* Raise global max ball-speed cap by +50% across settings. */
    if (vlim) *vlim *= 1.5f;
}

void physics_reset_ball(pong_game_t *g, int serve_dir)
{
    if (!g) return;

    float dir = (serve_dir >= 0) ? 1.0f : -1.0f;

    g->ball.x = 0.5f;
    g->ball.y = rand_f(g, 0.30f, 0.70f);
    g->ball.z = rand_f(g, 0.20f, 0.80f);
    g->ball.r = 0.040f;

    float serve_speed = 1.0f;
    float vlim = 1.0f;
    physics_get_tuning(g, &serve_speed, NULL, &vlim);
    if (g->speedpp_enabled && g->speedpp_serve_speed_target > 0.0f)
    {
        float max_serve = vlim * 0.95f;
        if (max_serve < serve_speed) max_serve = serve_speed;
        serve_speed = clampf(g->speedpp_serve_speed_target, serve_speed, max_serve);
    }
    g->ball.vx = dir * serve_speed;
    physics_pick_ai_serve_lateral(g, dir, serve_speed, &g->ball.vy, &g->ball.vz);
    physics_speedpp_track_peak(g);
}

static bool physics_wall_bounce(float *p, float *v, float r)
{
    if (!p || !v) return false;
    bool bounced = false;
    if ((*p - r) < 0.0f)
    {
        *p = r;
        *v = absf(*v);
        bounced = true;
    }
    if ((*p + r) > 1.0f)
    {
        *p = 1.0f - r;
        *v = -absf(*v);
        bounced = true;
    }
    return bounced;
}

static float physics_axis_reflect(float p0, float v, float tau, float r)
{
    float lo = r;
    float hi = 1.0f - r;
    float p = p0 + v * tau;

    /* Apply a small number of reflections; with substeps, more than one is unlikely. */
    for (int i = 0; i < 4; i++)
    {
        if (p < lo)
        {
            p = lo + (lo - p);
            continue;
        }
        if (p > hi)
        {
            p = hi - (p - hi);
            continue;
        }
        break;
    }

    return clampf(p, lo, hi);
}

static bool physics_paddle_overlap_yz(const pong_paddle_t *p, float y, float z, float ball_r, float slop)
{
    if (!p) return false;

    /* Sphere vs paddle-rect overlap, evaluated in the paddle plane (y/z).
     * Use a circle-rectangle distance test to avoid "ghost" corner bounces
     * from the overly-permissive AABB expansion.
     */
    float hy = p->size_y * 0.5f;
    float hz = p->size_z * 0.5f;
    float rr = ball_r + slop;

    float dy = absf(y - p->y) - hy;
    float dz = absf(z - p->z) - hz;
    if (dy <= 0.0f && dz <= 0.0f) return true;

    if (dy < 0.0f) dy = 0.0f;
    if (dz < 0.0f) dz = 0.0f;
    return (dy * dy + dz * dz) <= (rr * rr);
}

static void physics_paddle_hit(pong_game_t *g, pong_paddle_t *p, bool left_side)
{
    if (!g || !p) return;
    if (g->sfx_paddle_hit_count < 255u) g->sfx_paddle_hit_count++;

    float speed_up = 1.02f;
    float vlim = 1.6f;
    physics_get_tuning(g, NULL, &speed_up, &vlim);

    float dy = (g->ball.y - p->y) / (p->size_y * 0.5f);
    float dz = (g->ball.z - p->z) / (p->size_z * 0.5f);
    dy = clampf(dy, -1.0f, 1.0f);
    dz = clampf(dz, -1.0f, 1.0f);

    g->last_hit_dy = dy;
    g->last_hit_dz = dz;
    ai_learning_on_paddle_hit(g, left_side);

    const float english = 0.55f;
    const float p_influence = 0.12f;
    const float edge_kick = 0.40f;
    const float corner_bonus = 0.72f;

    g->ball.vy += dy * english;
    g->ball.vz += dz * english;

    /* Corner/edge contacts get a slight outward kick so wall ricochets happen
     * more naturally. Y/Z walls plus both paddle planes act as six bounce walls.
     */
    {
        /* Start bending earlier near paddle outer halves, then strongly amplify
         * only when both axes are close to a true corner.
         */
        float edge_y = clampf((absf(dy) - 0.50f) / 0.50f, 0.0f, 1.0f);
        float edge_z = clampf((absf(dz) - 0.50f) / 0.50f, 0.0f, 1.0f);
        float corner = edge_y * edge_z;

        float kick_y = (edge_kick * edge_y + corner_bonus * corner) * signf(dy);
        float kick_z = (edge_kick * edge_z + corner_bonus * corner) * signf(dz);
        g->ball.vy += kick_y;
        g->ball.vz += kick_z;

        /* Near true corners, force a visibly sharp wall-seeking angle. */
        if (corner > 0.72f)
        {
            float min_side = vlim * 0.46f;
            if (absf(g->ball.vy) < min_side) g->ball.vy = min_side * signf(dy);
            if (absf(g->ball.vz) < min_side) g->ball.vz = min_side * signf(dz);
        }
    }

    /* Touch control can produce very large paddle velocities; clamp to keep hits stable. */
    float pvy = clampf(p->vy, -vlim, vlim);
    float pvz = clampf(p->vz, -vlim, vlim);
    g->ball.vy += pvy * p_influence;
    g->ball.vz += pvz * p_influence;

    g->ball.vx = (left_side ? absf(g->ball.vx) : -absf(g->ball.vx)) * speed_up;

    /* Clamp speeds. */
    g->ball.vx = clampf(g->ball.vx, -vlim, vlim);
    g->ball.vy = clampf(g->ball.vy, -vlim, vlim);
    g->ball.vz = clampf(g->ball.vz, -vlim, vlim);

    /* Ensure x motion does not stall. */
    if (absf(g->ball.vx) < 0.24f)
    {
        g->ball.vx = (g->ball.vx < 0.0f) ? -0.24f : 0.24f;
    }
}

static bool physics_step_sub(pong_game_t *g, float dt)
{
    if (!g) return false;
    if (dt <= 0.0f) return false;

    float prev_x = g->ball.x;
    float prev_y = g->ball.y;
    float prev_z = g->ball.z;
    float start_vy = g->ball.vy;
    float start_vz = g->ball.vz;

    g->ball.x += g->ball.vx * dt;
    g->ball.y += g->ball.vy * dt;
    g->ball.z += g->ball.vz * dt;

    /* Walls (y and z). */
    if (physics_wall_bounce(&g->ball.y, &g->ball.vy, g->ball.r))
    {
        if (g->sfx_wall_bounce_count < 255u) g->sfx_wall_bounce_count++;
    }
    if (physics_wall_bounce(&g->ball.z, &g->ball.vz, g->ball.r))
    {
        /* Keep Z-wall (depth) bounce silent to avoid non-obvious buzz-like SFX. */
    }

    /* Paddle collisions. */
    const float slop = g->ball.r * 0.02f;

    const float x_hit_l = g->paddle_l.x_plane + g->ball.r;
    if (g->ball.vx < 0.0f && prev_x > x_hit_l && g->ball.x <= x_hit_l)
    {
        float denom = (prev_x - g->ball.x);
        float t = (denom != 0.0f) ? ((prev_x - x_hit_l) / denom) : 0.0f;
        t = clampf(t, 0.0f, 1.0f);

        float tau = dt * t;
        float y_at = physics_axis_reflect(prev_y, start_vy, tau, g->ball.r);
        float z_at = physics_axis_reflect(prev_z, start_vz, tau, g->ball.r);

        bool hit = physics_paddle_overlap_yz(&g->paddle_l, y_at, z_at, g->ball.r, slop);
        if (hit)
        {
            g->ball.x = x_hit_l;
            g->ball.y = y_at;
            g->ball.z = z_at;
            physics_paddle_hit(g, &g->paddle_l, true);
        }
    }

    const float x_hit_r = g->paddle_r.x_plane - g->ball.r;
    if (g->ball.vx > 0.0f && prev_x < x_hit_r && g->ball.x >= x_hit_r)
    {
        float denom = (g->ball.x - prev_x);
        float t = (denom != 0.0f) ? ((x_hit_r - prev_x) / denom) : 0.0f;
        t = clampf(t, 0.0f, 1.0f);

        float tau = dt * t;
        float y_at = physics_axis_reflect(prev_y, start_vy, tau, g->ball.r);
        float z_at = physics_axis_reflect(prev_z, start_vz, tau, g->ball.r);

        bool hit = physics_paddle_overlap_yz(&g->paddle_r, y_at, z_at, g->ball.r, slop);
        if (hit)
        {
            g->ball.x = x_hit_r;
            g->ball.y = y_at;
            g->ball.z = z_at;
            physics_paddle_hit(g, &g->paddle_r, false);
        }
    }

    /* Scoring. */
    const float margin = 0.10f;
    if (g->ball.x < -margin)
    {
        g->score.right++;
        ai_learning_on_miss(g, true);
        physics_speedpp_on_score(g, false);
        /* Serve toward the player who just conceded (left). */
        physics_reset_ball(g, -1);
        return true;
    }
    else if (g->ball.x > 1.0f + margin)
    {
        g->score.left++;
        ai_learning_on_miss(g, false);
        physics_speedpp_on_score(g, true);
        /* Serve toward the player who just conceded (right). */
        physics_reset_ball(g, +1);
        return true;
    }

    return false;
}

void physics_step(pong_game_t *g, float dt)
{
    if (!g) return;

    physics_speedpp_track_peak(g);

    int n = physics_substeps(g, dt);
    float h = dt / (float)n;
    for (int i = 0; i < n; i++)
    {
        if (physics_step_sub(g, h))
        {
            return;
        }
    }
}
