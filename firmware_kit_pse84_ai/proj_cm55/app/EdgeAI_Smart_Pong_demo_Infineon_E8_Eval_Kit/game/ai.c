#include "game/ai.h"

#include <string.h>

#include "fsl_flash.h"
#include "platform/time_hal.h"

static inline float clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static inline uint16_t clampu16(uint32_t v, uint16_t hi)
{
    if (v > (uint32_t)hi) return hi;
    return (uint16_t)v;
}

static inline float absf(float v)
{
    return (v < 0.0f) ? -v : v;
}

static inline float sqrtf_approx(float x)
{
    if (x <= 0.0f) return 0.0f;
    float r = x;
    /* Small fixed-iteration Newton step is sufficient for lead scaling. */
    for (int i = 0; i < 4; i++) r = 0.5f * (r + x / r);
    return r;
}

static inline float signf_nonzero(float v)
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
    uint32_t u = (g->rng >> 8) & 0x00FFFFFFu;
    return (float)u * (1.0f / 16777215.0f);
}

static float rand_f(pong_game_t *g, float lo, float hi)
{
    return lo + (hi - lo) * rand_f01(g);
}

#define EDGEAI_LEARN_STORE_MAGIC 0x4C524E31u /* "LRN1" */
#define EDGEAI_LEARN_STORE_VERSION 0x00000002u
#define EDGEAI_LEARN_FLASH_PROGRAM_BYTES 512u
#define EDGEAI_LEARN_DECAY_ALPHA 0.04f
#define EDGEAI_LEARN_EVAL_MIN_SAMPLES 10u
#define EDGEAI_LEARN_GOOD_MISS_RATIO_MAX 0.44f
#define EDGEAI_LEARN_BAD_MISS_RATIO_MIN 0.60f
#define EDGEAI_LEARN_BAD_STREAK_ROLLBACK 2u

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t crc32;
    ai_learn_profile_t left;
    ai_learn_profile_t right;
    uint32_t reserved[(EDGEAI_LEARN_FLASH_PROGRAM_BYTES - (3u * sizeof(uint32_t)) - (2u * sizeof(ai_learn_profile_t))) /
                      sizeof(uint32_t)];
} ai_learn_store_t;

static ai_learn_store_t s_learn_store = {0u};
static ai_learn_store_t s_last_good_store = {0u};
static bool s_learn_store_dirty = false;
static bool s_last_good_valid = false;
static bool s_flash_ready = false;
static bool s_flash_init_done = false;
static uint32_t s_flash_store_addr = 0u;
static uint32_t s_flash_sector_size = 0u;
static flash_config_t s_flash_cfg;
static uint8_t s_bad_sync_streak = 0u;

static uint32_t ai_store_checksum(const ai_learn_store_t *store)
{
    if (!store) return 0u;
    ai_learn_store_t copy = *store;
    copy.crc32 = 0u;

    const uint8_t *p = (const uint8_t *)&copy;
    uint32_t hash = 2166136261u;
    for (uint32_t i = 0u; i < (uint32_t)sizeof(copy); i++)
    {
        hash ^= p[i];
        hash *= 16777619u;
    }
    return hash;
}

static bool ai_flash_init(void)
{
    if (s_flash_init_done) return s_flash_ready;
    s_flash_init_done = true;

    if (FLASH_Init(&s_flash_cfg) != kStatus_FLASH_Success) return false;

    uint32_t pflash_base = 0u;
    uint32_t pflash_size = 0u;
    uint32_t sector_size = 0u;
    if (FLASH_GetProperty(&s_flash_cfg, kFLASH_PropertyPflashBlockBaseAddr, &pflash_base) != kStatus_FLASH_Success)
        return false;
    if (FLASH_GetProperty(&s_flash_cfg, kFLASH_PropertyPflashTotalSize, &pflash_size) != kStatus_FLASH_Success)
        return false;
    if (FLASH_GetProperty(&s_flash_cfg, kFLASH_PropertyPflashSectorSize, &sector_size) != kStatus_FLASH_Success)
        return false;
    if (sector_size < EDGEAI_LEARN_FLASH_PROGRAM_BYTES || pflash_size < sector_size) return false;

    s_flash_sector_size = sector_size;
    s_flash_store_addr = (pflash_base + pflash_size) - sector_size;
    s_flash_ready = true;
    return true;
}

static bool ai_flash_read_store(ai_learn_store_t *out_store)
{
    if (!out_store) return false;
    if (!ai_flash_init()) return false;

    const ai_learn_store_t *flash_store = (const ai_learn_store_t *)(uintptr_t)s_flash_store_addr;
    if (flash_store->magic != EDGEAI_LEARN_STORE_MAGIC) return false;
    if (flash_store->version != EDGEAI_LEARN_STORE_VERSION) return false;
    if (ai_store_checksum(flash_store) != flash_store->crc32) return false;

    *out_store = *flash_store;
    return true;
}

static bool ai_flash_write_store(const ai_learn_store_t *store)
{
    if (!store) return false;
    if (!ai_flash_init()) return false;

    ai_learn_store_t write_buf;
    memset(&write_buf, 0xFF, sizeof(write_buf));
    write_buf = *store;
    write_buf.crc32 = 0u;
    write_buf.crc32 = ai_store_checksum(&write_buf);

    if (FLASH_Erase(&s_flash_cfg, s_flash_store_addr, s_flash_sector_size, kFLASH_ApiEraseKey) != kStatus_FLASH_Success)
        return false;
    if (FLASH_Program(&s_flash_cfg, s_flash_store_addr, (uint8_t *)&write_buf, sizeof(write_buf)) !=
        kStatus_FLASH_Success)
        return false;

    const ai_learn_store_t *flash_store = (const ai_learn_store_t *)(uintptr_t)s_flash_store_addr;
    return (memcmp(flash_store, &write_buf, sizeof(write_buf)) == 0);
}

