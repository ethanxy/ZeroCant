#ifndef ANGLE_DISPLAY_H
#define ANGLE_DISPLAY_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

void angle_display_init(lv_obj_t *parent, int screen_width, int screen_height);
void angle_display_update(float pitch, float roll);
void angle_display_cleanup(void);  // 添加cleanup函数

#ifdef __cplusplus
}
#endif

#endif // ANGLE_DISPLAY_H
