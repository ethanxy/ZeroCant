#include "settings_ammo.h"
#include "ballistic_profile.h"
#include "lvgl.h"
#include <stdio.h>
#include <math.h>

#define AMMO_FONT_TITLE      &lv_font_montserrat_24
#define AMMO_FONT_HINT       &lv_font_montserrat_14
#define AMMO_FONT_ROW        &lv_font_montserrat_16
#define AMMO_FONT_BTN        &lv_font_montserrat_16

#define AMMO_ROW_Y0          68
#define AMMO_ROW_STEP        47
#define AMMO_ROW_NAME_X      14
#define AMMO_ROW_VALUE_X     84
#define AMMO_BTN_H           38
#define AMMO_BTN_SM_W        40
#define AMMO_BTN_TOGGLE_W    84
#define AMMO_BACK_W          200
#define AMMO_BACK_H          48

static lv_obj_t *ammo_page = NULL;
static lv_obj_t *val_labels[6] = {0};
static bool ammo_open = false;

enum {
    ROW_GR = 0,
    ROW_BC,
    ROW_FPS,
    ROW_ZERO,
    ROW_SIGHT,
    ROW_MODEL,
    ROW_COUNT
};

static void refresh_value_labels(void) {
    const ballistic_profile_t *p = ballistic_profile_get();
    char buf[32];

    if (val_labels[ROW_GR]) {
        snprintf(buf, sizeof(buf), "%.0f gr", p->bullet_weight_gr);
        lv_label_set_text(val_labels[ROW_GR], buf);
    }
    if (val_labels[ROW_BC]) {
        snprintf(buf, sizeof(buf), "%.3f", p->bc);
        lv_label_set_text(val_labels[ROW_BC], buf);
    }
    if (val_labels[ROW_FPS]) {
        snprintf(buf, sizeof(buf), "%.0f fps", p->muzzle_velocity_fps);
        lv_label_set_text(val_labels[ROW_FPS], buf);
    }
    if (val_labels[ROW_ZERO]) {
        snprintf(buf, sizeof(buf), "%.0f m", p->zero_range_m);
        lv_label_set_text(val_labels[ROW_ZERO], buf);
    }
    if (val_labels[ROW_SIGHT]) {
        float inches = p->sight_height_m * 39.3700787f;
        snprintf(buf, sizeof(buf), "%.1f in", inches);
        lv_label_set_text(val_labels[ROW_SIGHT], buf);
    }
    if (val_labels[ROW_MODEL]) {
        lv_label_set_text(val_labels[ROW_MODEL],
                          p->drag_model == BALLISTIC_DRAG_G7 ? "G7" : "G1");
    }
}

static void clamp_profile(ballistic_profile_t *p) {
    if (p->bullet_weight_gr < 30.0f) p->bullet_weight_gr = 30.0f;
    if (p->bullet_weight_gr > 250.0f) p->bullet_weight_gr = 250.0f;
    if (p->bc < 0.050f) p->bc = 0.050f;
    if (p->bc > 1.200f) p->bc = 1.200f;
    if (p->muzzle_velocity_fps < 800.0f) p->muzzle_velocity_fps = 800.0f;
    if (p->muzzle_velocity_fps > 4500.0f) p->muzzle_velocity_fps = 4500.0f;
    if (p->zero_range_m < 25.0f) p->zero_range_m = 25.0f;
    if (p->zero_range_m > 500.0f) p->zero_range_m = 500.0f;
    float inches = p->sight_height_m * 39.3700787f;
    if (inches < 0.5f) inches = 0.5f;
    if (inches > 4.0f) inches = 4.0f;
    p->sight_height_m = inches / 39.3700787f;
    /* Keep .223 diameter for 68gr-class; bump if heavy bullet looks like .308 */
    if (p->bullet_weight_gr >= 140.0f && p->bullet_diameter_in < 0.28f) {
        p->bullet_diameter_in = 0.308f;
    } else if (p->bullet_weight_gr < 100.0f) {
        p->bullet_diameter_in = 0.224f;
    }
}

typedef struct {
    int row;
    float delta;
} adj_t;

