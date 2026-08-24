#include "action_display.h"
#include "action_store.h"
#include "angle_calc.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "memory_diag.h"
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "action_display";

/* Outer ring default ±4°. UI steps this by ±1°. */
#define ANGLE_RANGE_DEFAULT     4.0f
#define ANGLE_RANGE_STEP        1.0f
#define ANGLE_RANGE_MIN         3.0f
#define ANGLE_RANGE_MAX         45.0f
#define TRAIL_CAP               200
#define TRAIL_DEFAULT           20
#define TRAIL_STEP              10
#define TRAIL_MIN               10
#define TRAIL_LIMIT_MAX         200
#define DIAMOND_SIZE            10
#define RING_COUNT              5
#define BOTTOM_LABEL_HEIGHT     156
#define PLAY_TIME_Y             8

static lv_obj_t *action_canvas = NULL;
static lv_obj_t *zero_btn = NULL;
static lv_obj_t *clear_btn = NULL;
static lv_obj_t *range_minus_btn = NULL;
static lv_obj_t *range_plus_btn = NULL;
static lv_obj_t *range_label = NULL;
static lv_obj_t *trail_minus_btn = NULL;
static lv_obj_t *trail_plus_btn = NULL;
static lv_obj_t *trail_len_label = NULL;
static lv_obj_t *pitch_label = NULL;
static lv_obj_t *yaw_label = NULL;
static lv_obj_t *rec_btn = NULL;
static lv_obj_t *rec_dot = NULL;
static lv_obj_t *rec_time_label = NULL;
static lv_obj_t *list_btn = NULL;
static lv_obj_t *play_label = NULL;
static lv_obj_t *list_overlay = NULL;
static lv_obj_t *list_widget = NULL;
static int disp_width = 240;
static int disp_height = 240;
static int target_cx = 120;
static int target_cy = 120;
static int target_radius = 110;

static lv_color_t *cbuf = NULL;
static lv_color_t *target_bg_buf = NULL;
static bool target_cached = false;

typedef struct {
    float pitch;
    float yaw;
} trail_sample_t;

static trail_sample_t trail[TRAIL_CAP];
static uint16_t trail_len = 0;
static uint16_t trail_head = 0; /* next write index when wrapping */
static bool trail_wrapped = false;

static float max_angle_deg = ANGLE_RANGE_DEFAULT;
static uint16_t trail_limit = TRAIL_DEFAULT;

static int last_px = -1;
static int last_py = -1;
static float s_live_pitch = 0.0f;
static float s_live_yaw = 0.0f;
static float pitch_origin = 0.0f;
static float yaw_origin = 0.0f;
static bool pitch_origin_set = false;
static bool yaw_origin_set = false;
static float s_last_label_pitch = 9999.0f;
static float s_last_label_yaw = 9999.0f;
static bool first_update = true;
static bool trail_dirty = true;

static bool s_recording = false;
static bool s_playing = false;
static uint16_t s_rec_count = 0;
static uint16_t s_play_index = 0;
static uint16_t s_play_count = 0;
static int64_t s_rec_start_us = 0;
static int64_t s_play_start_us = 0;
static float s_play_pitch_origin = 0.0f;
static float s_play_yaw_origin = 0.0f;
static float s_saved_pitch_origin = 0.0f;
static float s_saved_yaw_origin = 0.0f;
static bool s_saved_pitch_origin_set = false;
static bool s_saved_yaw_origin_set = false;
static action_store_sample_t s_rec_buf[ACTION_STORE_MAX_SAMPLES];

static void action_display_clear_trail_internal(void)
{
    trail_len = 0;
    trail_head = 0;
    trail_wrapped = false;
    last_px = -1;
    last_py = -1;
    first_update = true;
    trail_dirty = true;
}

static inline void set_pixel(lv_color_t *buf, int x, int y, lv_color_t color)
{
    if (!buf) {
        return;
    }
    if (x >= 0 && x < disp_width && y >= 0 && y < disp_height) {
        buf[y * disp_width + x] = color;
    }
}

static void draw_manual_line(lv_color_t *buf, int x0, int y0, int x1, int y1,
                             lv_color_t color, int thickness)
{
    if (!buf) {
        return;
    }

    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int x = x0;
    int y = y0;
    int half = thickness / 2;

    while (true) {
        for (int ox = -half; ox <= half; ox++) {
            for (int oy = -half; oy <= half; oy++) {
                set_pixel(buf, x + ox, y + oy, color);
            }
        }

        if (x == x1 && y == y1) {
            break;
        }

        int e2 = 2 * err;
        if (e2 > -dy) {
            err -= dy;
            x += sx;
        }
        if (e2 < dx) {
            err += dx;
            y += sy;
        }
    }
}

static void draw_circle_outline(lv_color_t *buf, int cx, int cy, int r, lv_color_t color)
{
    int x = r;
    int y = 0;
    int err = 0;

    while (x >= y) {
        set_pixel(buf, cx + x, cy + y, color);
        set_pixel(buf, cx + y, cy + x, color);
        set_pixel(buf, cx - y, cy + x, color);
        set_pixel(buf, cx - x, cy + y, color);
        set_pixel(buf, cx - x, cy - y, color);
        set_pixel(buf, cx - y, cy - x, color);
        set_pixel(buf, cx + y, cy - x, color);
        set_pixel(buf, cx + x, cy - y, color);

        y++;
        if (err <= 0) {
            err += 2 * y + 1;
        }
        if (err > 0) {
            x--;
            err -= 2 * x + 1;
        }
    }
}

static void draw_filled_circle(lv_color_t *buf, int cx, int cy, int r, lv_color_t color)
{
    for (int y = -r; y <= r; y++) {
        int span = (int)sqrtf((float)(r * r - y * y));
        for (int x = -span; x <= span; x++) {
            set_pixel(buf, cx + x, cy + y, color);
        }
    }
}