static void ai_flash_clear_store(void)
{
    if (!ai_flash_init()) return;
    (void)FLASH_Erase(&s_flash_cfg, s_flash_store_addr, s_flash_sector_size, kFLASH_ApiEraseKey);
}

static ai_learn_profile_t ai_profile_default(void)
{
    ai_learn_profile_t p;
    memset(&p, 0, sizeof(p));
    p.speed_scale = 1.06f;
    p.noise_scale = 0.92f;
    p.lead_scale = 1.05f;
    p.center_bias = 0.0f;
    p.corner_bias = 0.0f;
    p.hits = 0u;
    p.misses = 0u;
    p.last_style = 0u;
    return p;
}

static void ai_profile_clamp(ai_learn_profile_t *p)
{
    if (!p) return;
    p->speed_scale = clampf(p->speed_scale, 0.75f, 1.85f);
    p->noise_scale = clampf(p->noise_scale, 0.40f, 2.20f);
    p->lead_scale = clampf(p->lead_scale, 0.70f, 2.00f);
    p->center_bias = clampf(p->center_bias, -0.35f, 0.35f);
    p->corner_bias = clampf(p->corner_bias, 0.00f, 0.45f);
    if (p->last_style > 3u) p->last_style = 0u;
}

static void ai_profile_decay_toward_default(ai_learn_profile_t *p, float alpha)
{
    if (!p) return;
    ai_learn_profile_t d = ai_profile_default();

    alpha = clampf(alpha, 0.0f, 1.0f);
    p->speed_scale += (d.speed_scale - p->speed_scale) * alpha;
    p->noise_scale += (d.noise_scale - p->noise_scale) * alpha;
    p->lead_scale += (d.lead_scale - p->lead_scale) * alpha;
    p->center_bias += (d.center_bias - p->center_bias) * alpha;
    p->corner_bias += (d.corner_bias - p->corner_bias) * alpha;

    p->hits = (uint16_t)(((uint32_t)p->hits * 99u) / 100u);
    p->misses = (uint16_t)(((uint32_t)p->misses * 99u) / 100u);
    for (uint32_t i = 0u; i < 4u; i++)
    {
        p->style_trials[i] = (uint16_t)(((uint32_t)p->style_trials[i] * 98u) / 100u);
        p->style_value_q8[i] = (int16_t)((((int32_t)p->style_value_q8[i]) * 98) / 100);
    }
}

static uint32_t ai_profile_sample_count(const ai_learn_profile_t *p)
{
    if (!p) return 0u;
    return (uint32_t)p->hits + (uint32_t)p->misses;
}

static float ai_profile_miss_ratio(const ai_learn_profile_t *p)
{
    uint32_t n = ai_profile_sample_count(p);
    if (n == 0u) return 0.5f;
    return (float)p->misses / (float)n;
}

static bool ai_profile_is_bad(const ai_learn_profile_t *p)
{
    uint32_t n = ai_profile_sample_count(p);
    if (n < EDGEAI_LEARN_EVAL_MIN_SAMPLES) return false;
    return (ai_profile_miss_ratio(p) >= EDGEAI_LEARN_BAD_MISS_RATIO_MIN);
}

static bool ai_profile_is_good(const ai_learn_profile_t *p)
{
    uint32_t n = ai_profile_sample_count(p);
    if (n < EDGEAI_LEARN_EVAL_MIN_SAMPLES) return false;
    return (ai_profile_miss_ratio(p) <= EDGEAI_LEARN_GOOD_MISS_RATIO_MAX);
}

static uint8_t ai_style_select(pong_game_t *g, const ai_learn_profile_t *p)
{
    if (!g || !p) return 0u;

    uint32_t n_trials = (uint32_t)p->style_trials[0] + (uint32_t)p->style_trials[1] +
                        (uint32_t)p->style_trials[2] + (uint32_t)p->style_trials[3];
    float eps = (n_trials < 20u) ? 0.05f : 0.02f;

    /* Keep exploration low so early rallies stay stable. */
    if (rand_f01(g) < eps)
    {
        float r = rand_f01(g);
        uint8_t s = (uint8_t)(r * 4.0f);
        if (s > 3u) s = 3u;
        return s;
    }

    float best_score = -1000000.0f;
    uint8_t best_style = 0u;
    for (uint8_t s = 0u; s < 4u; s++)
    {
        float q = (float)p->style_value_q8[s] * (1.0f / 256.0f);
        float prior = 0.0f;
        switch (s)
        {
            case 0u: prior = p->center_bias; break;
            case 1u:
            case 2u: prior = 0.85f * p->corner_bias; break;
            default: prior = 1.00f * p->corner_bias; break;
        }
        float explore = (p->style_trials[s] == 0u) ? 0.45f : (0.12f / (float)p->style_trials[s]);
        float score = q + prior + explore;
        if (score > best_score)
        {
            best_score = score;
            best_style = s;
        }
    }

    return best_style;
}

