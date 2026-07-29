#ifndef LEVEL_DISPLAY_H
#define LEVEL_DISPLAY_H

#include "lvgl.h"

// 初始化level_display组件
void level_display_init(lv_obj_t *parent, int screen_width, int screen_height);

// 更新level_display显示（根据roll角度变色）
void level_display_update(float roll);

// 清理level_display组件
void level_display_cleanup(void);

#endif // LEVEL_DISPLAY_H