static void adjust_event(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    adj_t *adj = (adj_t *)lv_event_get_user_data(e);
    if (!adj) {
        return;
    }

    ballistic_profile_t *p = ballistic_profile_get_mut();
    switch (adj->row) {
        case ROW_GR:
            p->bullet_weight_gr += adj->delta;
            break;
        case ROW_BC:
            p->bc += adj->delta;
            break;
        case ROW_FPS:
            p->muzzle_velocity_fps += adj->delta;
            break;
        case ROW_ZERO:
            p->zero_range_m += adj->delta;
            break;
        case ROW_SIGHT:
            p->sight_height_m += adj->delta / 39.3700787f;
            break;
        case ROW_MODEL:
            p->drag_model = (p->drag_model == BALLISTIC_DRAG_G1)
                                ? BALLISTIC_DRAG_G7
                                : BALLISTIC_DRAG_G1;
            break;
        default:
            break;
    }
    clamp_profile(p);
    ballistic_profile_refresh_name();
    refresh_value_labels();
}

static void back_event(lv_event_t *e) {
    if (lv_event_get_code(e) != LV_EVENT_CLICKED) {
        return;
    }
    ballistic_profile_save();
    settings_ammo_page_hide();
}

static lv_obj_t *make_btn(lv_obj_t *parent, const char *txt, int w, int h) {
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x404040), 0);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(btn, lv_color_white(), 0);
    lv_obj_set_style_border_width(btn, 1, 0);
    lv_obj_set_style_radius(btn, 6, 0);
    lv_obj_set_ext_click_area(btn, 10);

    lv_obj_t *lab = lv_label_create(btn);
    lv_label_set_text(lab, txt);
    lv_obj_set_style_text_color(lab, lv_color_white(), 0);
    lv_obj_set_style_text_font(lab, AMMO_FONT_BTN, 0);
    lv_obj_center(lab);
    return btn;
}

static adj_t s_adj_store[ROW_COUNT * 2];
static int s_adj_idx = 0;

