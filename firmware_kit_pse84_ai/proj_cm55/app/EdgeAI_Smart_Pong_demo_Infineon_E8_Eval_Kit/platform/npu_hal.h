#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct
{
    bool init_ok;
    uint32_t invoke_count;
    uint32_t invoke_ok_count;
    uint32_t invoke_fail_count;
    uint32_t last_infer_us;
    uint32_t avg_infer_us;
} npu_hal_t;

typedef struct
{
    float y_hit;
    float z_hit;
    float t_hit;
} npu_pred_t;

typedef struct
{
    uint32_t invoke_count;
    uint32_t invoke_ok_count;
    uint32_t invoke_fail_count;
    uint32_t last_infer_us;
    uint32_t avg_infer_us;
} npu_telemetry_t;

bool npu_hal_init(npu_hal_t *s);
bool npu_hal_predict(npu_hal_t *s, const float features[16], npu_pred_t *out);
bool npu_hal_get_telemetry(const npu_hal_t *s, npu_telemetry_t *out);
