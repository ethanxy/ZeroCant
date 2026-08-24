#include "action_store.h"

#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "wear_levelling.h"

#include <stdbool.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "action_store";

#define STORE_BASE_PATH   "/act"
#define STORE_PART_LABEL  "storage"
#define STORE_MAGIC       0x31524341u /* 'ACR1' */
#define INDEX_PATH        STORE_BASE_PATH "/INDEX.BIN"

typedef struct {
    uint32_t magic;
    uint32_t seq;
    uint32_t duration_ms;
    uint16_t sample_hz;
    uint16_t sample_count;
    float pitch_origin;
    float yaw_origin;
} rec_file_hdr_t;

typedef struct {
    uint32_t magic;
    uint32_t next_slot;
    uint32_t save_seq;
    uint8_t used[ACTION_STORE_MAX_SLOTS];
    uint32_t seq[ACTION_STORE_MAX_SLOTS];
    uint32_t duration_ms[ACTION_STORE_MAX_SLOTS];
    uint16_t sample_count[ACTION_STORE_MAX_SLOTS];
    float pitch_origin[ACTION_STORE_MAX_SLOTS];
    float yaw_origin[ACTION_STORE_MAX_SLOTS];
} rec_index_t;

static wl_handle_t s_wl = WL_INVALID_HANDLE;
static bool s_ready = false;
static rec_index_t s_index;

static void rec_path(uint8_t slot, char *buf, size_t buflen)
{
    snprintf(buf, buflen, STORE_BASE_PATH "/REC%d.BIN", (int)slot);
}

static void index_reset(void)
{
    memset(&s_index, 0, sizeof(s_index));
    s_index.magic = STORE_MAGIC;
}

static esp_err_t index_write(void)
{
    FILE *f = fopen(INDEX_PATH, "wb");
    if (!f) {
        ESP_LOGE(TAG, "fopen index write failed");
        return ESP_FAIL;
    }
    size_t n = fwrite(&s_index, 1, sizeof(s_index), f);
    fclose(f);
    return (n == sizeof(s_index)) ? ESP_OK : ESP_FAIL;
}

static void index_load(void)
{
    FILE *f = fopen(INDEX_PATH, "rb");
    if (!f) {
        index_reset();
        (void)index_write();
        return;
    }
    rec_index_t tmp;
    size_t n = fread(&tmp, 1, sizeof(tmp), f);
    fclose(f);
    if (n != sizeof(tmp) || tmp.magic != STORE_MAGIC) {
        ESP_LOGW(TAG, "index invalid, resetting");
        index_reset();
        (void)index_write();
        return;
    }
    s_index = tmp;
    if (s_index.next_slot >= ACTION_STORE_MAX_SLOTS) {
        s_index.next_slot = 0;
    }
}

esp_err_t action_store_init(void)
{
    if (s_ready) {
        return ESP_OK;
    }

    esp_vfs_fat_mount_config_t mount_config = {
        .format_if_mount_failed = true,
        .max_files = 4,
        .allocation_unit_size = CONFIG_WL_SECTOR_SIZE,
        .disk_status_check_enable = false,
    };

    esp_err_t err = esp_vfs_fat_spiflash_mount_rw_wl(STORE_BASE_PATH, STORE_PART_LABEL,
                                                     &mount_config, &s_wl);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount failed: %s", esp_err_to_name(err));
        return err;
    }

    index_load();
    s_ready = true;
    ESP_LOGI(TAG, "mounted, next_slot=%u save_seq=%u",
             (unsigned)s_index.next_slot, (unsigned)s_index.save_seq);
    return ESP_OK;
}

bool action_store_ready(void)
{
    return s_ready;
}

