#include "angle_display.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "memory_diag.h"  // 添加内存诊断工具
#include <math.h>
#include <stdio.h>

static lv_obj_t *angle_disp_canvas = NULL;
static lv_obj_t *angle_label = NULL;
static int disp_width = 240;
static int disp_height = 240;
#define MAX_PITCH 45.0f
#define DIAMOND_AREA_HEIGHT 40  // 正方形区域高度保护，调整为40px以匹配新位置
#define BOTTOM_LABEL_AREA_HEIGHT 40  // 底部标签区域高度保护，与ruler_bottom保持一致

static lv_color_t *cbuf = NULL;
static lv_color_t *ruler_bg_buf = NULL;
static bool ruler_cached = false; // 添加缓存状态标志

// 防抖动相关变量
static int last_line_x0 = -1, last_line_y0 = -1, last_line_x1 = -1, last_line_y1 = -1;
static float last_filtered_pitch = 0.0f, last_filtered_roll = 0.0f;
static bool first_update = true;

// 防抖动阈值（像素）
#define PIXEL_JITTER_THRESHOLD 2
// 角度变化阈值（度）
#define ANGLE_CHANGE_THRESHOLD 0.4f

// 手动绘制线段的辅助函数，避免LVGL内存问题
static void draw_manual_line(lv_color_t *buf, int width, int height, int x0, int y0, int x1, int y1, lv_color_t color) {
    if (!buf) return;
    
    // Bresenham算法绘制线段
    int dx = abs(x1 - x0);
    int dy = abs(y1 - y0);
    int sx = (x0 < x1) ? 1 : -1;
    int sy = (y0 < y1) ? 1 : -1;
    int err = dx - dy;
    int x = x0, y = y0;
    
    while (true) {
        // 边界检查
        if (x >= 0 && x < width && y >= 0 && y < height) {
            // 绘制3x3的粗线，模拟线宽
            for (int ox = -1; ox <= 1; ox++) {
                for (int oy = -1; oy <= 1; oy++) {
                    int px = x + ox;
                    int py = y + oy;
                    if (px >= 0 && px < width && py >= 0 && py < height) {
                        buf[py * width + px] = color;
                    }
                }
            }
        }
        
        if (x == x1 && y == y1) break;
        
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

static void angle_disp_canvas_event_cb(lv_event_t *e) {
    ESP_LOGI("angle_display", "event_cb code=%d", lv_event_get_code(e));
    if(lv_event_get_code(e) == LV_EVENT_CLICKED) {
        static int display_mode = 0;
        display_mode = !display_mode;
        lv_obj_t *canvas = lv_event_get_target(e);
        if (!canvas) {
            ESP_LOGE("angle_display", "event_cb: canvas is NULL!");
            return;
        }
        lv_obj_set_user_data(canvas, (void*)(intptr_t)display_mode);
        lv_obj_invalidate(canvas);
    }
}

void draw_pitch_ruler_on_canvas(void) {
    // 绘制左右标尺（每5度一个刻度，-45~+45°，美观为主）
    int ruler_left_x = 10;
    int ruler_right_x = disp_width - 10;
    int ruler_top = 50;  // 标尺顶部边界
    int ruler_bottom = disp_height - 40;
    int ruler_len = ruler_bottom - ruler_top;
    int tick_len_long = 16;
    int tick_len_short = 8;
    int font_offset = 4;
    for(int deg = -45; deg <= 45; deg += 5) {
        float rel = (float)(deg + 45) / 90.0f; // 0~1
        int y = ruler_bottom - (int)(rel * ruler_len + 0.5f);
        int is_major = (deg % 10 == 0);
        // 左侧刻度线
        lv_draw_line_dsc_t tick_dsc;
        lv_draw_line_dsc_init(&tick_dsc);
        tick_dsc.color = lv_color_hex(0x888888);
        tick_dsc.width = is_major ? 3 : 2;
        lv_point_t tick_left[2] = {
            {ruler_left_x, y},
            {ruler_left_x + (is_major ? tick_len_long : tick_len_short), y}
        };
        lv_canvas_draw_line(angle_disp_canvas, tick_left, 2, &tick_dsc);
        // 右侧刻度线
        lv_point_t tick_right[2] = {
            {ruler_right_x, y},
            {ruler_right_x - (is_major ? tick_len_long : tick_len_short), y}
        };
        lv_canvas_draw_line(angle_disp_canvas, tick_right, 2, &tick_dsc);
        // 文字（仅主刻度且不与保护区冲突）
        if(is_major && y >= ruler_top + 16) {  // 确保文字不会绘制到保护区
            char label[8];
            snprintf(label, sizeof(label), "%+d", deg);
            lv_draw_label_dsc_t label_dsc;
            lv_draw_label_dsc_init(&label_dsc);
            label_dsc.color = lv_color_hex(0xAAAAAA);
            label_dsc.font = &lv_font_montserrat_12;  // 关键修复：设置字体
            
            // 计算文字坐标并进行边界检查
            lv_coord_t left_text_x = ruler_left_x + tick_len_long + font_offset;
            lv_coord_t right_text_x = ruler_right_x - tick_len_long - 32 - font_offset;
            lv_coord_t text_y = y - 8;
            
            // 左侧文字边界检查
            if (left_text_x >= 0 && left_text_x + 32 <= disp_width && text_y >= 0 && text_y + 16 <= disp_height) {
                lv_canvas_draw_text(angle_disp_canvas, left_text_x, text_y, 32, &label_dsc, label);
            }
            // 右侧文字边界检查
            if (right_text_x >= 0 && right_text_x + 32 <= disp_width && text_y >= 0 && text_y + 16 <= disp_height) {
                lv_canvas_draw_text(angle_disp_canvas, right_text_x, text_y, 32, &label_dsc, label);
            }
        }
    }
}

// 在Canvas上绘制顶部正方形指示器
void draw_top_diamond_on_canvas(float roll_angle) {
    if (!angle_disp_canvas) {
        return;
    }
    
    // 根据roll角度确定显示方块数量
    bool show_squares[5] = {false, false, false, false, false};  // 对应5个方块位置
    
    if (fabsf(roll_angle) <= 0.4f) {
        // 显示所有5个方块
        for(int i = 0; i < 5; i++) {
            show_squares[i] = true;
        }
    } else if (fabsf(roll_angle) <= 0.8f) {
        // 显示除中间外的4个方块（索引0,1,3,4）
        show_squares[0] = true;  // 左侧第2个
        show_squares[1] = true;  // 左侧第1个
        show_squares[2] = false; // 中间方块（不显示）
        show_squares[3] = true;  // 右侧第1个
        show_squares[4] = true;  // 右侧第2个
    } else if (fabsf(roll_angle) <= 1.6f) {
        // 显示最边上2个方块（索引0,4）
        show_squares[0] = true;  // 左侧第2个
        show_squares[1] = false; // 左侧第1个
        show_squares[2] = false; // 中间方块
        show_squares[3] = false; // 右侧第1个
        show_squares[4] = true;  // 右侧第2个
    } else {
        // roll角度超过1.6度，不显示任何方块
        return;
    }
    
    // 正方形参数 - 贴着屏幕顶部，无空缺
    int square_cy = 15;  // Y坐标 = 正方形半径15px，使顶部贴着屏幕
    int square_half_size = 15;  // 正方形半边长 (30px的一半)
    
    // 5个正方形的中心X坐标，平均分布
    int square_centers[5] = {
        disp_width / 2 - 80,  // 左侧第2个
        disp_width / 2 - 40,  // 左侧第1个
        disp_width / 2,       // 中心正方形
        disp_width / 2 + 40,  // 右侧第1个
        disp_width / 2 + 80   // 右侧第2个
    };
    
    // 绘制符合条件的正方形
    for(int s = 0; s < 5; s++) {
        if (!show_squares[s]) {
            continue;  // 跳过不需要显示的方块
        }
        
        int square_cx = square_centers[s];
        
        // 边界检查，确保正方形不会超出屏幕
        if(square_cx - square_half_size < 0 || square_cx + square_half_size >= disp_width) {
            continue;  // 跳过超出边界的正方形
        }
        
        // 绘制实心绿色正方形
        lv_draw_rect_dsc_t square_dsc;
        lv_draw_rect_dsc_init(&square_dsc);
        square_dsc.bg_color = lv_color_hex(0x00FF00);  // 绿色填充
        square_dsc.bg_opa = LV_OPA_COVER;  // 完全不透明
        square_dsc.border_width = 0;  // 无边框
        
        // 计算矩形参数
        lv_coord_t rect_x = square_cx - square_half_size;
        lv_coord_t rect_y = square_cy - square_half_size;
        lv_coord_t rect_w = square_half_size * 2;  // 宽度 = 24px
        lv_coord_t rect_h = square_half_size * 2;  // 高度 = 24px
        
        // 绘制实心矩形
        lv_canvas_draw_rect(angle_disp_canvas, rect_x, rect_y, rect_w, rect_h, &square_dsc);
    }
}

void angle_display_init(lv_obj_t *parent, int screen_width, int screen_height) {
    disp_width = screen_width;
    disp_height = screen_height;
    
    // 清理之前的资源
    if(angle_disp_canvas) {
        lv_obj_del(angle_disp_canvas);
        angle_disp_canvas = NULL;
    }
    if(cbuf) {
        heap_caps_free(cbuf);  // 修复：使用正确的释放函数
        cbuf = NULL;
    }
    if(ruler_bg_buf) {
        heap_caps_free(ruler_bg_buf);  // 修复：使用正确的释放函数
        ruler_bg_buf = NULL;
    }
    if(angle_label) {
        lv_obj_del(angle_label);
        angle_label = NULL;
    }
    ruler_cached = false;
    
    // 重置防抖动状态
    last_line_x0 = last_line_y0 = last_line_x1 = last_line_y1 = -1;
    last_filtered_pitch = last_filtered_roll = 0.0f;
    first_update = true;
    
    // 计算缓存大小
    size_t buf_size = sizeof(lv_color_t) * disp_width * disp_height;
    ESP_LOGI("angle_display", "Requested canvas buffer: %u bytes (%dx%d)", 
             (unsigned)buf_size, disp_width, disp_height);
    
    // 检查内存是否足够
    if (!memory_diag_check_available(buf_size, true)) {
        ESP_LOGE("angle_display", "Insufficient memory for canvas buffer");
        return;
    }
    
    // 使用智能分配主缓存（优先PSRAM，允许降级）
    cbuf = (lv_color_t*)memory_diag_smart_malloc(buf_size, true, true);
    if (cbuf) {
        ESP_LOGI("angle_display", "Canvas buffer allocated: addr=%p size=%u type=%s", 
                 cbuf, (unsigned)buf_size, memory_diag_get_type_str(cbuf));
    } else {
        ESP_LOGE("angle_display", "Canvas buffer allocation failed! Trying reduced size...");
        // 尝试分配一半大小的缓存 - 降低分辨率而不是仅减少宽度
        int reduced_width = disp_width / 2;
        int reduced_height = disp_height / 2;
        size_t reduced_size = sizeof(lv_color_t) * reduced_width * reduced_height;
        
        cbuf = (lv_color_t*)memory_diag_smart_malloc(reduced_size, false, true);
        if (cbuf) {
            ESP_LOGW("angle_display", "Canvas buffer allocated with reduced size: addr=%p size=%u type=%s (%dx%d)", 
                     cbuf, (unsigned)reduced_size, memory_diag_get_type_str(cbuf), reduced_width, reduced_height);
            // 更新显示尺寸和缓存大小
            disp_width = reduced_width;
            disp_height = reduced_height;
            buf_size = reduced_size;
        }
    }
    
    if (!cbuf) {
        ESP_LOGE("angle_display", "cbuf malloc completely failed!");
        return;
    }
    
    // 验证缓冲区完整性
    ESP_LOGI("angle_display", "Final canvas configuration: %dx%d, buffer size: %u bytes", 
             disp_width, disp_height, (unsigned)buf_size);
    
    // 创建画布前清零缓冲区
    memset(cbuf, 0, buf_size);
    
    // 创建画布
    angle_disp_canvas = lv_canvas_create(parent);
    if (!angle_disp_canvas) {
        ESP_LOGE("angle_display", "Failed to create canvas object!");
        return;
    }
    
    lv_obj_set_size(angle_disp_canvas, disp_width, disp_height);
    lv_obj_align(angle_disp_canvas, LV_ALIGN_CENTER, 0, 0);
    
    // 设置缓冲区前验证参数
    if (disp_width <= 0 || disp_height <= 0) {
        ESP_LOGE("angle_display", "Invalid canvas dimensions: %dx%d", disp_width, disp_height);
        return;
    }
    
    lv_canvas_set_buffer(angle_disp_canvas, cbuf, disp_width, disp_height, LV_IMG_CF_TRUE_COLOR);
    lv_canvas_fill_bg(angle_disp_canvas, lv_color_black(), LV_OPA_COVER);
    
    // 绘制标尺
    draw_pitch_ruler_on_canvas();
    
    // 使用智能分配标尺缓存（优先PSRAM，允许降级）
    ESP_LOGI("angle_display", "Attempting ruler cache allocation: %u bytes", (unsigned)buf_size);
    ruler_bg_buf = (lv_color_t*)memory_diag_smart_malloc(buf_size, true, true);
    
    if (ruler_bg_buf) {
        ESP_LOGI("angle_display", "Ruler cache allocated: addr=%p size=%u type=%s", 
                 ruler_bg_buf, (unsigned)buf_size, memory_diag_get_type_str(ruler_bg_buf));
        memcpy(ruler_bg_buf, cbuf, buf_size);
        ruler_cached = true;
        ESP_LOGI("angle_display", "Ruler cache created successfully");
    } else {
        ESP_LOGW("angle_display", "Failed to allocate ruler cache, will redraw each frame");
        ruler_cached = false;
        // 不是致命错误，可以继续运行，只是性能较差
    }
    
    // 注册事件回调
    lv_obj_add_event_cb(angle_disp_canvas, angle_disp_canvas_event_cb, LV_EVENT_CLICKED, NULL);
    
    // 创建底部角度显示label (移除安全间距，贴底显示，放大字体)
    angle_label = lv_label_create(parent);
    assert(angle_label);
    lv_obj_set_style_text_color(angle_label, lv_color_white(), 0);
    // 使用16号字体以适配增加的8px空间 (从默认14号放大到16号)
    lv_obj_set_style_text_font(angle_label, &lv_font_montserrat_16, 0);
    lv_obj_align(angle_label, LV_ALIGN_BOTTOM_MID, 0, 0);
    
    // 设置初始文本，避免显示默认的"TEXT"占位符
    lv_label_set_text(angle_label, "Pitch: 0.0°  Roll: 0.0°");
}

void angle_display_update(float pitch, float roll) {
    // 多重检查，防止并发访问已删除的对象
    if(!angle_disp_canvas || !cbuf) {
        ESP_LOGE("angle_display", "update called but canvas or cbuf is NULL!");
        return;
    }
    
    // 验证Canvas对象的有效性
    if (!lv_obj_is_valid(angle_disp_canvas)) {
        ESP_LOGE("angle_display", "Canvas object is invalid!");
        angle_disp_canvas = NULL; // 清除无效引用
        return;
    }
    
    // 再次检查，防止在检查过程中对象被删除
    if(!angle_disp_canvas || !cbuf) {
        ESP_LOGE("angle_display", "Objects became invalid during check!");
        return;
    }
    
    // 验证显示尺寸
    if (disp_width <= 0 || disp_height <= 0) {
        ESP_LOGE("angle_display", "Invalid display dimensions: %dx%d", disp_width, disp_height);
        return;
    }
    
    int display_mode = (int)(intptr_t)lv_obj_get_user_data(angle_disp_canvas);
    
    if(display_mode == 1) {
        // 颜色模式：清空画布并填充颜色
        lv_canvas_fill_bg(angle_disp_canvas, lv_color_black(), LV_OPA_COVER);
        if(fabsf(roll) <= 0.4f) {
            lv_canvas_fill_bg(angle_disp_canvas, lv_color_hex(0x00FF00), LV_OPA_COVER);
        } else {
            lv_canvas_fill_bg(angle_disp_canvas, lv_color_hex(0xFF0000), LV_OPA_COVER);
        }
        if(angle_label) {
            lv_label_set_text(angle_label, "颜色模式: 点击切换");
        }
        lv_obj_invalidate(angle_disp_canvas);
        return;
    }
    
    // 角度变化检测，避免微小变化导致的视觉抖动
    bool angle_changed = first_update || 
                        fabsf(pitch - last_filtered_pitch) > ANGLE_CHANGE_THRESHOLD ||
                        fabsf(roll - last_filtered_roll) > ANGLE_CHANGE_THRESHOLD;
    
    // 计算新的线段位置（断开式：中间空隙，两边各有短线段）
    // 修复坐标系：使用标尺的实际范围进行映射
    int ruler_top = 50;  // 与draw_pitch_ruler_on_canvas()中的值保持一致
    int ruler_bottom = disp_height - 40;  // 与draw_pitch_ruler_on_canvas()中的值保持一致
    int ruler_center = (ruler_top + ruler_bottom) / 2;  // 标尺中心点
    int ruler_range = ruler_bottom - ruler_top;  // 标尺有效范围
    
    float y_offset = -(ruler_range/2) * (pitch / MAX_PITCH);  // 使用标尺范围而非屏幕高度
    float theta = -roll * 3.1415926f / 180.0f;
    float segment_len = 79.0f; // 每段线段长度（延伸到标尺长刻度线）
    float gap_len = 120.0f;    // 中间空隙长度（增大4倍：30→120像素）
    int cx = disp_width / 2;
    int cy = ruler_center + (int)y_offset;  // 使用标尺中心作为基准
    
    // 左侧线段
    int left_x0 = (int)(cx - (gap_len/2 + segment_len) * cosf(theta));
    int left_y0 = (int)(cy - (gap_len/2 + segment_len) * sinf(theta));
    int left_x1 = (int)(cx - gap_len/2 * cosf(theta));
    int left_y1 = (int)(cy - gap_len/2 * sinf(theta));
    
    // 右侧线段
    int right_x0 = (int)(cx + gap_len/2 * cosf(theta));
    int right_y0 = (int)(cy + gap_len/2 * sinf(theta));
    int right_x1 = (int)(cx + (gap_len/2 + segment_len) * cosf(theta));
    int right_y1 = (int)(cy + (gap_len/2 + segment_len) * sinf(theta));
    
    // 边界保护 - 左侧线段（包含顶部菱形区域和底部标签区域保护）
    if(left_x0 < 0) left_x0 = 0;
    if(left_x0 >= disp_width) left_x0 = disp_width - 1;
    if(left_x1 < 0) left_x1 = 0;
    if(left_x1 >= disp_width) left_x1 = disp_width - 1;
    if(left_y0 < DIAMOND_AREA_HEIGHT) left_y0 = DIAMOND_AREA_HEIGHT;  // 避免进入顶部菱形区域
    if(left_y0 >= disp_height - BOTTOM_LABEL_AREA_HEIGHT) left_y0 = disp_height - BOTTOM_LABEL_AREA_HEIGHT - 1;  // 避免进入底部标签区域
    if(left_y1 < DIAMOND_AREA_HEIGHT) left_y1 = DIAMOND_AREA_HEIGHT;  // 避免进入顶部菱形区域
    if(left_y1 >= disp_height - BOTTOM_LABEL_AREA_HEIGHT) left_y1 = disp_height - BOTTOM_LABEL_AREA_HEIGHT - 1;  // 避免进入底部标签区域
    
    // 边界保护 - 右侧线段（包含顶部菱形区域和底部标签区域保护）
    if(right_x0 < 0) right_x0 = 0;
    if(right_x0 >= disp_width) right_x0 = disp_width - 1;
    if(right_x1 < 0) right_x1 = 0;
    if(right_x1 >= disp_width) right_x1 = disp_width - 1;
    if(right_y0 < DIAMOND_AREA_HEIGHT) right_y0 = DIAMOND_AREA_HEIGHT;  // 避免进入顶部菱形区域
    if(right_y0 >= disp_height - BOTTOM_LABEL_AREA_HEIGHT) right_y0 = disp_height - BOTTOM_LABEL_AREA_HEIGHT - 1;  // 避免进入底部标签区域
    if(right_y1 < DIAMOND_AREA_HEIGHT) right_y1 = DIAMOND_AREA_HEIGHT;  // 避免进入顶部菱形区域
    if(right_y1 >= disp_height - BOTTOM_LABEL_AREA_HEIGHT) right_y1 = disp_height - BOTTOM_LABEL_AREA_HEIGHT - 1;  // 避免进入底部标签区域
    
    // 像素级防抖动检测（检查所有4个端点）
    bool pixel_changed = first_update ||
                        abs(left_x0 - last_line_x0) > PIXEL_JITTER_THRESHOLD ||
                        abs(left_y0 - last_line_y0) > PIXEL_JITTER_THRESHOLD ||
                        abs(right_x1 - last_line_x1) > PIXEL_JITTER_THRESHOLD ||
                        abs(right_y1 - last_line_y1) > PIXEL_JITTER_THRESHOLD;
    
    // 只有当角度变化足够大或像素位置变化足够大时才重绘
    if (angle_changed || pixel_changed) {
        // 线段模式：使用缓存或降级方案
        if(ruler_cached && ruler_bg_buf && cbuf) {
            // 方案1：有缓存，安全恢复
            // 重要：确保复制大小不超过任何一个缓冲区的实际大小
            size_t expected_size = sizeof(lv_color_t) * disp_width * disp_height;
            size_t actual_cbuf_size = heap_caps_get_allocated_size(cbuf);
            size_t actual_ruler_size = heap_caps_get_allocated_size(ruler_bg_buf);
            size_t safe_copy_size = (expected_size < actual_cbuf_size && expected_size < actual_ruler_size) ? 
                                   expected_size : 
                                   (actual_cbuf_size < actual_ruler_size ? actual_cbuf_size : actual_ruler_size);
            
            ESP_LOGD("angle_display", "Safe copy: expected=%u cbuf=%u ruler=%u using=%u", 
                     (unsigned)expected_size, (unsigned)actual_cbuf_size, 
                     (unsigned)actual_ruler_size, (unsigned)safe_copy_size);
            
            memcpy(cbuf, ruler_bg_buf, safe_copy_size);
            
            // 重新设置缓冲区确保LVGL知道缓冲区已更新
            lv_canvas_set_buffer(angle_disp_canvas, cbuf, disp_width, disp_height, LV_IMG_CF_TRUE_COLOR);
        } else {
            // 方案2：无缓存，重新绘制（性能较差但功能正常）
            lv_canvas_fill_bg(angle_disp_canvas, lv_color_black(), LV_OPA_COVER);
            draw_pitch_ruler_on_canvas(); // 每帧重绘标尺
            static int warn_count = 0;
            if (warn_count < 5) { // 只警告前5次，避免日志过多
                ESP_LOGW("angle_display", "No ruler cache, redrawing each frame (performance impact)");
                warn_count++;
            }
        }
        
        // 绘制顶部正方形指示器（根据roll角度显示不同数量）
        draw_top_diamond_on_canvas(roll);
        
        // 手动绘制动态白线，避免使用LVGL的lv_canvas_draw_line
        if (left_x0 >= 0 && left_x0 < disp_width && left_y0 >= 0 && left_y0 < disp_height &&
            left_x1 >= 0 && left_x1 < disp_width && left_y1 >= 0 && left_y1 < disp_height &&
            right_x0 >= 0 && right_x0 < disp_width && right_y0 >= 0 && right_y0 < disp_height &&
            right_x1 >= 0 && right_x1 < disp_width && right_y1 >= 0 && right_y1 < disp_height) {
            
            // 手动绘制左侧线段
            draw_manual_line(cbuf, disp_width, disp_height, left_x0, left_y0, left_x1, left_y1, lv_color_white());
            
            // 手动绘制右侧线段
            draw_manual_line(cbuf, disp_width, disp_height, right_x0, right_y0, right_x1, right_y1, lv_color_white());
            
        } else {
            ESP_LOGW("angle_display", "Line coordinates out of bounds, skipping draw");
        }
        
        // 绘制绿色菱形指示器（当roll ≤ ±0.4度时）
        if(fabsf(roll) <= 0.4f) {
            // 计算菱形中心点（两个线段之间的中心）
            int diamond_cx = cx;
            int diamond_cy = cy;
            
            // 菱形大小
            int diamond_size = 8;  // 菱形半径
            
            // 边界保护 - 确保菱形不会绘制到保护区域
            if(diamond_cy - diamond_size >= DIAMOND_AREA_HEIGHT && 
               diamond_cy + diamond_size < disp_height - BOTTOM_LABEL_AREA_HEIGHT) {
            
                // 菱形四个顶点
                lv_point_t diamond_points[5] = {
                    {diamond_cx, diamond_cy - diamond_size},  // 上
                    {diamond_cx + diamond_size, diamond_cy},  // 右
                    {diamond_cx, diamond_cy + diamond_size},  // 下
                    {diamond_cx - diamond_size, diamond_cy},  // 左
                    {diamond_cx, diamond_cy - diamond_size}   // 回到起点闭合
                };
                
                // 绘制绿色菱形
                lv_draw_line_dsc_t diamond_dsc;
                lv_draw_line_dsc_init(&diamond_dsc);
                diamond_dsc.color = lv_color_hex(0x00FF00);  // 绿色
                diamond_dsc.width = 3;
                
                // 绘制菱形的四条边
                for(int i = 0; i < 4; i++) {
                    lv_point_t diamond_edge[2] = {
                        diamond_points[i],
                        diamond_points[i + 1]
                    };
                    lv_canvas_draw_line(angle_disp_canvas, diamond_edge, 2, &diamond_dsc);
                }
            }
        }
        
        // 刷新画布
        lv_obj_invalidate(angle_disp_canvas);
        
        // 更新记录的位置和角度（保存关键端点）
        last_line_x0 = left_x0;
        last_line_y0 = left_y0;
        last_line_x1 = right_x1;
        last_line_y1 = right_y1;
        last_filtered_pitch = pitch;
        last_filtered_roll = roll;
        first_update = false;
    }
    
    // 角度显示标签始终更新（但限制更新频率）
    static int label_update_counter = 0;
    if (++label_update_counter >= 5) { // 每5次更新一次标签
        label_update_counter = 0;
        if(angle_label) {
            char buf[64];
            if (ruler_cached) {
                snprintf(buf, sizeof(buf), "Pitch: %.1f°  Roll: %.1f°", pitch, roll);
            } else {
                snprintf(buf, sizeof(buf), "Pitch: %.1f°  Roll: %.1f° [No Cache]", pitch, roll);
            }
            lv_label_set_text(angle_label, buf);
        }
    }
}

void angle_display_cleanup(void) {
    printf("angle_display_cleanup: starting cleanup\n");
    
    // 删除LVGL对象
    if (angle_disp_canvas) {
        lv_obj_del(angle_disp_canvas);
        angle_disp_canvas = NULL;
    }
    if (angle_label) {
        lv_obj_del(angle_label);
        angle_label = NULL;
    }
    
    // 释放内存缓冲区
    if (cbuf) {
        heap_caps_free(cbuf);
        cbuf = NULL;
    }
    if (ruler_bg_buf) {
        heap_caps_free(ruler_bg_buf);
        ruler_bg_buf = NULL;
    }
    
    ruler_cached = false;
    
    printf("angle_display_cleanup: completed\n");
}
