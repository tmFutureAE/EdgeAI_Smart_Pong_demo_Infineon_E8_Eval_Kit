#include "platform/npu_hal.h"
#include "platform/time_hal.h"

#if defined(CONFIG_EDGEAI_USE_NPU) && (CONFIG_EDGEAI_USE_NPU)
bool npu_hal_tflm_neutron_init(npu_hal_t *s);
bool npu_hal_tflm_neutron_predict(npu_hal_t *s, const float features[16], npu_pred_t *out);
#endif

static void npu_hal_reset_stats(npu_hal_t *s)
{
    if (!s) return;
    s->invoke_count = 0u;
    s->invoke_ok_count = 0u;
    s->invoke_fail_count = 0u;
    s->last_infer_us = 0u;
    s->avg_infer_us = 0u;
}

static float npu_clampf(float v, float lo, float hi)
{
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

static float npu_absf(float v)
{
    return (v < 0.0f) ? -v : v;
}

static float npu_reflect_axis(float p0, float v, float tau)
{
    const float lo = 0.0f;
    const float hi = 1.0f;
    float p = p0 + v * tau;
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
    return npu_clampf(p, lo, hi);
}

bool npu_hal_init(npu_hal_t *s)
{
    if (!s) return false;
    s->init_ok = false;
    npu_hal_reset_stats(s);

#if defined(CONFIG_EDGEAI_USE_NPU) && (CONFIG_EDGEAI_USE_NPU)
    return npu_hal_tflm_neutron_init(s);
#else
    /* Keep NPU mode usable even when an external Neutron backend is not linked. */
    s->init_ok = true;
    return true;
#endif
}

bool npu_hal_predict(npu_hal_t *s, const float features[16], npu_pred_t *out)
{
    if (!s || !out) return false;
    if (!s->init_ok) return false;

    uint32_t start_cycles = time_hal_cycles();
    bool ok = false;
#if defined(CONFIG_EDGEAI_USE_NPU) && (CONFIG_EDGEAI_USE_NPU)
    ok = npu_hal_tflm_neutron_predict(s, features, out);
#else
    /* Lightweight surrogate predictor over the same feature vector shape.
     * Feature layout:
     * 0:x 1:y 2:z 3:vx 4:vy 5:vz ... (right-oriented view).
     */
    float x = features[0];
    float y = features[1];
    float z = features[2];
    float vx = features[3];
    float vy = features[4];
    float vz = features[5];
    float speed_x = npu_absf(vx);
    if (speed_x < 0.02f) speed_x = 0.02f;
    float t = (1.0f - x) / speed_x;
    t = npu_clampf(t, 0.0f, 2.5f);
    out->y_hit = npu_reflect_axis(y, vy, t);
    out->z_hit = npu_reflect_axis(z, vz, t);
    out->t_hit = t;
    ok = true;
#endif

    uint32_t elapsed_us = time_hal_elapsed_us(start_cycles);
    s->invoke_count++;
    s->last_infer_us = elapsed_us;
    if (s->invoke_count == 1u)
    {
        s->avg_infer_us = elapsed_us;
    }
    else
    {
        /* Low-cost moving average for runtime telemetry. */
        s->avg_infer_us = (s->avg_infer_us * 7u + elapsed_us) / 8u;
    }

    if (ok) s->invoke_ok_count++;
    else s->invoke_fail_count++;

    return ok;
}

bool npu_hal_get_telemetry(const npu_hal_t *s, npu_telemetry_t *out)
{
    if (!s || !out) return false;

    out->invoke_count = s->invoke_count;
    out->invoke_ok_count = s->invoke_ok_count;
    out->invoke_fail_count = s->invoke_fail_count;
    out->last_infer_us = s->last_infer_us;
    out->avg_infer_us = s->avg_infer_us;
    return true;
}
