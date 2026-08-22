#ifndef ACTION_DISPLAY_H
#define ACTION_DISPLAY_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void action_display_init(lv_obj_t *parent, int screen_width, int screen_height);
void action_display_update(float pitch, float roll);
void action_display_cleanup(void);
void action_display_clear_trail(void);

#ifdef __cplusplus
}
#endif

#endif /* ACTION_DISPLAY_H */