int action_store_list(action_store_info_t *out, int max_out)
{
    if (!s_ready || !out || max_out <= 0) {
        return 0;
    }

    action_store_info_t tmp[ACTION_STORE_MAX_SLOTS];
    int n = 0;
    for (int i = 0; i < ACTION_STORE_MAX_SLOTS; i++) {
        if (!s_index.used[i]) {
            continue;
        }
        tmp[n].slot = (uint8_t)i;
        tmp[n].seq = s_index.seq[i];
        tmp[n].duration_ms = s_index.duration_ms[i];
        tmp[n].sample_count = s_index.sample_count[i];
        tmp[n].pitch_origin = s_index.pitch_origin[i];
        tmp[n].yaw_origin = s_index.yaw_origin[i];
        n++;
    }

    for (int i = 0; i < n - 1; i++) {
        for (int j = i + 1; j < n; j++) {
            if (tmp[j].seq > tmp[i].seq) {
                action_store_info_t swap = tmp[i];
                tmp[i] = tmp[j];
                tmp[j] = swap;
            }
        }
    }

    if (n > max_out) {
        n = max_out;
    }
    memcpy(out, tmp, (size_t)n * sizeof(action_store_info_t));
    return n;
}

esp_err_t action_store_save(const action_store_sample_t *samples, uint16_t count,
                            uint32_t duration_ms, float pitch_origin, float yaw_origin)
{
    if (!s_ready || !samples || count == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    if (count > ACTION_STORE_MAX_SAMPLES) {
        count = ACTION_STORE_MAX_SAMPLES;
    }

    uint8_t slot = (uint8_t)s_index.next_slot;
    char path[32];
    rec_path(slot, path, sizeof(path));

    rec_file_hdr_t hdr = {
        .magic = STORE_MAGIC,
        .seq = s_index.save_seq + 1,
        .duration_ms = duration_ms,
        .sample_hz = ACTION_STORE_SAMPLE_HZ,
        .sample_count = count,
        .pitch_origin = pitch_origin,
        .yaw_origin = yaw_origin,
    };

    FILE *f = fopen(path, "wb");
    if (!f) {
        ESP_LOGE(TAG, "fopen %s failed", path);
        return ESP_FAIL;
    }
    bool ok = fwrite(&hdr, 1, sizeof(hdr), f) == sizeof(hdr);
    if (ok) {
        size_t bytes = (size_t)count * sizeof(action_store_sample_t);
        ok = fwrite(samples, 1, bytes, f) == bytes;
    }
    fclose(f);
    if (!ok) {
        ESP_LOGE(TAG, "write %s failed", path);
        return ESP_FAIL;
    }

    s_index.save_seq++;
    s_index.used[slot] = 1;
    s_index.seq[slot] = s_index.save_seq;
    s_index.duration_ms[slot] = duration_ms;
    s_index.sample_count[slot] = count;
    s_index.pitch_origin[slot] = pitch_origin;
    s_index.yaw_origin[slot] = yaw_origin;
    s_index.next_slot = (s_index.next_slot + 1) % ACTION_STORE_MAX_SLOTS;

    esp_err_t err = index_write();
    ESP_LOGI(TAG, "saved slot %u seq %u count %u dur %ums",
             (unsigned)slot, (unsigned)s_index.seq[slot],
             (unsigned)count, (unsigned)duration_ms);
    return err;
}

esp_err_t action_store_load(uint8_t slot, action_store_sample_t *samples, uint16_t max_samples,
                            action_store_info_t *info)
{
    if (!s_ready || slot >= ACTION_STORE_MAX_SLOTS || !samples || max_samples == 0) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_index.used[slot]) {
        return ESP_ERR_NOT_FOUND;
    }

    char path[32];
    rec_path(slot, path, sizeof(path));
    FILE *f = fopen(path, "rb");
    if (!f) {
        ESP_LOGE(TAG, "fopen %s failed", path);
        return ESP_FAIL;
    }

    rec_file_hdr_t hdr;
    size_t n = fread(&hdr, 1, sizeof(hdr), f);
    if (n != sizeof(hdr) || hdr.magic != STORE_MAGIC || hdr.sample_count == 0) {
        fclose(f);
        return ESP_ERR_INVALID_CRC;
    }

    uint16_t count = hdr.sample_count;
    if (count > max_samples) {
        count = max_samples;
    }
    size_t bytes = (size_t)count * sizeof(action_store_sample_t);
    n = fread(samples, 1, bytes, f);
    fclose(f);
    if (n != bytes) {
        return ESP_FAIL;
    }

    if (info) {
        info->slot = slot;
        info->seq = hdr.seq;
        info->duration_ms = hdr.duration_ms;
        info->sample_count = count;
        info->pitch_origin = hdr.pitch_origin;
        info->yaw_origin = hdr.yaw_origin;
    }
    return ESP_OK;
}