static void ai_style_update(ai_learn_profile_t *p, uint8_t style, int32_t reward_q8)
{
    if (!p || style > 3u) return;
    if (p->style_trials[style] < 65535u) p->style_trials[style]++;

    int32_t q = (int32_t)p->style_value_q8[style];
    uint16_t n = p->style_trials[style];
    int32_t den = (n < 24u) ? (int32_t)n : 24;
    if (den < 1) den = 1;
    q += (reward_q8 - q) / den;

    if (q > 32767) q = 32767;
    if (q < -32768) q = -32768;
    p->style_value_q8[style] = (int16_t)q;
}

static void ai_style_apply(const pong_game_t *g,
                           const ai_learn_profile_t *p,
                           uint8_t style,
                           float confidence,
                           float *y_hit,
                           float *z_hit)
{
    if (!g || !p || !y_hit || !z_hit) return;

    confidence = clampf(confidence, 0.0f, 1.0f);
    float k = 0.45f + (0.55f * confidence);
    uint32_t n_hist = (uint32_t)p->hits + (uint32_t)p->misses;
    float maturity = clampf((float)n_hist * (1.0f / 24.0f), 0.0f, 1.0f);
    k *= (0.30f + 0.70f * maturity);

    float edge = 0.07f + (0.28f * p->corner_bias);
    float edge_y = edge + 0.08f * absf(g->ball.vy);
    float edge_z = edge + 0.08f * absf(g->ball.vz);

    if (style == 0u)
    {
        float hold = clampf(0.22f + 0.65f * p->center_bias, 0.00f, 0.70f) * k;
        *y_hit += (0.5f - *y_hit) * hold;
        *z_hit += (0.5f - *z_hit) * hold;
    }
    else if (style == 1u)
    {
        *y_hit += signf_nonzero(g->ball.y - 0.5f) * edge_y * k;
    }
    else if (style == 2u)
    {
        *z_hit += signf_nonzero(g->ball.z - 0.5f) * edge_z * k;
    }
    else
    {
        *y_hit += signf_nonzero(g->ball.y - 0.5f) * edge_y * k;
        *z_hit += signf_nonzero(g->ball.z - 0.5f) * edge_z * k;
    }
}

static ai_learn_profile_t *ai_profile_side(pong_game_t *g, bool right_side)
{
    if (!g) return NULL;
    return right_side ? &g->ai_profile_right : &g->ai_profile_left;
}

static const ai_learn_profile_t *ai_profile_side_const(const pong_game_t *g, bool right_side)
{
    if (!g) return NULL;
    return right_side ? &g->ai_profile_right : &g->ai_profile_left;
}

static bool ai_learning_side_selected(const pong_game_t *g, bool right_side)
{
    if (!g) return false;
    if (right_side)
    {
        if (!g->ai_right_active) return false;
        if (g->ai_learn_mode == kAiLearnModeAiAlgo) return false;
        return true;
    }

    if (!g->ai_left_active) return false;
    if (g->ai_learn_mode == kAiLearnModeAlgoAi) return false;
    return true;
}

static void ai_learning_commit_store(const pong_game_t *g)
{
    if (!g) return;
    s_learn_store.magic = EDGEAI_LEARN_STORE_MAGIC;
    s_learn_store.version = EDGEAI_LEARN_STORE_VERSION;
    s_learn_store.crc32 = 0u;
    s_learn_store.left = g->ai_profile_left;
    s_learn_store.right = g->ai_profile_right;
    s_learn_store.crc32 = ai_store_checksum(&s_learn_store);
    s_learn_store_dirty = true;
}

static void ai_learning_apply_store_to_game(pong_game_t *g, const ai_learn_store_t *store)
{
    if (!g || !store) return;
    g->ai_profile_left = store->left;
    g->ai_profile_right = store->right;
    ai_profile_clamp(&g->ai_profile_left);
    ai_profile_clamp(&g->ai_profile_right);
}

static void ai_learning_load_store(pong_game_t *g)
{
    if (!g || !g->persistent_learning) return;

    ai_learn_store_t flash_store;
    if (ai_flash_read_store(&flash_store))
    {
        s_learn_store = flash_store;
        s_learn_store_dirty = false;
        s_last_good_store = s_learn_store;
        s_last_good_valid = true;
        s_bad_sync_streak = 0u;
        ai_learning_apply_store_to_game(g, &s_learn_store);
        return;
    }

    if (s_learn_store.magic == EDGEAI_LEARN_STORE_MAGIC && s_learn_store.version == EDGEAI_LEARN_STORE_VERSION &&
        ai_store_checksum(&s_learn_store) == s_learn_store.crc32)
    {
        s_last_good_store = s_learn_store;
        s_last_good_valid = true;
        s_bad_sync_streak = 0u;
        ai_learning_apply_store_to_game(g, &s_learn_store);
    }
    else
    {
        s_learn_store.magic = EDGEAI_LEARN_STORE_MAGIC;
        s_learn_store.version = EDGEAI_LEARN_STORE_VERSION;
        s_learn_store.crc32 = 0u;
        s_learn_store.left = g->ai_profile_left;
        s_learn_store.right = g->ai_profile_right;
        s_learn_store.crc32 = ai_store_checksum(&s_learn_store);
        s_last_good_store = s_learn_store;
        s_last_good_valid = true;
        s_bad_sync_streak = 0u;
        s_learn_store_dirty = false;
    }
}

void ai_learning_reset_session(pong_game_t *g)
{
    if (!g) return;
    g->ai_profile_left = ai_profile_default();
    g->ai_profile_right = ai_profile_default();
    ai_learning_load_store(g);
}

