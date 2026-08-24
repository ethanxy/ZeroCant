#ifndef ACTION_STORE_H
#define ACTION_STORE_H

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ACTION_STORE_MAX_SLOTS     10
#define ACTION_STORE_SAMPLE_HZ     40
#define ACTION_STORE_MAX_SECONDS   60
#define ACTION_STORE_MAX_SAMPLES   (ACTION_STORE_SAMPLE_HZ * ACTION_STORE_MAX_SECONDS)

typedef struct {
    float pitch;
    float yaw;
} action_store_sample_t;

typedef struct {
    uint8_t slot;
    uint32_t seq;
    uint32_t duration_ms;
    uint16_t sample_count;
    float pitch_origin;
    float yaw_origin;
} action_store_info_t;

esp_err_t action_store_init(void);
bool action_store_ready(void);

/** Newest first. Returns number of used slots written to out[]. */
int action_store_list(action_store_info_t *out, int max_out);

esp_err_t action_store_save(const action_store_sample_t *samples, uint16_t count,
                            uint32_t duration_ms, float pitch_origin, float yaw_origin);

esp_err_t action_store_load(uint8_t slot, action_store_sample_t *samples, uint16_t max_samples,
                            action_store_info_t *info);

#ifdef __cplusplus
}
#endif

#endif /* ACTION_STORE_H */