static void draw_diamond(lv_color_t *buf, int cx, int cy, int size, lv_color_t fill, lv_color_t edge)
{
    /* Filled diamond: |dx| + |dy| <= size */
    for (int y = -size; y <= size; y++) {
        int span = size - (y < 0 ? -y : y);
        for (int x = -span; x <= span; x++) {
            int adx = x < 0 ? -x : x;
            int ady = y < 0 ? -y : y;
            lv_color_t c = (adx + ady >= size - 1) ? edge : fill;
            set_pixel(buf, cx + x, cy + y, c);
        }
    }
}

static void draw_target_background(lv_color_t *buf)
{
    if (!buf) {
        return;
    }

    size_t buf_size = sizeof(lv_color_t) * (size_t)disp_width * (size_t)disp_height;
    memset(buf, 0, buf_size);

    lv_color_t paper = lv_color_hex(0xEDE6D6);
    lv_color_t ring = lv_color_hex(0x1A1A1A);
    lv_color_t cross = lv_color_hex(0x888070);
    lv_color_t bull = lv_color_hex(0x8B1E1E);

    draw_filled_circle(buf, target_cx, target_cy, target_radius, paper);
    draw_circle_outline(buf, target_cx, target_cy, target_radius, ring);
    draw_circle_outline(buf, target_cx, target_cy, target_radius - 1, ring);

    draw_manual_line(buf, target_cx - target_radius + 2, target_cy,
                     target_cx + target_radius - 2, target_cy, cross, 1);
    draw_manual_line(buf, target_cx, target_cy - target_radius + 2,
                     target_cx, target_cy + target_radius - 2, cross, 1);

    for (int i = RING_COUNT - 1; i >= 1; i--) {
        int r = (target_radius * i) / RING_COUNT;
        draw_circle_outline(buf, target_cx, target_cy, r, ring);
        if (r > 1) {
            draw_circle_outline(buf, target_cx, target_cy, r - 1, ring);
        }
    }

    draw_filled_circle(buf, target_cx, target_cy, 6, bull);
}

static float wrap_deg180(float a)
{
    while (a > 180.0f) {
        a -= 360.0f;
    }
    while (a < -180.0f) {
        a += 360.0f;
    }
    return a;
}

static void angles_to_point(float pitch, float yaw, int *px, int *py)
{
    float span = max_angle_deg;
    if (span < ANGLE_RANGE_MIN) {
        span = ANGLE_RANGE_MIN;
    }
    float rel_pitch = pitch - pitch_origin;
    float rel_yaw = wrap_deg180(yaw - yaw_origin);
    float nx = rel_yaw / span;              /* +yaw = diamond right */
    float ny = rel_pitch / span;            /* +pitch = diamond down */
    float mag2 = nx * nx + ny * ny;
    if (mag2 > 1.0f) {
        float mag = sqrtf(mag2);
        nx /= mag;
        ny /= mag;
    }

    int usable = target_radius - DIAMOND_SIZE - 2;
    if (usable < 8) {
        usable = 8;
    }

    *px = target_cx + (int)lroundf(nx * (float)usable);
    *py = target_cy + (int)lroundf(ny * (float)usable);

    if (*px < 0) {
        *px = 0;
    }
    if (*px >= disp_width) {
        *px = disp_width - 1;
    }
    if (*py < 0) {
        *py = 0;
    }
    if (*py >= disp_height) {
        *py = disp_height - 1;
    }
}

static uint16_t trail_count(void)
{
    return trail_wrapped ? TRAIL_CAP : trail_len;
}

static uint16_t trail_visible_count(void)
{
    uint16_t stored = trail_count();
    return (stored > trail_limit) ? trail_limit : stored;
}

static trail_sample_t trail_at(uint16_t chronological_index)
{
    uint16_t start = trail_wrapped ? trail_head : 0;
    return trail[(start + chronological_index) % TRAIL_CAP];
}

static void trail_push(float pitch, float yaw)
{
    trail_sample_t sample = { .pitch = pitch, .yaw = yaw };
    if (trail_wrapped || trail_len >= TRAIL_CAP) {
        trail[trail_head] = sample;
        trail_head = (uint16_t)((trail_head + 1) % TRAIL_CAP);
        trail_wrapped = true;
        trail_len = TRAIL_CAP;
    } else {
        trail[trail_len++] = sample;
    }
    trail_dirty = true;
}

static lv_color_t trail_color(uint16_t chronological_index, uint16_t count)
{
    if (count <= 1) {
        return lv_color_hex(0x00FF00);
    }
    /* Oldest: dark green, newest: bright green */
    float t = (float)chronological_index / (float)(count - 1);
    uint8_t g = (uint8_t)(90.0f + t * 165.0f);
    uint8_t rb = (uint8_t)(20.0f + t * 20.0f);
    return lv_color_make(rb, g, rb);
}

static void restore_target_background(void)
{
    size_t expected = sizeof(lv_color_t) * (size_t)disp_width * (size_t)disp_height;
    if (target_cached && target_bg_buf && cbuf) {
        size_t cbuf_size = heap_caps_get_allocated_size(cbuf);
        size_t bg_size = heap_caps_get_allocated_size(target_bg_buf);
        size_t copy = expected;
        if (copy > cbuf_size) {
            copy = cbuf_size;
        }
        if (copy > bg_size) {
            copy = bg_size;
        }
        memcpy(cbuf, target_bg_buf, copy);
        lv_canvas_set_buffer(action_canvas, cbuf, disp_width, disp_height, LV_IMG_CF_TRUE_COLOR);
    } else {
        lv_canvas_fill_bg(action_canvas, lv_color_black(), LV_OPA_COVER);
        draw_target_background(cbuf);
    }
}

static void draw_trail_and_marker(int px, int py)
{
    uint16_t stored = trail_count();
    uint16_t count = trail_visible_count();
    uint16_t skip = (stored > count) ? (uint16_t)(stored - count) : 0;
    if (count >= 2) {
        int prev_x = 0;
        int prev_y = 0;
        for (uint16_t i = 0; i < count; i++) {
            trail_sample_t s = trail_at((uint16_t)(skip + i));
            int x;
            int y;
            angles_to_point(s.pitch, s.yaw, &x, &y);
            if (i > 0) {
                int thickness = (i > count - 8) ? 3 : 2;
                draw_manual_line(cbuf, prev_x, prev_y, x, y, trail_color(i, count), thickness);
            }
            prev_x = x;
            prev_y = y;
        }
    }

    lv_color_t fill = lv_color_hex(0x00FF00);
    lv_color_t edge = lv_color_hex(0x006600);
    draw_diamond(cbuf, px, py, DIAMOND_SIZE, fill, edge);
}