void ai_learning_set_mode(pong_game_t *g, ai_learn_mode_t mode)
{
    if (!g) return;
    switch (mode)
    {
        case kAiLearnModeBoth:
        case kAiLearnModeAiAlgo:
        case kAiLearnModeAlgoAi:
            g->ai_learn_mode = mode;
            break;
        default:
            g->ai_learn_mode = kAiLearnModeBoth;
            break;
    }
}

void ai_learning_set_persistent(pong_game_t *g, bool enabled)
{
    if (!g) return;
    if (enabled)
    {
        g->persistent_learning = true;
        ai_learning_load_store(g);
        return;
    }

    /* Persistence OFF means no carry-over advantage:
     * clear current learned profiles and wipe stored snapshot.
     */
    g->persistent_learning = false;
    ai_learning_reset_session(g);
    memset(&s_learn_store, 0, sizeof(s_learn_store));
    memset(&s_last_good_store, 0, sizeof(s_last_good_store));
    s_learn_store_dirty = false;
    s_last_good_valid = false;
    s_bad_sync_streak = 0u;
    ai_flash_clear_store();
}

void ai_learning_sync_store(pong_game_t *g)
{
    if (!g) return;
    if (!s_learn_store_dirty) return;

    /* Apply light per-match decay to avoid stale-profile lock-in across long sessions. */
    ai_profile_decay_toward_default(&g->ai_profile_left, EDGEAI_LEARN_DECAY_ALPHA);
    ai_profile_decay_toward_default(&g->ai_profile_right, EDGEAI_LEARN_DECAY_ALPHA);
    ai_profile_clamp(&g->ai_profile_left);
    ai_profile_clamp(&g->ai_profile_right);
    ai_learning_commit_store(g);

    bool left_bad = ai_profile_is_bad(&g->ai_profile_left);
    bool right_bad = ai_profile_is_bad(&g->ai_profile_right);
    bool any_bad = (left_bad || right_bad);

    if (any_bad)
    {
        if (s_bad_sync_streak < 255u) s_bad_sync_streak++;
        if (s_last_good_valid && s_bad_sync_streak >= EDGEAI_LEARN_BAD_STREAK_ROLLBACK)
        {
            ai_learning_apply_store_to_game(g, &s_last_good_store);
            ai_learning_commit_store(g);
            if (g->persistent_learning && ai_flash_write_store(&s_learn_store))
            {
                s_learn_store_dirty = false;
            }
            else if (!g->persistent_learning)
            {
                s_learn_store_dirty = false;
            }
            s_bad_sync_streak = 0u;
        }
        return;
    }

    s_bad_sync_streak = 0u;

    bool left_good = ai_profile_is_good(&g->ai_profile_left);
    bool right_good = ai_profile_is_good(&g->ai_profile_right);
    if (!(left_good || right_good)) return;

    s_last_good_store = s_learn_store;
    s_last_good_valid = true;

    if (g->persistent_learning)
    {
        if (ai_flash_write_store(&s_learn_store))
        {
            s_learn_store_dirty = false;
        }
    }
    else
    {
        s_learn_store_dirty = false;
    }
}

void ai_learning_on_paddle_hit(pong_game_t *g, bool left_side)
{
    if (!g) return;
    bool right_side = !left_side;
    if (!ai_learning_side_selected(g, right_side)) return;

    ai_learn_profile_t *p = ai_profile_side(g, right_side);
    if (!p) return;

    p->hits = clampu16((uint32_t)p->hits + 1u, 65535u);

    /* Successful return: slightly cleaner and quicker tracking, with mild anticipation. */
    p->noise_scale *= 0.987f;
    p->speed_scale += 0.008f;
    p->lead_scale += 0.005f;

    {
        uint8_t style = (p->last_style > 3u) ? 0u : p->last_style;
        float ahy = absf(g->last_hit_dy);
        float ahz = absf(g->last_hit_dz);
        int32_t reward = 18 << 8;
        if (style == 0u)
        {
            reward += ((ahy + ahz) < 0.60f) ? (6 << 8) : (-4 << 8);
            p->center_bias += 0.012f;
            p->corner_bias -= 0.003f;
        }
        else if (style == 1u)
        {
            reward += (ahy > 0.55f) ? (8 << 8) : (-3 << 8);
            p->corner_bias += 0.010f;
            p->center_bias -= 0.003f;
        }
        else if (style == 2u)
        {
            reward += (ahz > 0.55f) ? (8 << 8) : (-3 << 8);
            p->corner_bias += 0.010f;
            p->center_bias -= 0.003f;
        }
        else
        {
            reward += ((ahy + ahz) > 1.05f) ? (10 << 8) : (-4 << 8);
            p->corner_bias += 0.012f;
            p->center_bias -= 0.004f;
        }
        ai_style_update(p, style, reward);
    }

    ai_profile_clamp(p);
    ai_learning_commit_store(g);
}

void ai_learning_on_miss(pong_game_t *g, bool left_side)
{
    if (!g) return;
    bool right_side = !left_side;
    if (!ai_learning_side_selected(g, right_side)) return;

    ai_learn_profile_t *p = ai_profile_side(g, right_side);
    if (!p) return;

    p->misses = clampu16((uint32_t)p->misses + 1u, 65535u);

    /* Missed return: make AI react faster and reduce wander so it recovers over the session. */
    p->noise_scale *= 0.94f;
    p->speed_scale += 0.045f;
    p->lead_scale += 0.030f;

    {
        uint8_t style = (p->last_style > 3u) ? 0u : p->last_style;
        int32_t penalty = -22 << 8;
        ai_style_update(p, style, penalty);
        if (style == 0u)
        {
            p->center_bias -= 0.014f;
            p->corner_bias += 0.004f;
        }
        else
        {
            p->corner_bias -= 0.012f;
            p->center_bias += 0.004f;
        }
    }

    ai_profile_clamp(p);
    ai_learning_commit_store(g);
}

