#ifndef SETTINGS_AMMO_H
#define SETTINGS_AMMO_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Create ammo settings overlay (hidden). Parent = settings_screen. */
void settings_ammo_page_create(lv_obj_t *parent, int screen_width, int screen_height);

/** Show ammo page and refresh values from profile. */
void settings_ammo_page_show(void);

/** Hide ammo page (does not save). */
void settings_ammo_page_hide(void);

/** Delete ammo page objects. */
void settings_ammo_page_destroy(void);

/** True if ammo page is visible. */
bool settings_ammo_page_is_open(void);

#ifdef __cplusplus
}
#endif

#endif /* SETTINGS_AMMO_H */