static lv_obj_t *action_make_btn(lv_obj_t *parent, const char *text,
                                 lv_event_cb_t cb, lv_align_t align, lv_coord_t x_ofs)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 124, 52);
    lv_obj_align(btn, align, x_ofs, -12);
    lv_obj_set_ext_click_area(btn, 16);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x404040), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x606060), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    return btn;
}

static void action_redraw_scene(void)
{
    if (!action_canvas || !cbuf || !lv_obj_is_valid(action_canvas)) {
        return;
    }
    int px;
    int py;
    angles_to_point(s_live_pitch, s_live_yaw, &px, &py);
    last_px = px;
    last_py = py;
    restore_target_background();
    draw_trail_and_marker(px, py);
    lv_obj_invalidate(action_canvas);
}

static void action_range_refresh_label(void)
{
    if (range_label && lv_obj_is_valid(range_label)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%d°", (int)lroundf(max_angle_deg));
        lv_label_set_text(range_label, buf);
    }

    if (range_minus_btn && lv_obj_is_valid(range_minus_btn)) {
        if (max_angle_deg <= ANGLE_RANGE_MIN + 0.01f) {
            lv_obj_add_state(range_minus_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(range_minus_btn, LV_STATE_DISABLED);
        }
    }
    if (range_plus_btn && lv_obj_is_valid(range_plus_btn)) {
        if (max_angle_deg >= ANGLE_RANGE_MAX - 0.01f) {
            lv_obj_add_state(range_plus_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(range_plus_btn, LV_STATE_DISABLED);
        }
    }
}

static void action_range_adjust(float delta)
{
    float next = max_angle_deg + delta;
    if (next < ANGLE_RANGE_MIN) {
        next = ANGLE_RANGE_MIN;
    }
    if (next > ANGLE_RANGE_MAX) {
        next = ANGLE_RANGE_MAX;
    }
    if (fabsf(next - max_angle_deg) < 0.01f) {
        return;
    }
    max_angle_deg = next;
    action_range_refresh_label();
    action_redraw_scene();
}

static void action_range_minus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    action_range_adjust(-ANGLE_RANGE_STEP);
}

static void action_range_plus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    action_range_adjust(ANGLE_RANGE_STEP);
}

static lv_obj_t *action_make_range_btn(lv_obj_t *parent, const char *text,
                                       lv_event_cb_t cb, lv_coord_t x_ofs, lv_coord_t y_ofs)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 40, 36);
    lv_obj_align(btn, LV_ALIGN_BOTTOM_MID, x_ofs, y_ofs);
    lv_obj_set_ext_click_area(btn, 12);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x404040), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x606060), LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A2A2A), LV_STATE_DISABLED);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *label = lv_label_create(btn);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_white(), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_obj_center(label);
    return btn;
}

static void action_trail_refresh_label(void)
{
    if (trail_len_label && lv_obj_is_valid(trail_len_label)) {
        char buf[16];
        snprintf(buf, sizeof(buf), "%u", (unsigned)trail_limit);
        lv_label_set_text(trail_len_label, buf);
    }
    if (trail_minus_btn && lv_obj_is_valid(trail_minus_btn)) {
        if (trail_limit <= TRAIL_MIN) {
            lv_obj_add_state(trail_minus_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(trail_minus_btn, LV_STATE_DISABLED);
        }
    }
    if (trail_plus_btn && lv_obj_is_valid(trail_plus_btn)) {
        if (trail_limit >= TRAIL_LIMIT_MAX) {
            lv_obj_add_state(trail_plus_btn, LV_STATE_DISABLED);
        } else {
            lv_obj_clear_state(trail_plus_btn, LV_STATE_DISABLED);
        }
    }
}

static void action_trail_adjust(int delta)
{
    int next = (int)trail_limit + delta;
    if (next < TRAIL_MIN) {
        next = TRAIL_MIN;
    }
    if (next > TRAIL_LIMIT_MAX) {
        next = TRAIL_LIMIT_MAX;
    }
    if (next == (int)trail_limit) {
        return;
    }
    trail_limit = (uint16_t)next;
    action_trail_refresh_label();
    action_redraw_scene();
}

static void action_trail_minus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    action_trail_adjust(-(int)TRAIL_STEP);
}

static void action_trail_plus_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    action_trail_adjust((int)TRAIL_STEP);
}

static void action_zero_btn_refresh_style(void)
{
    if (!zero_btn || !lv_obj_is_valid(zero_btn)) {
        return;
    }
    if (pitch_origin_set) {
        lv_obj_set_style_border_color(zero_btn, lv_color_hex(0x00FF00), 0);
        lv_obj_set_style_border_width(zero_btn, 2, 0);
    } else {
        lv_obj_set_style_border_color(zero_btn, lv_color_white(), 0);
        lv_obj_set_style_border_width(zero_btn, 1, 0);
    }
}

static void action_display_redraw_marker(int px, int py)
{
    if (!action_canvas || !cbuf || !lv_obj_is_valid(action_canvas)) {
        return;
    }
    restore_target_background();
    draw_diamond(cbuf, px, py, DIAMOND_SIZE,
                 lv_color_hex(0x00FF00), lv_color_hex(0x006600));
    lv_obj_invalidate(action_canvas);
}

static void action_apply_clear_or_zero(bool set_aim_origin)
{
    if (set_aim_origin) {
        angle_calc_capture_yaw_bias();
        pitch_origin = s_live_pitch;
        yaw_origin = s_live_yaw;
        pitch_origin_set = true;
        yaw_origin_set = true;
        action_zero_btn_refresh_style();
        s_last_label_pitch = 9999.0f;
        s_last_label_yaw = 9999.0f;
    }

    action_display_clear_trail_internal();
    int px;
    int py;
    angles_to_point(s_live_pitch, s_live_yaw, &px, &py);
    last_px = px;
    last_py = py;
    action_display_redraw_marker(px, py);
}