static void ai_sim_wall(float *p, float *v, float r)
{
    if (!p || !v) return;
    if ((*p - r) < 0.0f)
    {
        *p = r;
        *v = absf(*v);
    }
    if ((*p + r) > 1.0f)
    {
        *p = 1.0f - r;
        *v = -absf(*v);
    }
}

static void ai_predict_right(const pong_game_t *g, float dt, float *out_y, float *out_z, float *out_t)
{
    if (!out_y || !out_z || !out_t)
        return;

    *out_y = 0.5f;
    *out_z = 0.5f;
    *out_t = 0.0f;
    if (!g) return;

    if (g->ball.vx <= 0.0f)
    {
        *out_y = 0.5f;
        *out_z = 0.5f;
        *out_t = 0.0f;
        return;
    }

    float x = g->ball.x;
    float y = g->ball.y;
    float z = g->ball.z;
    float vx = g->ball.vx;
    float vy = g->ball.vy;
    float vz = g->ball.vz;
    float r = g->ball.r;

    const float x_hit = g->paddle_r.x_plane - r;
    const int max_steps = 240; /* ~4s at 60 Hz */

    for (int i = 0; i < max_steps; i++)
    {
        x += vx * dt;
        y += vy * dt;
        z += vz * dt;
        ai_sim_wall(&y, &vy, r);
        ai_sim_wall(&z, &vz, r);
        if (x >= x_hit)
        {
            *out_y = clampf(y, 0.0f, 1.0f);
            *out_z = clampf(z, 0.0f, 1.0f);
            *out_t = (float)(i + 1) * dt;
            return;
        }
    }
}

static void ai_predict_left(const pong_game_t *g, float dt, float *out_y, float *out_z, float *out_t)
{
    if (!out_y || !out_z || !out_t)
        return;

    *out_y = 0.5f;
    *out_z = 0.5f;
    *out_t = 0.0f;
    if (!g) return;

    if (g->ball.vx >= 0.0f)
    {
        *out_y = 0.5f;
        *out_z = 0.5f;
        *out_t = 0.0f;
        return;
    }

    float x = g->ball.x;
    float y = g->ball.y;
    float z = g->ball.z;
    float vx = g->ball.vx;
    float vy = g->ball.vy;
    float vz = g->ball.vz;
    float r = g->ball.r;

    const float x_hit = g->paddle_l.x_plane + r;
    const int max_steps = 240; /* ~4s at 60 Hz */

    for (int i = 0; i < max_steps; i++)
    {
        x += vx * dt;
        y += vy * dt;
        z += vz * dt;
        ai_sim_wall(&y, &vy, r);
        ai_sim_wall(&z, &vz, r);
        if (x <= x_hit)
        {
            *out_y = clampf(y, 0.0f, 1.0f);
            *out_z = clampf(z, 0.0f, 1.0f);
            *out_t = (float)(i + 1) * dt;
            return;
        }
    }
}

void ai_init(pong_game_t *g)
{
    if (!g) return;
    (void)npu_hal_init(&g->npu);
    ai_learning_reset_session(g);
}

static void ai_build_features(const pong_game_t *g, float f[16])
{
    if (!g || !f) return;
    memset(f, 0, 16 * sizeof(f[0]));

    f[0] = g->ball.x;
    f[1] = g->ball.y;
    f[2] = g->ball.z;
    f[3] = g->ball.vx;
    f[4] = g->ball.vy;
    f[5] = g->ball.vz;

    f[6] = g->paddle_l.y;
    f[7] = g->paddle_l.z;
    f[8] = g->paddle_l.vy;
    f[9] = g->paddle_l.vz;

    f[10] = g->paddle_r.y;
    f[11] = g->paddle_r.z;

    int32_t sd = (int32_t)g->score.left - (int32_t)g->score.right;
    if (sd > 20) sd = 20;
    if (sd < -20) sd = -20;
    f[12] = (float)sd * (1.0f / 20.0f);

    f[13] = g->last_hit_dy;
    f[14] = g->last_hit_dz;
    f[15] = 1.0f;
}

static void ai_mirror_features_x(const float in[16], float out[16])
{
    if (!in || !out) return;

    /* Keep the NPU feature view always predicting the right paddle plane:
     * - mirror x and vx
     * - swap left/right paddle features
     * - flip score diff sign (left-right -> right-left)
     */
    memcpy(out, in, 16 * sizeof(out[0]));

    out[0] = 1.0f - in[0];   /* ball.x */
    out[3] = -in[3];         /* ball.vx */

    /* left paddle (6..9) <-> right paddle (10..11) */
    out[6] = in[10]; /* opp y */
    out[7] = in[11]; /* opp z */
    out[8] = 0.0f;
    out[9] = 0.0f;

    out[10] = in[6]; /* self y */
    out[11] = in[7]; /* self z */

    out[12] = -in[12];
}