static void add_adjust_row(lv_obj_t *parent, int y, int row, const char *title,
                           float delta_minus, float delta_plus, bool model_toggle) {
    lv_obj_t *name = lv_label_create(parent);
    lv_label_set_text(name, title);
    lv_obj_set_style_text_color(name, lv_color_white(), 0);
    lv_obj_set_style_text_font(name, AMMO_FONT_ROW, 0);
    lv_obj_align(name, LV_ALIGN_TOP_LEFT, AMMO_ROW_NAME_X, y + 8);

    val_labels[row] = lv_label_create(parent);
    lv_obj_set_style_text_color(val_labels[row], lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(val_labels[row], AMMO_FONT_ROW, 0);
    lv_obj_align(val_labels[row], LV_ALIGN_TOP_LEFT, AMMO_ROW_VALUE_X, y + 8);

    if (model_toggle) {
        lv_obj_t *btn = make_btn(parent, "G1/G7", AMMO_BTN_TOGGLE_W, AMMO_BTN_H);
        lv_obj_align(btn, LV_ALIGN_TOP_RIGHT, -12, y);
        if (s_adj_idx < (int)(sizeof(s_adj_store) / sizeof(s_adj_store[0]))) {
            s_adj_store[s_adj_idx].row = row;
            s_adj_store[s_adj_idx].delta = 0;
            lv_obj_add_event_cb(btn, adjust_event, LV_EVENT_CLICKED, &s_adj_store[s_adj_idx]);
            s_adj_idx++;
        }
        return;
    }

    lv_obj_t *minus = make_btn(parent, "-", AMMO_BTN_SM_W, AMMO_BTN_H);
    lv_obj_align(minus, LV_ALIGN_TOP_RIGHT, -60, y);
    lv_obj_t *plus = make_btn(parent, "+", AMMO_BTN_SM_W, AMMO_BTN_H);
    lv_obj_align(plus, LV_ALIGN_TOP_RIGHT, -12, y);

    if (s_adj_idx + 1 < (int)(sizeof(s_adj_store) / sizeof(s_adj_store[0]))) {
        s_adj_store[s_adj_idx].row = row;
        s_adj_store[s_adj_idx].delta = delta_minus;
        lv_obj_add_event_cb(minus, adjust_event, LV_EVENT_CLICKED, &s_adj_store[s_adj_idx]);
        s_adj_idx++;
        s_adj_store[s_adj_idx].row = row;
        s_adj_store[s_adj_idx].delta = delta_plus;
        lv_obj_add_event_cb(plus, adjust_event, LV_EVENT_CLICKED, &s_adj_store[s_adj_idx]);
        s_adj_idx++;
    }
}

void settings_ammo_page_create(lv_obj_t *parent, int screen_width, int screen_height) {
    if (ammo_page) {
        return;
    }

    s_adj_idx = 0;

    ammo_page = lv_obj_create(parent);
    lv_obj_set_size(ammo_page, screen_width, screen_height);
    lv_obj_align(ammo_page, LV_ALIGN_TOP_LEFT, 0, 0);
    lv_obj_set_style_bg_color(ammo_page, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(ammo_page, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(ammo_page, 0, 0);
    lv_obj_set_style_pad_all(ammo_page, 0, 0);
    lv_obj_clear_flag(ammo_page, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(ammo_page, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *title = lv_label_create(ammo_page);
    lv_label_set_text(title, "Ammo Setup");
    lv_obj_set_style_text_color(title, lv_color_white(), 0);
    lv_obj_set_style_text_font(title, AMMO_FONT_TITLE, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 14);

    lv_obj_t *hint = lv_label_create(ammo_page);
    lv_label_set_text(hint, "Sight ~1.5in typical");
    lv_obj_set_style_text_color(hint, lv_color_hex(0x808080), 0);
    lv_obj_set_style_text_font(hint, AMMO_FONT_HINT, 0);
    lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 46);

    add_adjust_row(ammo_page, AMMO_ROW_Y0 + 0 * AMMO_ROW_STEP, ROW_GR, "Gr", -1.0f, 1.0f, false);
    add_adjust_row(ammo_page, AMMO_ROW_Y0 + 1 * AMMO_ROW_STEP, ROW_BC, "BC", -0.005f, 0.005f, false);
    add_adjust_row(ammo_page, AMMO_ROW_Y0 + 2 * AMMO_ROW_STEP, ROW_FPS, "FPS", -25.0f, 25.0f, false);
    add_adjust_row(ammo_page, AMMO_ROW_Y0 + 3 * AMMO_ROW_STEP, ROW_ZERO, "Zero", -5.0f, 5.0f, false);
    add_adjust_row(ammo_page, AMMO_ROW_Y0 + 4 * AMMO_ROW_STEP, ROW_SIGHT, "Sight", -0.1f, 0.1f, false);
    add_adjust_row(ammo_page, AMMO_ROW_Y0 + 5 * AMMO_ROW_STEP, ROW_MODEL, "Model", 0, 0, true);

    /* Keep above the settings close-swipe zone at the bottom */
    lv_obj_t *back = make_btn(ammo_page, "Back / Save", AMMO_BACK_W, AMMO_BACK_H);
    lv_obj_set_ext_click_area(back, 14);
    lv_obj_align(back, LV_ALIGN_BOTTOM_MID, 0, -58);
    lv_obj_add_event_cb(back, back_event, LV_EVENT_CLICKED, NULL);

    (void)screen_height;
    ammo_open = false;
}

void settings_ammo_page_show(void) {
    if (!ammo_page) {
        return;
    }
    refresh_value_labels();
    lv_obj_clear_flag(ammo_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(ammo_page);
    ammo_open = true;
}

void settings_ammo_page_hide(void) {
    if (!ammo_page) {
        return;
    }
    lv_obj_add_flag(ammo_page, LV_OBJ_FLAG_HIDDEN);
    ammo_open = false;
}

void settings_ammo_page_destroy(void) {
    if (ammo_page && lv_obj_is_valid(ammo_page)) {
        lv_obj_del(ammo_page);
    }
    ammo_page = NULL;
    for (int i = 0; i < ROW_COUNT; i++) {
        val_labels[i] = NULL;
    }
    ammo_open = false;
}

bool settings_ammo_page_is_open(void) {
    return ammo_open;
}