static void action_clear_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    action_apply_clear_or_zero(false);
}

static void action_zero_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_playing || s_recording) {
        return;
    }
    action_apply_clear_or_zero(true);
}

static void action_format_mmss(uint32_t ms, char *buf, size_t buflen)
{
    uint32_t sec = ms / 1000u;
    snprintf(buf, buflen, "%u:%02u", (unsigned)(sec / 60u), (unsigned)(sec % 60u));
}

static void action_rec_refresh_style(void)
{
    if (!rec_btn || !lv_obj_is_valid(rec_btn) || !rec_dot || !lv_obj_is_valid(rec_dot)) {
        return;
    }
    if (s_recording) {
        lv_obj_set_size(rec_dot, 16, 16);
        lv_obj_set_style_radius(rec_dot, 3, 0);
        lv_obj_set_style_bg_color(rec_dot, lv_color_hex(0xFF2222), 0);
        lv_obj_set_style_border_color(rec_btn, lv_color_hex(0xFF4444), 0);
        lv_obj_set_style_border_width(rec_btn, 2, 0);
        if (rec_time_label && lv_obj_is_valid(rec_time_label)) {
            lv_obj_clear_flag(rec_time_label, LV_OBJ_FLAG_HIDDEN);
        }
    } else {
        lv_obj_set_size(rec_dot, 18, 18);
        lv_obj_set_style_radius(rec_dot, 9, 0);
        lv_obj_set_style_bg_color(rec_dot, lv_color_hex(0xCC0000), 0);
        lv_obj_set_style_border_color(rec_btn, lv_color_white(), 0);
        lv_obj_set_style_border_width(rec_btn, 1, 0);
        if (rec_time_label && lv_obj_is_valid(rec_time_label) && !s_playing) {
            lv_obj_add_flag(rec_time_label, LV_OBJ_FLAG_HIDDEN);
        }
    }
}

static void action_play_refresh_label(uint32_t now_ms, uint32_t tot_ms)
{
    if (!play_label || !lv_obj_is_valid(play_label)) {
        return;
    }
    char buf[24];
    char now_s[8];
    char tot_s[8];
    action_format_mmss(now_ms, now_s, sizeof(now_s));
    action_format_mmss(tot_ms, tot_s, sizeof(tot_s));
    snprintf(buf, sizeof(buf), "PLAY %s/%s", now_s, tot_s);
    const char *cur = lv_label_get_text(play_label);
    if (!cur || strcmp(cur, buf) != 0) {
        lv_label_set_text(play_label, buf);
    }
    lv_obj_align(play_label, LV_ALIGN_TOP_MID, 0, PLAY_TIME_Y);
    lv_obj_move_foreground(play_label);
    lv_obj_clear_flag(play_label, LV_OBJ_FLAG_HIDDEN);
}

static void action_play_restore_origin(void)
{
    pitch_origin = s_saved_pitch_origin;
    yaw_origin = s_saved_yaw_origin;
    pitch_origin_set = s_saved_pitch_origin_set;
    yaw_origin_set = s_saved_yaw_origin_set;
    action_zero_btn_refresh_style();
}

static void action_play_stop(void)
{
    if (!s_playing) {
        return;
    }
    s_playing = false;
    s_play_index = 0;
    s_play_count = 0;
    action_play_restore_origin();
    if (play_label && lv_obj_is_valid(play_label)) {
        lv_obj_add_flag(play_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void action_play_stop_from_user(void)
{
    if (!s_playing) {
        return;
    }
    action_play_stop();
    action_display_clear_trail_internal();
    action_redraw_scene();
}

static void action_canvas_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (!s_playing) {
        return;
    }
    lv_indev_t *indev = lv_indev_get_act();
    if (!indev) {
        return;
    }
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t coords;
    lv_obj_get_coords(action_canvas, &coords);
    int dx = (p.x - coords.x1) - target_cx;
    int dy = (p.y - coords.y1) - target_cy;
    if ((dx * dx + dy * dy) <= (target_radius * target_radius)) {
        action_play_stop_from_user();
    }
}

static void action_rec_stop(bool save)
{
    if (!s_recording) {
        return;
    }
    s_recording = false;
    uint32_t duration_ms = 0;
    if (s_rec_start_us > 0) {
        int64_t elapsed = (esp_timer_get_time() - s_rec_start_us) / 1000;
        if (elapsed < 0) {
            elapsed = 0;
        }
        duration_ms = (uint32_t)elapsed;
    }
    action_rec_refresh_style();

    if (save && s_rec_count >= 8 && action_store_ready()) {
        esp_err_t err = action_store_save(s_rec_buf, s_rec_count, duration_ms,
                                         pitch_origin, yaw_origin);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "save recording failed: %s", esp_err_to_name(err));
        }
    }
    s_rec_count = 0;
}

static void action_rec_start(void)
{
    action_play_stop();
    action_display_clear_trail_internal();
    s_rec_count = 0;
    s_rec_start_us = esp_timer_get_time();
    s_recording = true;
    action_rec_refresh_style();
    if (rec_time_label && lv_obj_is_valid(rec_time_label)) {
        lv_label_set_text(rec_time_label, "0:00");
        lv_obj_clear_flag(rec_time_label, LV_OBJ_FLAG_HIDDEN);
    }
}

static void action_rec_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (s_recording) {
        action_rec_stop(true);
    } else {
        action_rec_start();
    }
}

static void action_list_close(void)
{
    if (list_overlay && lv_obj_is_valid(list_overlay)) {
        lv_obj_add_flag(list_overlay, LV_OBJ_FLAG_HIDDEN);
    }
}