static uint32_t ai_update_div(const pong_game_t *g, bool use_npu)
{
    uint8_t d = g ? g->difficulty : 2;
    if (d < 1) d = 1;
    if (d > 3) d = 3;

    if (!use_npu)
    {
        switch (d)
        {
            case 1: return 5u;
            case 2: return 3u;
            default: return 2u;
        }
    }

    /* In mixed SKILL modes, keep EdgeAI control cadence near ALGO cadence so the
     * selected AI side is not handicapped by slower NPU refresh.
     */
    if (g && (g->ai_learn_mode != kAiLearnModeBoth))
    {
        switch (d)
        {
            case 1: return 5u;
            case 2: return 3u;
            default: return 2u;
        }
    }

    /* NPU path: keep refresh aggressive for gameplay quality, then adapt to observed latency. */
    uint32_t div = 3u;
    switch (d)
    {
        case 1: div = 4u; break;
        case 2: div = 3u; break;
        default: div = 2u; break;
    }

    uint32_t avg_us = g ? g->npu.avg_infer_us : 0u;
    if (avg_us > 14000u) div += 2u;
    else if (avg_us > 9000u) div += 1u;
    else if (avg_us > 5000u) div += 1u;

    if (div < 2u) div = 2u;
    if (div > 8u) div = 8u;
    return div;
}

static float ai_noise(const pong_game_t *g, bool right_side)
{
    uint8_t d = g ? g->difficulty : 2;
    if (d < 1) d = 1;
    if (d > 3) d = 3;

    float n = 0.015f;
    switch (d)
    {
        case 1: n = 0.032f; break;
        case 2: n = 0.015f; break;
        default: n = 0.007f; break;
    }

    /* Show clear behavior difference when NPU path is disabled. */
    if (g && !g->ai_enabled)
    {
        n *= 1.7f;
    }

    if (g && ai_learning_side_selected(g, right_side))
    {
        const ai_learn_profile_t *p = ai_profile_side_const(g, right_side);
        if (p)
        {
            n *= p->noise_scale;
        }
        if (g->ai_learn_mode != kAiLearnModeBoth)
        {
            /* Mixed mode: keep a slight EdgeAI assist without overwhelming ALGO baseline. */
            n *= 0.98f;
        }
        if (g->ai_enabled)
        {
            /* NPU path should help, but stay closer to neutral for fair side-by-side comparison. */
            n *= 0.97f;
        }
    }

    n = clampf(n, 0.002f, 0.08f);
    return n;
}

static float ai_max_speed(const pong_game_t *g, bool right_side)
{
    uint8_t d = g ? g->difficulty : 2;
    if (d < 1) d = 1;
    if (d > 3) d = 3;
    float s = 1.48f;
    switch (d)
    {
        case 1: s = 1.22f; break;
        case 2: s = 1.48f; break;
        default: s = 1.78f; break;
    }

    if (g && !g->ai_enabled)
    {
        s *= 0.82f;
    }

    if (g && ai_learning_side_selected(g, right_side))
    {
        const ai_learn_profile_t *p = ai_profile_side_const(g, right_side);
        if (p)
        {
            s *= p->speed_scale;
        }
        if (g->ai_learn_mode != kAiLearnModeBoth)
        {
            s *= 1.00f;
        }
    }

    s = clampf(s, 0.70f, 2.60f);
    return s;
}

static float ai_lead_scale(const pong_game_t *g, bool right_side)
{
    if (!g) return 1.0f;
    if (!ai_learning_side_selected(g, right_side)) return 1.0f;
    const ai_learn_profile_t *p = ai_profile_side_const(g, right_side);
    if (!p) return 1.0f;
    return clampf(p->lead_scale, 0.70f, 2.00f);
}

static float ai_dynamic_lead_gain(const pong_game_t *g, bool side_edgeai, float t_hit, bool mixed_mode, float confidence)
{
    if (!g) return 1.0f;

    float vx = g->ball.vx;
    float vy = g->ball.vy;
    float vz = g->ball.vz;
    float vmag = sqrtf_approx((vx * vx) + (vy * vy) + (vz * vz));
    float speed_w = clampf((vmag - 1.05f) * (1.0f / 1.45f), 0.0f, 1.0f);

    float eta = clampf(t_hit, 0.0f, 0.90f);
    float eta_w = clampf(1.0f - (eta * (1.0f / 0.90f)), 0.0f, 1.0f);

    confidence = clampf(confidence, 0.0f, 1.0f);
    float conf_w = 0.70f + (0.30f * confidence);
    float gain = 1.0f + (0.22f * speed_w) + (0.24f * eta_w) + (0.22f * speed_w * eta_w * conf_w);

    /* Keep a small EdgeAI anticipation edge, reduced for fair mixed-mode comparison. */
    if (side_edgeai) gain += 0.01f;
    if (mixed_mode) gain += 0.00f;

    return clampf(gain, 1.0f, 1.65f);
}

static void ai_update_telemetry_window(pong_game_t *g)
{
    if (!g) return;

    if (g->ai_telemetry_start_cycles == 0u)
    {
        g->ai_telemetry_start_cycles = time_hal_cycles();
        return;
    }

    uint32_t elapsed_us = time_hal_elapsed_us(g->ai_telemetry_start_cycles);
    if (elapsed_us < 1000000u) return;

    uint32_t npu_hz = 0u;
    uint32_t fb_hz = 0u;

    if (elapsed_us > 0u)
    {
        npu_hz = (uint32_t)(((uint64_t)g->ai_npu_attempts_window * 1000000ull) / (uint64_t)elapsed_us);
        fb_hz = (uint32_t)(((uint64_t)g->ai_fallback_window * 1000000ull) / (uint64_t)elapsed_us);
    }

    g->ai_npu_rate_hz = clampu16(npu_hz, 999u);
    g->ai_fallback_rate_hz = clampu16(fb_hz, 999u);
    g->ai_npu_attempts_window = 0u;
    g->ai_fallback_window = 0u;
    g->ai_telemetry_start_cycles = time_hal_cycles();
}

