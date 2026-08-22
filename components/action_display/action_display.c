#include "action_display.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "memory_diag.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "action_display";

/* Outer ring default ±5°. UI steps this by ±5°. */
#define ANGLE_RANGE_DEFAULT     10.0f
#define ANGLE_RANGE_STEP        5.0f
#define ANGLE_RANGE_MIN         5.0f
#define ANGLE_RANGE_MAX         45.0f
#define TRAIL_CAP               200
#define TRAIL_DEFAULT           20
#define TRAIL_STEP              10
#define TRAIL_MIN               10
#define TRAIL_LIMIT_MAX         200
#define DIAMOND_SIZE            10
#define RING_COUNT              5
#define BOTTOM_LABEL_HEIGHT     156

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
static lv_obj_t *roll_label = NULL;
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
    float roll;
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
static float s_live_roll = 0.0f;
static float pitch_origin = 0.0f;
static bool pitch_origin_set = false;
static float s_last_label_pitch = 9999.0f;
static float s_last_label_roll = 9999.0f;
static bool first_update = true;
static bool trail_dirty = true;

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

static void angles_to_point(float pitch, float roll, int *px, int *py)
{
    float span = max_angle_deg;
    if (span < ANGLE_RANGE_MIN) {
        span = ANGLE_RANGE_MIN;
    }
    float rel_pitch = pitch - pitch_origin;
    float nx = roll / span;                 /* roll stays on Set Level */
    float ny = -rel_pitch / span;           /* pitch relative to SET ZERO */
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

static void trail_push(float pitch, float roll)
{
    trail_sample_t sample = { .pitch = pitch, .roll = roll };
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
            angles_to_point(s.pitch, s.roll, &x, &y);
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
    angles_to_point(s_live_pitch, s_live_roll, &px, &py);
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

static void action_apply_clear_or_zero(bool set_pitch_origin)
{
    if (set_pitch_origin) {
        pitch_origin = s_live_pitch;
        pitch_origin_set = true;
        action_zero_btn_refresh_style();
        s_last_label_pitch = 9999.0f; /* force P: x.x* refresh */
    }

    action_display_clear_trail_internal();
    int px;
    int py;
    angles_to_point(s_live_pitch, s_live_roll, &px, &py);
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
    action_apply_clear_or_zero(true);
}

static void action_display_labels_create(lv_obj_t *parent)
{
    zero_btn = action_make_btn(parent, "SET P ZERO", action_zero_btn_event_cb,
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

    roll_label = lv_label_create(parent);
    lv_label_set_text(roll_label, "R: 0.0");
    lv_obj_set_style_text_color(roll_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(roll_label, &lv_font_montserrat_16, 0);
    lv_obj_align(roll_label, LV_ALIGN_BOTTOM_RIGHT, -12, -148);
    lv_obj_clear_flag(roll_label, LV_OBJ_FLAG_CLICKABLE);
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
    if (roll_label) {
        lv_obj_del(roll_label);
        roll_label = NULL;
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
    pitch_origin_set = false;
    s_live_pitch = 0.0f;
    s_live_roll = 0.0f;
    s_last_label_pitch = 9999.0f;
    s_last_label_roll = 9999.0f;

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
    lv_obj_clear_flag(action_canvas, LV_OBJ_FLAG_CLICKABLE);
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
    if (roll_label) {
        lv_obj_move_foreground(roll_label);
    }

    ESP_LOGI(TAG, "init complete, target %d,%d r=%d", target_cx, target_cy, target_radius);
}

void action_display_update(float pitch, float roll)
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

    s_live_pitch = pitch;
    s_live_roll = roll;

    int px;
    int py;
    angles_to_point(pitch, roll, &px, &py);

    bool moved = first_update || px != last_px || py != last_py;
    if (moved) {
        trail_push(pitch, roll);
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

    if (pitch_label && roll_label &&
        (fabsf(pitch - s_last_label_pitch) >= 0.05f ||
         fabsf(roll - s_last_label_roll) >= 0.05f)) {
        s_last_label_pitch = pitch;
        s_last_label_roll = roll;
        char buf[24];
        if (pitch_origin_set) {
            snprintf(buf, sizeof(buf), "P: %.1f*", pitch);
        } else {
            snprintf(buf, sizeof(buf), "P: %.1f", pitch);
        }
        lv_label_set_text(pitch_label, buf);
        snprintf(buf, sizeof(buf), "R: %.1f", roll);
        lv_label_set_text(roll_label, buf);
    }
}

void action_display_clear_trail(void)
{
    action_apply_clear_or_zero(false);
}

void action_display_cleanup(void)
{
    ESP_LOGI(TAG, "cleanup");

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

    if (roll_label && lv_obj_is_valid(roll_label)) {
        lv_obj_del(roll_label);
    }
    roll_label = NULL;

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
    pitch_origin_set = false;
    s_last_label_pitch = 9999.0f;
    s_last_label_roll = 9999.0f;
}