static void action_play_start(uint8_t slot)
{
    action_rec_stop(true);
    action_store_info_t info;
    esp_err_t err = action_store_load(slot, s_rec_buf, ACTION_STORE_MAX_SAMPLES, &info);
    if (err != ESP_OK || info.sample_count == 0) {
        ESP_LOGW(TAG, "load slot %u failed: %s", (unsigned)slot, esp_err_to_name(err));
        return;
    }

    s_saved_pitch_origin = pitch_origin;
    s_saved_yaw_origin = yaw_origin;
    s_saved_pitch_origin_set = pitch_origin_set;
    s_saved_yaw_origin_set = yaw_origin_set;
    pitch_origin = info.pitch_origin;
    yaw_origin = info.yaw_origin;
    pitch_origin_set = true;
    yaw_origin_set = true;
    action_zero_btn_refresh_style();

    action_display_clear_trail_internal();
    s_play_count = info.sample_count;
    s_play_index = 0;
    s_play_pitch_origin = info.pitch_origin;
    s_play_yaw_origin = info.yaw_origin;
    s_play_start_us = esp_timer_get_time();
    s_playing = true;
    action_list_close();
    action_play_refresh_label(0, info.duration_ms);
}

static void action_list_item_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    uintptr_t slot = (uintptr_t)lv_event_get_user_data(e);
    action_play_start((uint8_t)slot);
}

static void action_list_overlay_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    action_list_close();
}

static void action_list_fill(lv_obj_t *list)
{
    lv_obj_clean(list);
    action_store_info_t items[ACTION_STORE_MAX_SLOTS];
    int n = action_store_list(items, ACTION_STORE_MAX_SLOTS);
    if (n <= 0) {
        lv_obj_t *empty = lv_label_create(list);
        lv_label_set_text(empty, "No recordings");
        lv_obj_set_style_text_color(empty, lv_color_hex(0xAAAAAA), 0);
        lv_obj_set_style_text_font(empty, &lv_font_montserrat_16, 0);
        lv_obj_set_style_pad_all(empty, 12, 0);
        return;
    }
    for (int i = 0; i < n; i++) {
        char line[40];
        char dur[12];
        action_format_mmss(items[i].duration_ms, dur, sizeof(dur));
        snprintf(line, sizeof(line), "#%u   %s", (unsigned)items[i].seq, dur);
        lv_obj_t *btn = lv_list_add_btn(list, LV_SYMBOL_PLAY, line);
        lv_obj_set_style_bg_color(btn, lv_color_hex(0x333333), 0);
        lv_obj_set_style_text_color(btn, lv_color_white(), 0);
        lv_obj_set_style_text_font(btn, &lv_font_montserrat_16, 0);
        lv_obj_add_event_cb(btn, action_list_item_cb, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)items[i].slot);
    }
}

static void action_list_open(void)
{
    if (!list_overlay || !lv_obj_is_valid(list_overlay)) {
        return;
    }
    if (!list_widget || !lv_obj_is_valid(list_widget)) {
        return;
    }
    action_list_fill(list_widget);
    lv_obj_clear_flag(list_overlay, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(list_overlay);
}

static void action_list_btn_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    if (list_overlay && lv_obj_is_valid(list_overlay) &&
        !lv_obj_has_flag(list_overlay, LV_OBJ_FLAG_HIDDEN)) {
        action_list_close();
        return;
    }
    action_list_open();
}

static lv_obj_t *action_make_round_btn(lv_obj_t *parent, lv_align_t align,
                                       lv_coord_t x_ofs, lv_coord_t y_ofs,
                                       lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 44, 44);
    lv_obj_align(btn, align, x_ofs, y_ofs);
    lv_obj_set_ext_click_area(btn, 12);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(btn, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_set_style_radius(btn, 22, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x303030), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x505050), LV_STATE_PRESSED);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_pad_all(btn, 0, 0);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
    return btn;
}