static bool ai_easy_ball_lock(const pong_game_t *g, const pong_paddle_t *p, bool right_side, float t_hit)
{
    if (!g || !p) return false;

    float gap = 1.0f;
    if (right_side)
    {
        gap = p->x_plane - (g->ball.x + g->ball.r);
    }
    else
    {
        gap = (g->ball.x - g->ball.r) - p->x_plane;
    }

    /* Tight interception window: favor pure intercept over style/lead. */
    return (t_hit <= 0.24f) || (gap <= 0.12f);
}

static void ai_step_one(pong_game_t *g, float dt, pong_paddle_t *p, bool right_side)
{
    if (!g || !p) return;

    bool ball_toward = right_side ? (g->ball.vx > 0.0f) : (g->ball.vx < 0.0f);
    bool side_edgeai = ai_learning_side_selected(g, right_side);
    bool use_npu = g->ai_enabled && side_edgeai && ball_toward;

    /* Refresh AI target at a lower rate for difficulty and lower CPU. */
    uint32_t div = ai_update_div(g, use_npu);
    if (div == 0u) div = 1u;
    if ((g->frame % div) == 0u)
    {
        float y_hit = 0.5f;
        float z_hit = 0.5f;
        float t_hit = 0.0f;
        bool easy_lock = false;
        bool used_npu = false;
        float npu_confidence = 1.0f;

        if (ball_toward)
        {
            if (use_npu)
            {
                float feat[16];
                float feat2[16];
                ai_build_features(g, feat);

                const float *use_feat = feat;
                if (!right_side)
                {
                    ai_mirror_features_x(feat, feat2);
                    use_feat = feat2;
                }

                g->ai_npu_attempts_window++;
                npu_pred_t pred;
                used_npu = npu_hal_predict(&g->npu, use_feat, &pred);
                if (used_npu)
                {
                    y_hit = pred.y_hit;
                    z_hit = pred.z_hit;
                    t_hit = pred.t_hit;

                    /* In mixed SKILL modes, blend NPU with analytic prediction to
                     * keep EdgeAI competitive against fixed ALGO baseline.
                     */
                    if (g->ai_learn_mode != kAiLearnModeBoth)
                    {
                        float y_ref = 0.5f;
                        float z_ref = 0.5f;
                        float t_ref = 0.0f;
                        if (right_side)
                        {
                            ai_predict_right(g, dt, &y_ref, &z_ref, &t_ref);
                        }
                        else
                        {
                            ai_predict_left(g, dt, &y_ref, &z_ref, &t_ref);
                        }

                        /* Confidence gate: when NPU diverges from analytic physics, bias heavily
                         * toward the analytic path to preserve competitiveness.
                         */
                        float dy = absf(y_hit - y_ref);
                        float dz = absf(z_hit - z_ref);
                        float dtau = absf(t_hit - t_ref);
                        float disagreement = dy + dz + (0.60f * dtau);

                        float npu_w = 0.10f;
                        if (disagreement >= 0.22f)
                        {
                            npu_w = 0.0f;
                        }
                        else if (disagreement > 0.0f)
                        {
                            float trust = 1.0f - (disagreement / 0.22f);
                            npu_w *= clampf(trust, 0.0f, 1.0f);
                        }
                        npu_confidence = clampf(1.0f - (disagreement / 0.22f), 0.0f, 1.0f);

                        const float ref_w = 1.0f - npu_w;
                        y_hit = (ref_w * y_ref) + (npu_w * y_hit);
                        z_hit = (ref_w * z_ref) + (npu_w * z_hit);
                        t_hit = (ref_w * t_ref) + (npu_w * t_hit);
                    }
                }
            }

            if (!used_npu)
            {
                if (right_side)
                {
                    ai_predict_right(g, dt, &y_hit, &z_hit, &t_hit);
                }
                else
                {
                    ai_predict_left(g, dt, &y_hit, &z_hit, &t_hit);
                }
            }

            easy_lock = ai_easy_ball_lock(g, p, right_side, t_hit);
            if (easy_lock)
            {
                /* Close, simple returns should not lose to stylistic offsets. */
                if (right_side)
                {
                    ai_predict_right(g, dt, &y_hit, &z_hit, &t_hit);
                }
                else
                {
                    ai_predict_left(g, dt, &y_hit, &z_hit, &t_hit);
                }
            }

            /* Learned anticipation: shift target along projected travel based on side profile. */
            if (!easy_lock)
            {
                float lead = ai_lead_scale(g, right_side);
                float conf = used_npu ? npu_confidence : 0.85f;
                lead *= ai_dynamic_lead_gain(g, side_edgeai, t_hit, g->ai_learn_mode != kAiLearnModeBoth, conf);

                /* If AI side is trailing, commit more aggressively to recover rallies. */
                if (side_edgeai)
                {
                    int32_t diff = right_side ? ((int32_t)g->score.left - (int32_t)g->score.right)
                                              : ((int32_t)g->score.right - (int32_t)g->score.left);
                    if (diff >= 3)
                    {
                        float catchup = clampf(((float)diff - 1.0f) * 0.06f, 0.0f, 0.18f);
                        lead *= (1.0f + catchup);
                    }
                }
                float t_use = clampf(t_hit, 0.0f, 0.80f);
                float k = (lead - 1.0f) * 0.66f;
                y_hit += g->ball.vy * t_use * k;
                z_hit += g->ball.vz * t_use * k;

                /* Add a small velocity-weighted bias so AI commits earlier in the current travel direction. */
                {
                    float vxy = absf(g->ball.vy);
                    float vxz = absf(g->ball.vz);
                    float vsum = vxy + vxz;
                    if (vsum > 0.0001f)
                    {
                        float vx_w = clampf((absf(g->ball.vx) - 0.30f) * (1.0f / 1.60f), 0.0f, 1.0f);
                        float eta_w = clampf(1.0f - (t_use * (1.0f / 0.80f)), 0.0f, 1.0f);
                        float dir_gain = 0.038f + (0.042f * vx_w * eta_w);
                        y_hit += signf_nonzero(g->ball.vy) * dir_gain * (vxy / vsum);
                        z_hit += signf_nonzero(g->ball.vz) * dir_gain * (vxz / vsum);
                    }
                }
            }

            if (side_edgeai && !easy_lock)
            {
                ai_learn_profile_t *lp = ai_profile_side(g, right_side);
                if (lp)
                {
                    uint8_t style = ai_style_select(g, lp);
                    lp->last_style = style;
                    ai_style_apply(g, lp, style, used_npu ? npu_confidence : 0.80f, &y_hit, &z_hit);
                }
            }

            /* Add small noise to avoid perfect play. */
            float noise = ai_noise(g, right_side);
            if (easy_lock) noise *= 0.60f;
            if (side_edgeai && g->ai_enabled && ball_toward)
            {
                noise *= 0.95f;
            }

            /* In mixed AI/ALGO modes, if EdgeAI is comfortably ahead, add slight
             * variability so ALGO can still score and comparison stays informative. */
            if (side_edgeai && (g->ai_learn_mode != kAiLearnModeBoth))
            {
                int32_t lead_pts = right_side ? ((int32_t)g->score.right - (int32_t)g->score.left)
                                              : ((int32_t)g->score.left - (int32_t)g->score.right);
                if (lead_pts >= 4)
                {
                    float bonus = clampf(((float)lead_pts - 3.0f) * 0.08f, 0.0f, 0.30f);
                    noise *= (1.0f + bonus);
                }
            }
            y_hit += rand_f(g, -noise, noise);
            z_hit += rand_f(g, -noise, noise);
        }

        if (!used_npu)
        {
            g->ai_fallback_window++;
        }

        p->target_y = clampf(y_hit, 0.0f, 1.0f);
        p->target_z = clampf(z_hit, 0.0f, 1.0f);
    }

    /* Speed-limited movement. */
    float prev_y = p->y;
    float prev_z = p->z;

    float max_speed = ai_max_speed(g, right_side);
    if (ball_toward)
    {
        float chase_w = clampf((absf(g->ball.vx) - 0.40f) * (1.0f / 1.50f), 0.0f, 1.0f);
        max_speed *= (1.0f + (0.16f * chase_w));
        if (side_edgeai && g->ai_enabled)
        {
            max_speed *= 1.00f;
        }

        if (ai_easy_ball_lock(g, p, right_side, 0.25f))
        {
            max_speed *= 1.05f;
        }

        if (side_edgeai && (g->ai_learn_mode != kAiLearnModeBoth))
        {
            int32_t lead_pts = right_side ? ((int32_t)g->score.right - (int32_t)g->score.left)
                                          : ((int32_t)g->score.left - (int32_t)g->score.right);
            if (lead_pts >= 4)
            {
                float cut = clampf(((float)lead_pts - 3.0f) * 0.03f, 0.0f, 0.12f);
                max_speed *= (1.0f - cut);
            }
        }
    }
    float max_step = max_speed * dt;

    float dy = p->target_y - p->y;
    float dz = p->target_z - p->z;
    dy = clampf(dy, -max_step, max_step);
    dz = clampf(dz, -max_step, max_step);
    p->y += dy;
    p->z += dz;

    /* Clamp inside arena. */
    float hy = p->size_y * 0.5f;
    float hz = p->size_z * 0.5f;
    p->y = clampf(p->y, hy, 1.0f - hy);
    p->z = clampf(p->z, hz, 1.0f - hz);

    p->vy = (p->y - prev_y) / dt;
    p->vz = (p->z - prev_z) / dt;
}

void ai_step(pong_game_t *g, float dt, bool ai_left, bool ai_right)
{
    if (!g) return;

    if (ai_left)
    {
        ai_step_one(g, dt, &g->paddle_l, false);
    }
    if (ai_right)
    {
        ai_step_one(g, dt, &g->paddle_r, true);
    }

    /* Gentle decay toward baseline so long sessions stay stable. */
    if (ai_learning_side_selected(g, false))
    {
        ai_learn_profile_t *p = &g->ai_profile_left;
        p->speed_scale += (1.0f - p->speed_scale) * 0.0008f;
        p->noise_scale += (1.0f - p->noise_scale) * 0.0010f;
        p->lead_scale += (1.0f - p->lead_scale) * 0.0008f;
        ai_profile_clamp(p);
    }
    if (ai_learning_side_selected(g, true))
    {
        ai_learn_profile_t *p = &g->ai_profile_right;
        p->speed_scale += (1.0f - p->speed_scale) * 0.0008f;
        p->noise_scale += (1.0f - p->noise_scale) * 0.0010f;
        p->lead_scale += (1.0f - p->lead_scale) * 0.0008f;
        ai_profile_clamp(p);
    }

    ai_update_telemetry_window(g);
}