static void action_record_ui_create(lv_obj_t *parent)
{
    rec_btn = action_make_round_btn(parent, LV_ALIGN_TOP_LEFT, 8, 8, action_rec_btn_event_cb);
    rec_dot = lv_obj_create(rec_btn);
    lv_obj_remove_style_all(rec_dot);
    lv_obj_set_size(rec_dot, 18, 18);
    lv_obj_center(rec_dot);
    lv_obj_clear_flag(rec_dot, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(rec_dot, 9, 0);
    lv_obj_set_style_bg_color(rec_dot, lv_color_hex(0xCC0000), 0);
    lv_obj_set_style_bg_opa(rec_dot, LV_OPA_COVER, 0);

    rec_time_label = lv_label_create(parent);
    lv_label_set_text(rec_time_label, "0:00");
    lv_obj_set_style_text_color(rec_time_label, lv_color_hex(0xFF4444), 0);
    lv_obj_set_style_text_font(rec_time_label, &lv_font_montserrat_16, 0);
    lv_obj_align(rec_time_label, LV_ALIGN_TOP_LEFT, 56, 18);
    lv_obj_add_flag(rec_time_label, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(rec_time_label, LV_OBJ_FLAG_CLICKABLE);

    list_btn = action_make_round_btn(parent, LV_ALIGN_TOP_RIGHT, -8, 8, action_list_btn_event_cb);
    lv_obj_t *list_icon = lv_label_create(list_btn);
    lv_label_set_text(list_icon, LV_SYMBOL_LIST);
    lv_obj_set_style_text_color(list_icon, lv_color_white(), 0);
    lv_obj_set_style_text_font(list_icon, &lv_font_montserrat_16, 0);
    lv_obj_center(list_icon);

    play_label = lv_label_create(parent);
    lv_label_set_text(play_label, "PLAY 0:00/0:00");
    lv_obj_set_style_text_color(play_label, lv_color_hex(0x66FF66), 0);
    lv_obj_set_style_text_font(play_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_align(play_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_bg_color(play_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(play_label, LV_OPA_80, 0);
    lv_obj_set_style_pad_hor(play_label, 10, 0);
    lv_obj_set_style_pad_ver(play_label, 13, 0);
    lv_obj_set_style_radius(play_label, 8, 0);
    lv_obj_add_flag(play_label, LV_OBJ_FLAG_FLOATING);
    lv_obj_clear_flag(play_label, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(play_label, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_align(play_label, LV_ALIGN_TOP_MID, 0, PLAY_TIME_Y);
    lv_obj_add_flag(play_label, LV_OBJ_FLAG_HIDDEN);

    list_overlay = lv_obj_create(parent);
    lv_obj_set_size(list_overlay, lv_pct(100), lv_pct(100));
    lv_obj_align(list_overlay, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(list_overlay, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(list_overlay, LV_OPA_70, 0);
    lv_obj_set_style_border_width(list_overlay, 0, 0);
    lv_obj_set_style_pad_all(list_overlay, 0, 0);
    lv_obj_add_event_cb(list_overlay, action_list_overlay_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_flag(list_overlay, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *panel = lv_obj_create(list_overlay);
    lv_obj_set_size(panel, 252, 360);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, 56);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x1A1A1A), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(panel, 12, 0);
    lv_obj_set_style_border_color(panel, lv_color_white(), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_pad_all(panel, 10, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_CLICK_FOCUSABLE);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_EVENT_BUBBLE);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Recordings");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_16, 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 4, 2);

    lv_obj_t *close_btn = lv_btn_create(panel);
    lv_obj_set_size(close_btn, 36, 32);
    lv_obj_align(close_btn, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_obj_set_style_bg_color(close_btn, lv_color_hex(0x404040), 0);
    lv_obj_set_style_radius(close_btn, 6, 0);
    lv_obj_add_event_cb(close_btn, action_list_overlay_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *close_lbl = lv_label_create(close_btn);
    lv_label_set_text(close_lbl, LV_SYMBOL_CLOSE);
    lv_obj_set_style_text_color(close_lbl, lv_color_white(), 0);
    lv_obj_center(close_lbl);

    list_widget = lv_list_create(panel);
    lv_obj_set_size(list_widget, 228, 292);
    lv_obj_align(list_widget, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(list_widget, lv_color_hex(0x111111), 0);
    lv_obj_set_style_border_width(list_widget, 0, 0);
    lv_obj_set_style_pad_all(list_widget, 4, 0);

    action_rec_refresh_style();
}

static void action_display_labels_create(lv_obj_t *parent)
{
    zero_btn = action_make_btn(parent, "SET ZERO", action_zero_btn_event_cb,
                               LV_ALIGN_BOTTOM_LEFT, 12);
    clear_btn = action_make_btn(parent, "CLEAR", action_clear_btn_event_cb,
                                LV_ALIGN_BOTTOM_RIGHT, -12);
    action_zero_btn_refresh_style();

    range_minus_btn = action_make_range_btn(parent, "-", action_range_minus_cb, -48, -108);
    range_plus_btn = action_make_range_btn(parent, "+", action_range_plus_cb, 48, -108);

    range_label = lv_label_create(parent);
    lv_obj_set_style_text_color(range_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(range_label, &lv_font_montserrat_16, 0);
    lv_obj_align(range_label, LV_ALIGN_BOTTOM_MID, 0, -114);
    lv_obj_clear_flag(range_label, LV_OBJ_FLAG_CLICKABLE);
    action_range_refresh_label();

    trail_minus_btn = action_make_range_btn(parent, "-", action_trail_minus_cb, -48, -70);
    trail_plus_btn = action_make_range_btn(parent, "+", action_trail_plus_cb, 48, -70);

    trail_len_label = lv_label_create(parent);
    lv_obj_set_style_text_color(trail_len_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(trail_len_label, &lv_font_montserrat_16, 0);
    lv_obj_align(trail_len_label, LV_ALIGN_BOTTOM_MID, 0, -76);
    lv_obj_clear_flag(trail_len_label, LV_OBJ_FLAG_CLICKABLE);
    action_trail_refresh_label();

    pitch_label = lv_label_create(parent);
    lv_label_set_text(pitch_label, "P: 0.0");
    lv_obj_set_style_text_color(pitch_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(pitch_label, &lv_font_montserrat_16, 0);
    lv_obj_align(pitch_label, LV_ALIGN_BOTTOM_LEFT, 12, -148);
    lv_obj_clear_flag(pitch_label, LV_OBJ_FLAG_CLICKABLE);

    yaw_label = lv_label_create(parent);
    lv_label_set_text(yaw_label, "Y: 0.0");
    lv_obj_set_style_text_color(yaw_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(yaw_label, &lv_font_montserrat_16, 0);
    lv_obj_align(yaw_label, LV_ALIGN_BOTTOM_RIGHT, -12, -148);
    lv_obj_clear_flag(yaw_label, LV_OBJ_FLAG_CLICKABLE);

    action_record_ui_create(parent);
}

void action_display_init(lv_obj_t *parent, int screen_width, int screen_height)
{
    disp_width = screen_width;
    disp_height = screen_height;

    if (action_canvas) {
        lv_obj_del(action_canvas);
        action_canvas = NULL;
    }
    if (zero_btn) {
        lv_obj_del(zero_btn);
        zero_btn = NULL;
    }
    if (clear_btn) {
        lv_obj_del(clear_btn);
        clear_btn = NULL;
    }
    if (range_minus_btn) {
        lv_obj_del(range_minus_btn);
        range_minus_btn = NULL;
    }
    if (range_plus_btn) {
        lv_obj_del(range_plus_btn);
        range_plus_btn = NULL;
    }
    if (range_label) {
        lv_obj_del(range_label);
        range_label = NULL;
    }
    if (trail_minus_btn) {
        lv_obj_del(trail_minus_btn);
        trail_minus_btn = NULL;
    }
    if (trail_plus_btn) {
        lv_obj_del(trail_plus_btn);
        trail_plus_btn = NULL;
    }
    if (trail_len_label) {
        lv_obj_del(trail_len_label);
        trail_len_label = NULL;
    }
    if (pitch_label) {
        lv_obj_del(pitch_label);
        pitch_label = NULL;
    }
    if (yaw_label) {
        lv_obj_del(yaw_label);
        yaw_label = NULL;
    }
    if (rec_btn) {
        lv_obj_del(rec_btn);
        rec_btn = NULL;
        rec_dot = NULL;
    }
    if (rec_time_label) {
        lv_obj_del(rec_time_label);
        rec_time_label = NULL;
    }
    if (list_btn) {
        lv_obj_del(list_btn);
        list_btn = NULL;
    }
    if (play_label) {
        lv_obj_del(play_label);
        play_label = NULL;
    }
    if (list_overlay) {
        lv_obj_del(list_overlay);
        list_overlay = NULL;
        list_widget = NULL;
    }
    if (cbuf) {
        heap_caps_free(cbuf);
        cbuf = NULL;
    }
    if (target_bg_buf) {
        heap_caps_free(target_bg_buf);
        target_bg_buf = NULL;
    }
    target_cached = false;
    action_display_clear_trail_internal();
    pitch_origin = 0.0f;
    yaw_origin = 0.0f;
    pitch_origin_set = false;
    yaw_origin_set = false;
    s_live_pitch = 0.0f;
    s_live_yaw = 0.0f;
    s_last_label_pitch = 9999.0f;
    s_last_label_yaw = 9999.0f;
    s_recording = false;
    s_playing = false;
    s_rec_count = 0;
    s_play_index = 0;
    s_play_count = 0;

    target_cx = disp_width / 2;
    target_cy = (disp_height - BOTTOM_LABEL_HEIGHT) / 2;
    int max_r_x = disp_width / 2 - 8;
    int max_r_y = target_cy - 8;
    target_radius = (max_r_x < max_r_y) ? max_r_x : max_r_y;
    if (target_radius < 60) {
        target_radius = 60;
    }

    size_t buf_size = sizeof(lv_color_t) * (size_t)disp_width * (size_t)disp_height;
    ESP_LOGI(TAG, "Requested canvas buffer: %u bytes (%dx%d)",
             (unsigned)buf_size, disp_width, disp_height);

    if (!memory_diag_check_available(buf_size, true)) {
        ESP_LOGE(TAG, "Insufficient memory for canvas buffer");
        return;
    }

    cbuf = (lv_color_t *)memory_diag_smart_malloc(buf_size, true, true);
    if (!cbuf) {
        ESP_LOGE(TAG, "Canvas buffer allocation failed");
        return;
    }

    memset(cbuf, 0, buf_size);

    action_canvas = lv_canvas_create(parent);
    if (!action_canvas) {
        ESP_LOGE(TAG, "Failed to create canvas");
        return;
    }

    lv_obj_set_size(action_canvas, disp_width, disp_height);
    lv_obj_align(action_canvas, LV_ALIGN_CENTER, 0, 0);
    lv_obj_add_flag(action_canvas, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(action_canvas, action_canvas_event_cb, LV_EVENT_CLICKED, NULL);
    lv_canvas_set_buffer(action_canvas, cbuf, disp_width, disp_height, LV_IMG_CF_TRUE_COLOR);

    draw_target_background(cbuf);
    draw_diamond(cbuf, target_cx, target_cy, DIAMOND_SIZE,
                 lv_color_hex(0x00FF00), lv_color_hex(0x006600));

    target_bg_buf = (lv_color_t *)memory_diag_smart_malloc(buf_size, true, true);
    if (target_bg_buf) {
        draw_target_background(target_bg_buf);
        target_cached = true;
        ESP_LOGI(TAG, "Target cache allocated");
    } else {
        target_cached = false;
        ESP_LOGW(TAG, "Target cache alloc failed, will redraw rings each frame");
    }

    action_display_labels_create(parent);
    if (zero_btn) {
        lv_obj_move_foreground(zero_btn);
    }
    if (clear_btn) {
        lv_obj_move_foreground(clear_btn);
    }
    if (range_minus_btn) {
        lv_obj_move_foreground(range_minus_btn);
    }
    if (range_plus_btn) {
        lv_obj_move_foreground(range_plus_btn);
    }
    if (range_label) {
        lv_obj_move_foreground(range_label);
    }
    if (trail_minus_btn) {
        lv_obj_move_foreground(trail_minus_btn);
    }
    if (trail_plus_btn) {
        lv_obj_move_foreground(trail_plus_btn);
    }
    if (trail_len_label) {
        lv_obj_move_foreground(trail_len_label);
    }
    if (pitch_label) {
        lv_obj_move_foreground(pitch_label);
    }
    if (yaw_label) {
        lv_obj_move_foreground(yaw_label);
    }
    if (rec_btn) {
        lv_obj_move_foreground(rec_btn);
    }
    if (rec_time_label) {
        lv_obj_move_foreground(rec_time_label);
    }
    if (list_btn) {
        lv_obj_move_foreground(list_btn);
    }
    if (play_label) {
        lv_obj_move_foreground(play_label);
    }
    if (list_overlay) {
        lv_obj_move_foreground(list_overlay);
    }

    ESP_LOGI(TAG, "init complete, target %d,%d r=%d", target_cx, target_cy, target_radius);
}

void action_display_update(float pitch, float yaw)
{
    if (!action_canvas || !cbuf) {
        return;
    }
    if (!lv_obj_is_valid(action_canvas)) {
        action_canvas = NULL;
        return;
    }
    if (disp_width <= 0 || disp_height <= 0) {
        return;
    }

    if (s_playing) {
        if (s_play_index >= s_play_count) {
            action_play_stop();
        } else {
            pitch = s_rec_buf[s_play_index].pitch;
            yaw = s_rec_buf[s_play_index].yaw;
            s_play_index++;
            uint32_t now_ms = ((uint32_t)s_play_index * 1000u) / ACTION_STORE_SAMPLE_HZ;
            uint32_t tot_ms = ((uint32_t)s_play_count * 1000u) / ACTION_STORE_SAMPLE_HZ;
            action_play_refresh_label(now_ms, tot_ms);
        }
    }

    s_live_pitch = pitch;
    s_live_yaw = yaw;
    if (!s_playing && !yaw_origin_set) {
        angle_calc_capture_yaw_bias();
        yaw_origin = yaw;
        yaw_origin_set = true;
    }

    if (s_recording) {
        if (s_rec_count < ACTION_STORE_MAX_SAMPLES) {
            s_rec_buf[s_rec_count].pitch = pitch;
            s_rec_buf[s_rec_count].yaw = yaw;
            s_rec_count++;
        }
        uint32_t elapsed_ms = 0;
        if (s_rec_start_us > 0) {
            int64_t elapsed = (esp_timer_get_time() - s_rec_start_us) / 1000;
            if (elapsed < 0) {
                elapsed = 0;
            }
            elapsed_ms = (uint32_t)elapsed;
        }
        if (rec_time_label && lv_obj_is_valid(rec_time_label)) {
            char tbuf[12];
            action_format_mmss(elapsed_ms, tbuf, sizeof(tbuf));
            lv_label_set_text(rec_time_label, tbuf);
        }
        if (s_rec_count >= ACTION_STORE_MAX_SAMPLES || elapsed_ms >= 60000u) {
            action_rec_stop(true);
        }
    }

    int px;
    int py;
    angles_to_point(pitch, yaw, &px, &py);

    bool moved = first_update || px != last_px || py != last_py;
    if (moved) {
        trail_push(pitch, yaw);
        last_px = px;
        last_py = py;
        first_update = false;
    }

    if (moved || trail_dirty) {
        restore_target_background();
        draw_trail_and_marker(px, py);
        lv_obj_invalidate(action_canvas);
        trail_dirty = false;
    }

    float rel_yaw = wrap_deg180(yaw - yaw_origin);
    if (pitch_label && yaw_label &&
        (fabsf(pitch - s_last_label_pitch) >= 0.05f ||
         fabsf(rel_yaw - s_last_label_yaw) >= 0.05f)) {
        s_last_label_pitch = pitch;
        s_last_label_yaw = rel_yaw;
        char buf[24];
        if (pitch_origin_set) {
            snprintf(buf, sizeof(buf), "P: %.1f*", pitch);
        } else {
            snprintf(buf, sizeof(buf), "P: %.1f", pitch);
        }
        lv_label_set_text(pitch_label, buf);
        if (pitch_origin_set) {
            snprintf(buf, sizeof(buf), "Y: %.1f*", rel_yaw);
        } else {
            snprintf(buf, sizeof(buf), "Y: %.1f", rel_yaw);
        }
        lv_label_set_text(yaw_label, buf);
    }
}

void action_display_clear_trail(void)
{
    action_apply_clear_or_zero(false);
}

void action_display_cleanup(void)
{
    ESP_LOGI(TAG, "cleanup");
    action_rec_stop(true);
    action_play_stop();
    action_list_close();

    if (zero_btn && lv_obj_is_valid(zero_btn)) {
        lv_obj_del(zero_btn);
    }
    zero_btn = NULL;

    if (clear_btn && lv_obj_is_valid(clear_btn)) {
        lv_obj_del(clear_btn);
    }
    clear_btn = NULL;

    if (range_minus_btn && lv_obj_is_valid(range_minus_btn)) {
        lv_obj_del(range_minus_btn);
    }
    range_minus_btn = NULL;

    if (range_plus_btn && lv_obj_is_valid(range_plus_btn)) {
        lv_obj_del(range_plus_btn);
    }
    range_plus_btn = NULL;

    if (range_label && lv_obj_is_valid(range_label)) {
        lv_obj_del(range_label);
    }
    range_label = NULL;

    if (trail_minus_btn && lv_obj_is_valid(trail_minus_btn)) {
        lv_obj_del(trail_minus_btn);
    }
    trail_minus_btn = NULL;

    if (trail_plus_btn && lv_obj_is_valid(trail_plus_btn)) {
        lv_obj_del(trail_plus_btn);
    }
    trail_plus_btn = NULL;

    if (trail_len_label && lv_obj_is_valid(trail_len_label)) {
        lv_obj_del(trail_len_label);
    }
    trail_len_label = NULL;

    if (action_canvas && lv_obj_is_valid(action_canvas)) {
        lv_obj_del(action_canvas);
    }
    action_canvas = NULL;

    if (pitch_label && lv_obj_is_valid(pitch_label)) {
        lv_obj_del(pitch_label);
    }
    pitch_label = NULL;

    if (yaw_label && lv_obj_is_valid(yaw_label)) {
        lv_obj_del(yaw_label);
    }
    yaw_label = NULL;

    if (rec_btn && lv_obj_is_valid(rec_btn)) {
        lv_obj_del(rec_btn);
    }
    rec_btn = NULL;
    rec_dot = NULL;

    if (rec_time_label && lv_obj_is_valid(rec_time_label)) {
        lv_obj_del(rec_time_label);
    }
    rec_time_label = NULL;

    if (list_btn && lv_obj_is_valid(list_btn)) {
        lv_obj_del(list_btn);
    }
    list_btn = NULL;

    if (play_label && lv_obj_is_valid(play_label)) {
        lv_obj_del(play_label);
    }
    play_label = NULL;

    if (list_overlay && lv_obj_is_valid(list_overlay)) {
        lv_obj_del(list_overlay);
    }
    list_overlay = NULL;
    list_widget = NULL;

    if (cbuf) {
        heap_caps_free(cbuf);
        cbuf = NULL;
    }
    if (target_bg_buf) {
        heap_caps_free(target_bg_buf);
        target_bg_buf = NULL;
    }

    target_cached = false;
    action_display_clear_trail_internal();
    pitch_origin = 0.0f;
    yaw_origin = 0.0f;
    pitch_origin_set = false;
    yaw_origin_set = false;
    s_last_label_pitch = 9999.0f;
    s_last_label_yaw = 9999.0f;
    s_recording = false;
    s_playing = false;
}

bool action_display_is_overlay_open(void)
{
    return list_overlay && lv_obj_is_valid(list_overlay) &&
           !lv_obj_has_flag(list_overlay, LV_OBJ_FLAG_HIDDEN);
}
