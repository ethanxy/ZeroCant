#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "esp_timer.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_system.h" // 添加用于重启原因检测
#include "esp_heap_caps.h"
#include "nvs_flash.h"  // 添加NVS支持
#include "nvs.h"        // 添加NVS操作

#include "lvgl.h"
#include "lv_demos.h"
#include "esp_lcd_sh8601.h"
#include "i2c_bsp.h"
#include "touch_bsp.h"
#include "user_app.h"
#include "memory_diag.h" // 添加内存诊断工具
#include "amoled_burn_protection.h" // 添加AMOLED防烧屏保护
#include "adc_bsp.h" // 添加ADC BSP功能
#include "ui_state_manager.h" // 添加UI状态管理器
#include "angle_display.h" // 角度显示组件
#include "level_display.h" // 水平仪显示组件
#include "laser.h" // 激光组件
#include "settings.h" // 设置组件

static const char *TAG = "example";
static SemaphoreHandle_t lvgl_mux = NULL;

#define LCD_HOST  SPI2_HOST

#if CONFIG_LV_COLOR_DEPTH == 32
#define LCD_BIT_PER_PIXEL       (24)
#elif CONFIG_LV_COLOR_DEPTH == 16
#define LCD_BIT_PER_PIXEL       (16)
#endif

#define EXAMPLE_LCD_BK_LIGHT_ON_LEVEL  1
#define EXAMPLE_LCD_BK_LIGHT_OFF_LEVEL !EXAMPLE_LCD_BK_LIGHT_ON_LEVEL
#define EXAMPLE_PIN_NUM_LCD_CS            (GPIO_NUM_9)
#define EXAMPLE_PIN_NUM_LCD_PCLK          (GPIO_NUM_10) 
#define EXAMPLE_PIN_NUM_LCD_DATA0         (GPIO_NUM_11)
#define EXAMPLE_PIN_NUM_LCD_DATA1         (GPIO_NUM_12)
#define EXAMPLE_PIN_NUM_LCD_DATA2         (GPIO_NUM_13)
#define EXAMPLE_PIN_NUM_LCD_DATA3         (GPIO_NUM_14)
#define EXAMPLE_PIN_NUM_LCD_RST           (GPIO_NUM_21)
#define EXAMPLE_PIN_NUM_BK_LIGHT          (-1)

// The pixel number in horizontal and vertical
#define EXAMPLE_LCD_H_RES              280 
#define EXAMPLE_LCD_V_RES              456 

#define EXAMPLE_USE_TOUCH               1

#define EXAMPLE_LVGL_BUF_HEIGHT        (EXAMPLE_LCD_V_RES / 4)
#define EXAMPLE_LVGL_TICK_PERIOD_MS    2
#define EXAMPLE_LVGL_TASK_MAX_DELAY_MS 500
#define EXAMPLE_LVGL_TASK_MIN_DELAY_MS 1
#define EXAMPLE_LVGL_TASK_STACK_SIZE   (12 * 1024)
#define EXAMPLE_LVGL_TASK_PRIORITY     5

static esp_lcd_panel_io_handle_t amoled_bl_handle = NULL;
static uint8_t current_brightness = 179; // 默认70%亮度
void setBrightnes(uint8_t brig);
void shutdownDisplay(void); // 添加关闭显示函数声明
void setBrightnessTemporary(uint8_t brig); // 临时设置亮度，不保存到NVS
uint8_t getBrightness(void);
uint8_t loadSavedBrightness(void);  // 公共函数用于加载保存的亮度

// NVS相关常量
#define NVS_NAMESPACE "settings"
#define NVS_BRIGHTNESS_KEY "brightness"

// 从NVS读取保存的亮度值
static uint8_t load_brightness_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    uint8_t brightness = 179; // 默认70%亮度
    
    // 尝试打开NVS命名空间
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace '%s': %s", NVS_NAMESPACE, esp_err_to_name(err));
        return brightness;
    }
    
    // 尝试读取亮度值
    size_t required_size = sizeof(brightness);
    err = nvs_get_blob(nvs_handle, NVS_BRIGHTNESS_KEY, &brightness, &required_size);
    
    if (err == ESP_OK) {
        // 验证亮度值的合理性（uint8_t范围是0-255，只需检查下限）
        if (brightness >= 26) { // 10%-100%的有效范围，上限自动满足
            ESP_LOGI(TAG, "Loaded brightness from NVS: %d (%.1f%%)", brightness, (brightness * 100.0f) / 255.0f);
        } else {
            ESP_LOGW(TAG, "Invalid brightness value in NVS: %d, using default", brightness);
            brightness = 179; // 恢复默认值
        }
    } else if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved brightness found, using default: %d (%.1f%%)", brightness, (brightness * 100.0f) / 255.0f);
    } else {
        ESP_LOGW(TAG, "Error reading brightness from NVS: %s", esp_err_to_name(err));
    }
    
    nvs_close(nvs_handle);
    return brightness;
}

// 将亮度值保存到NVS
static esp_err_t save_brightness_to_nvs(uint8_t brightness) {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    
    // 验证亮度值的合理性（uint8_t范围是0-255，只需检查下限）
    if (brightness < 26) {
        ESP_LOGW(TAG, "Invalid brightness value for NVS save: %d", brightness);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 尝试打开NVS命名空间
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to open NVS namespace '%s' for writing: %s", NVS_NAMESPACE, esp_err_to_name(err));
        return err;
    }
    
    // 尝试保存亮度值
    err = nvs_set_blob(nvs_handle, NVS_BRIGHTNESS_KEY, &brightness, sizeof(brightness));
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to save brightness to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    // 尝试提交更改
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Failed to commit brightness to NVS: %s", esp_err_to_name(err));
        nvs_close(nvs_handle);
        return err;
    }
    
    nvs_close(nvs_handle);
    ESP_LOGI(TAG, "Successfully saved brightness to NVS: %d (%.1f%%)", brightness, (brightness * 100.0f) / 255.0f);
    return ESP_OK;
}

static const sh8601_lcd_init_cmd_t lcd_init_cmds[] = {
    {0x11, (uint8_t []){0x00}, 0, 80},   
    {0xC4, (uint8_t []){0x80}, 1, 0},
   
    {0x35, (uint8_t []){0x00}, 1, 0},

    {0x53, (uint8_t []){0x20}, 1, 1},
    {0x63, (uint8_t []){0xFF}, 1, 1},
    {0x51, (uint8_t []){0x00}, 1, 1},    // 初始亮度设为0，避免白屏

    {0x29, (uint8_t []){0x00}, 0, 10},   // 显示开启

    // 先填充黑色像素数据来覆盖白屏
    {0x2A, (uint8_t []){0x00, 0x14, 0x01, 0x2B}, 4, 0},  // 设置列地址
    {0x2B, (uint8_t []){0x00, 0x00, 0x01, 0xC7}, 4, 0},  // 设置行地址
    
    {0x51, (uint8_t []){0x1A}, 1, 0},    // 设置较低的初始亮度 (26/255 ≈ 10%)
};

static bool example_notify_lvgl_flush_ready(esp_lcd_panel_io_handle_t panel_io, esp_lcd_panel_io_event_data_t *edata, void *user_ctx)
{
    lv_disp_drv_t *disp_driver = (lv_disp_drv_t *)user_ctx;
    lv_disp_flush_ready(disp_driver);
    return false;
}

static lv_color_t *s_burn_flush_scratch = NULL;
static size_t s_burn_flush_scratch_px = 0;
static volatile bool s_burn_need_invalidate = false;

static void example_lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    if (!area || !color_map) {
        ESP_LOGE(TAG, "LVGL flush_cb: area or color_map is NULL! area=%p color_map=%p", area, color_map);
        lv_disp_flush_ready(drv);
        return;
    }
    if (area->x1 < 0 || area->x2 >= EXAMPLE_LCD_H_RES || area->y1 < 0 || area->y2 >= EXAMPLE_LCD_V_RES) {
        ESP_LOGE(TAG, "LVGL flush_cb: area out of bounds! x1=%d x2=%d y1=%d y2=%d",
                 area->x1, area->x2, area->y1, area->y2);
        lv_disp_flush_ready(drv);
        return;
    }

    esp_lcd_panel_handle_t panel_handle = (esp_lcd_panel_handle_t)drv->user_data;
    int8_t burn_ox = 0;
    int8_t burn_oy = 0;
    amoled_burn_protection_get_offset(&burn_ox, &burn_oy);

    const int src_w = area->x2 - area->x1 + 1;
    const int src_h = area->y2 - area->y1 + 1;
    int dst_x = area->x1 + burn_ox;
    int dst_y = area->y1 + burn_oy;
    int copy_w = src_w;
    int copy_h = src_h;
    int src_x0 = 0;
    int src_y0 = 0;

    if (burn_ox == 0 && burn_oy == 0) {
#if LCD_BIT_PER_PIXEL == 24
        uint8_t *to = (uint8_t *)color_map;
        uint8_t temp = color_map[0].ch.blue;
        uint16_t pixel_num = (uint16_t)(src_w * src_h);
        *to++ = color_map[0].ch.red;
        *to++ = color_map[0].ch.green;
        *to++ = temp;
        for (int i = 1; i < pixel_num; i++) {
            *to++ = color_map[i].ch.red;
            *to++ = color_map[i].ch.green;
            *to++ = color_map[i].ch.blue;
        }
#endif
        esp_lcd_panel_draw_bitmap(panel_handle,
                                  area->x1 + 0x14, area->y1,
                                  area->x2 + 0x14 + 1, area->y2 + 1,
                                  color_map);
        return;
    }

    if (dst_x < 0) {
        src_x0 = -dst_x;
        copy_w -= src_x0;
        dst_x = 0;
    }
    if (dst_y < 0) {
        src_y0 = -dst_y;
        copy_h -= src_y0;
        dst_y = 0;
    }
    if (dst_x + copy_w > EXAMPLE_LCD_H_RES) {
        copy_w = EXAMPLE_LCD_H_RES - dst_x;
    }
    if (dst_y + copy_h > EXAMPLE_LCD_V_RES) {
        copy_h = EXAMPLE_LCD_V_RES - dst_y;
    }

    /*
     * 偶数对齐优先扩边重叠，禁止把 1/4 缓冲条带裁短，否则接缝会留下黑线/残影。
     * 防烧屏偏移已限制为偶数，多数块可整块平移。
     */
    if ((dst_x & 1) && dst_x > 0 && src_x0 > 0) {
        dst_x--;
        src_x0--;
        copy_w++;
    } else if (dst_x & 1) {
        dst_x++;
        src_x0++;
        copy_w--;
    }
    if ((dst_y & 1) && dst_y > 0 && src_y0 > 0) {
        dst_y--;
        src_y0--;
        copy_h++;
    } else if (dst_y & 1) {
        dst_y++;
        src_y0++;
        copy_h--;
    }
    if ((copy_w & 1) && dst_x + copy_w < EXAMPLE_LCD_H_RES && src_x0 + copy_w < src_w) {
        copy_w++;
    } else if (copy_w & 1) {
        copy_w--;
    }
    if ((copy_h & 1) && dst_y + copy_h < EXAMPLE_LCD_V_RES && src_y0 + copy_h < src_h) {
        copy_h++;
    } else if (copy_h & 1) {
        copy_h--;
    }

    if (copy_w <= 0 || copy_h <= 0 ||
        src_x0 < 0 || src_y0 < 0 ||
        src_x0 + copy_w > src_w || src_y0 + copy_h > src_h) {
        lv_disp_flush_ready(drv);
        return;
    }

    /*
     * 右移后左侧会留旧像素；左移后右侧同理。
     * 在同一块 DMA 缓冲里补黑边，再一次 draw，避免接缝残点。
     */
    int pad_left = 0;
    int pad_right = 0;
    int draw_x = dst_x;
    int draw_w = copy_w;
    if (burn_ox > 0 && dst_x > 0) {
        pad_left = dst_x;
        draw_x = 0;
        draw_w = dst_x + copy_w;
    }
    if (burn_ox < 0) {
        int right_end = dst_x + copy_w;
        if (right_end < EXAMPLE_LCD_H_RES) {
            pad_right = EXAMPLE_LCD_H_RES - right_end;
            draw_w = copy_w + pad_right;
        }
    }
    if (draw_w & 1) {
        if (draw_x + draw_w < EXAMPLE_LCD_H_RES) {
            draw_w++;
            pad_right++;
        } else if (draw_w > 1) {
            draw_w--;
            if (pad_right > 0) {
                pad_right--;
            } else {
                copy_w--;
            }
        }
    }
    if ((draw_x & 1) && draw_x > 0) {
        draw_x--;
        pad_left++;
        draw_w++;
    } else if (draw_x & 1) {
        draw_x++;
        if (pad_left > 0) {
            pad_left--;
            draw_w--;
        }
    }

    const bool need_pack = (pad_left > 0 || pad_right > 0 || src_x0 != 0 || src_y0 != 0 ||
                            copy_w != src_w || copy_h != src_h || draw_w != copy_w);
    const lv_color_t *blit_src = color_map;

    if (need_pack) {
        size_t need_px = (size_t)draw_w * (size_t)copy_h;
        if (!s_burn_flush_scratch || s_burn_flush_scratch_px < need_px) {
            size_t alloc_px = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT;
            if (alloc_px < need_px) {
                alloc_px = need_px;
            }
            lv_color_t *fresh = (lv_color_t *)heap_caps_malloc(
                alloc_px * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
            if (!fresh) {
                fresh = (lv_color_t *)heap_caps_malloc(
                    alloc_px * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
            }
            if (!fresh) {
                lv_disp_flush_ready(drv);
                return;
            }
            if (s_burn_flush_scratch) {
                heap_caps_free(s_burn_flush_scratch);
            }
            s_burn_flush_scratch = fresh;
            s_burn_flush_scratch_px = alloc_px;
        }

        const lv_color_t black = lv_color_black();
        for (int row = 0; row < copy_h; row++) {
            lv_color_t *dst_row = s_burn_flush_scratch + row * draw_w;
            int col = 0;
            for (; col < pad_left; col++) {
                dst_row[col] = black;
            }
            memcpy(dst_row + pad_left,
                   color_map + (src_y0 + row) * src_w + src_x0,
                   (size_t)copy_w * sizeof(lv_color_t));
            col = pad_left + copy_w;
            for (; col < draw_w; col++) {
                dst_row[col] = black;
            }
        }
        blit_src = s_burn_flush_scratch;
    }

    esp_lcd_panel_draw_bitmap(panel_handle,
                              draw_x + 0x14,
                              dst_y,
                              draw_x + 0x14 + draw_w,
                              dst_y + copy_h,
                              blit_src);
}

void example_lvgl_rounder_cb(struct _lv_disp_drv_t *disp_drv, lv_area_t *area)
{
    uint16_t x1 = area->x1;
    uint16_t x2 = area->x2;

    uint16_t y1 = area->y1;
    uint16_t y2 = area->y2;

    // round the start of coordinate down to the nearest 2M number
    area->x1 = (x1 >> 1) << 1;
    area->y1 = (y1 >> 1) << 1;
    // round the end of coordinate up to the nearest 2N+1 number
    area->x2 = ((x2 >> 1) << 1) + 1;
    area->y2 = ((y2 >> 1) << 1) + 1;
}

#if EXAMPLE_USE_TOUCH
// 触摸坐标转换函数 - 与user_app.c保持一致
static void transform_touch_coordinates(uint16_t raw_x, uint16_t raw_y, uint16_t *screen_x, uint16_t *screen_y) {
    *screen_x = raw_y;  // Y轴成为X轴
    *screen_y = raw_x;  // X轴成为Y轴

    /* 与刷屏像素微移对齐：物理坐标 → 逻辑 UI 坐标 */
    amoled_burn_protection_map_touch(screen_x, screen_y);
    
    // 边界检查
    if (*screen_x >= EXAMPLE_LCD_H_RES) *screen_x = EXAMPLE_LCD_H_RES - 1;
    if (*screen_y >= EXAMPLE_LCD_V_RES) *screen_y = EXAMPLE_LCD_V_RES - 1;
}

static void example_lvgl_touch_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    uint16_t tp_x;
    uint16_t tp_y;
    uint8_t win = getTouch(&tp_x,&tp_y);
    if (win)
    {
        // 应用坐标转换
        uint16_t screen_x, screen_y;
        transform_touch_coordinates(tp_x, tp_y, &screen_x, &screen_y);
        
        data->point.x = screen_x;
        data->point.y = screen_y;
        
        if(data->point.x > EXAMPLE_LCD_H_RES)
            data->point.x = EXAMPLE_LCD_H_RES;
        if(data->point.y > EXAMPLE_LCD_V_RES)
            data->point.y = EXAMPLE_LCD_V_RES;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}
#endif

static void example_increase_lvgl_tick(void *arg)
{
    /* Tell LVGL how many milliseconds has elapsed */
    lv_tick_inc(EXAMPLE_LVGL_TICK_PERIOD_MS);
}

bool example_lvgl_lock(int timeout_ms)
{
    assert(lvgl_mux && "bsp_display_start must be called first");

    const TickType_t timeout_ticks = (timeout_ms == -1) ? portMAX_DELAY : pdMS_TO_TICKS(timeout_ms);
    return xSemaphoreTake(lvgl_mux, timeout_ticks) == pdTRUE;
}

void example_lvgl_unlock(void)
{
    assert(lvgl_mux && "bsp_display_start must be called first");
    xSemaphoreGive(lvgl_mux);
}

static void burn_protection_on_offset_changed(void)
{
    /* 勿在 esp_timer 回调里抢 LVGL 锁，延后到 LVGL 任务内 invalidate */
    s_burn_need_invalidate = true;
}

static void example_lvgl_port_task(void *arg)
{
    ESP_LOGI(TAG, "Starting LVGL task");
    uint32_t task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
    while (1) {
        // Lock the mutex due to the LVGL APIs are not thread-safe
        if (example_lvgl_lock(-1)) {
            if (s_burn_need_invalidate) {
                s_burn_need_invalidate = false;
                lv_obj_t *scr = lv_scr_act();
                if (scr) {
                    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
                    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
                    lv_obj_invalidate(scr);
                    uint32_t n = lv_obj_get_child_cnt(scr);
                    for (uint32_t i = 0; i < n; i++) {
                        lv_obj_t *child = lv_obj_get_child(scr, i);
                        if (child) {
                            lv_obj_invalidate(child);
                        }
                    }
                }
            }
            task_delay_ms = lv_timer_handler();
            // Release the mutex
            example_lvgl_unlock();
        }
        if (task_delay_ms > EXAMPLE_LVGL_TASK_MAX_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MAX_DELAY_MS;
        } else if (task_delay_ms < EXAMPLE_LVGL_TASK_MIN_DELAY_MS) {
            task_delay_ms = EXAMPLE_LVGL_TASK_MIN_DELAY_MS;
        }
        vTaskDelay(pdMS_TO_TICKS(task_delay_ms));
    }
}

void app_main(void)
{
    // 安全初始化NVS
    ESP_LOGI(TAG, "Initializing NVS for brightness memory...");
    esp_err_t nvs_ret = nvs_flash_init();
    if (nvs_ret == ESP_ERR_NVS_NO_FREE_PAGES || nvs_ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition was truncated and needs to be erased");
        esp_err_t erase_ret = nvs_flash_erase();
        if (erase_ret == ESP_OK) {
            nvs_ret = nvs_flash_init();
        } else {
            ESP_LOGE(TAG, "Failed to erase NVS: %s", esp_err_to_name(erase_ret));
            nvs_ret = erase_ret;
        }
    }
    
    if (nvs_ret == ESP_OK) {
        ESP_LOGI(TAG, "NVS initialized successfully");
        // 尝试从NVS加载亮度值
        current_brightness = load_brightness_from_nvs();
    } else {
        ESP_LOGE(TAG, "Failed to initialize NVS (%s), using default brightness", esp_err_to_name(nvs_ret));
        current_brightness = 179; // 使用默认70%亮度
    }
    
    // 初始化UI状态管理器
    ESP_LOGI(TAG, "Initializing UI state manager...");
    esp_err_t ui_state_ret = ui_state_manager_init();
    if (ui_state_ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize UI state manager: %s", esp_err_to_name(ui_state_ret));
        // 继续执行，但UI切换可能不稳定
    } else {
        ESP_LOGI(TAG, "UI state manager initialized successfully");
    }
    
    // 检查重启原因并打印诊断信息
    esp_reset_reason_t reset_reason = esp_reset_reason();
    ESP_LOGI(TAG, "Reset reason: %d", reset_reason);
    switch (reset_reason) {
        case ESP_RST_POWERON:
            ESP_LOGI(TAG, "Reset due to power-on event");
            break;
        case ESP_RST_EXT:
            ESP_LOGI(TAG, "Reset by external pin (not applicable for ESP32)");
            break;
        case ESP_RST_SW:
            ESP_LOGI(TAG, "Software reset via esp_restart");
            break;
        case ESP_RST_PANIC:
            ESP_LOGE(TAG, "Software reset due to exception/panic");
            break;
        case ESP_RST_INT_WDT:
            ESP_LOGE(TAG, "Reset due to interrupt watchdog");
            break;
        case ESP_RST_TASK_WDT:
            ESP_LOGE(TAG, "Reset due to task watchdog");
            break;
        case ESP_RST_WDT:
            ESP_LOGE(TAG, "Reset due to other watchdogs");
            break;
        case ESP_RST_DEEPSLEEP:
            ESP_LOGI(TAG, "Reset after exiting deep sleep mode");
            break;
        case ESP_RST_BROWNOUT:
            ESP_LOGE(TAG, "Brownout reset (software or hardware)");
            break;
        case ESP_RST_SDIO:
            ESP_LOGI(TAG, "Reset over SDIO");
            break;
        default:
            ESP_LOGW(TAG, "Unknown reset reason");
            break;
    }
    
    static lv_disp_draw_buf_t disp_buf; // contains internal graphic buffer(s) called draw buffer(s)
    static lv_disp_drv_t disp_drv;      // contains callback functions
#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn off LCD backlight");
    gpio_config_t bk_gpio_config = {
        .mode = GPIO_MODE_OUTPUT,
        .pin_bit_mask = 1ULL << EXAMPLE_PIN_NUM_BK_LIGHT
    };
    ESP_ERROR_CHECK(gpio_config(&bk_gpio_config));
#endif

    ESP_LOGI(TAG, "Initialize SPI bus");
    const spi_bus_config_t buscfg = SH8601_PANEL_BUS_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_PCLK,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA0,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA1,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA2,
                                                                 EXAMPLE_PIN_NUM_LCD_DATA3,
                                                                 EXAMPLE_LCD_H_RES * EXAMPLE_LCD_V_RES * LCD_BIT_PER_PIXEL / 8);
    ESP_ERROR_CHECK(spi_bus_initialize(LCD_HOST, &buscfg, SPI_DMA_CH_AUTO));

    ESP_LOGI(TAG, "Install panel IO");
    esp_lcd_panel_io_handle_t io_handle = NULL;
    const esp_lcd_panel_io_spi_config_t io_config = SH8601_PANEL_IO_QSPI_CONFIG(EXAMPLE_PIN_NUM_LCD_CS,
                                                                                example_notify_lvgl_flush_ready,
                                                                                &disp_drv);
    sh8601_vendor_config_t vendor_config = {
        .init_cmds = lcd_init_cmds,
        .init_cmds_size = sizeof(lcd_init_cmds) / sizeof(lcd_init_cmds[0]),
        .flags = {
            .use_qspi_interface = 1,
        },
    };
    // Attach the LCD to the SPI bus
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)LCD_HOST, &io_config, &io_handle));
    amoled_bl_handle = io_handle;
    esp_lcd_panel_handle_t panel_handle = NULL;
    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = EXAMPLE_PIN_NUM_LCD_RST,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB,
        .bits_per_pixel = LCD_BIT_PER_PIXEL,
        .vendor_config = &vendor_config,
    };
    ESP_LOGI(TAG, "Install SH8601 panel driver");
    ESP_ERROR_CHECK(esp_lcd_new_panel_sh8601(io_handle, &panel_config, &panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel_handle));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel_handle));
    
    // 初始化后立即设置最低亮度，避免白屏显示
    setBrightnes(0);  // 设置亮度为0
    
    I2C_master_Init(); //I2C_Init
    
#if EXAMPLE_USE_TOUCH
    // 重新启用触摸初始化进行测试
    touch_Init();
    ESP_LOGI(TAG, "Touch initialization re-enabled for testing");
#endif

#if EXAMPLE_PIN_NUM_BK_LIGHT >= 0
    ESP_LOGI(TAG, "Turn on LCD backlight");
    gpio_set_level(EXAMPLE_PIN_NUM_BK_LIGHT, EXAMPLE_LCD_BK_LIGHT_ON_LEVEL);
#endif

    ESP_LOGI(TAG, "Initialize LVGL library");
    
    lv_init();
    //alloc draw buffers used by LVGL
    //it's recommended to choose the size of the draw buffer(s) to be at least 1/10 screen sized
    size_t lvgl_buf_size = EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT * sizeof(lv_color_t);
    
    // 使用智能分配替代原有的分配方式，并显示分配类型
    lv_color_t *buf1 = (lv_color_t*)memory_diag_smart_malloc(lvgl_buf_size, false, true); // DMA优先
    ESP_LOGI(TAG, "LVGL buf1 addr=%p size=%d type=%s", buf1, (int)lvgl_buf_size, memory_diag_get_type_str(buf1));
    assert(buf1);
    
    lv_color_t *buf2 = (lv_color_t*)memory_diag_smart_malloc(lvgl_buf_size, false, true); // DMA优先
    ESP_LOGI(TAG, "LVGL buf2 addr=%p size=%d type=%s", buf2, (int)lvgl_buf_size, memory_diag_get_type_str(buf2));
    assert(buf2);
    
    //initialize LVGL draw buffers
    lv_disp_draw_buf_init(&disp_buf, buf1, buf2, EXAMPLE_LCD_H_RES * EXAMPLE_LVGL_BUF_HEIGHT);

    ESP_LOGI(TAG, "Register display driver to LVGL");
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = EXAMPLE_LCD_H_RES;
    disp_drv.ver_res = EXAMPLE_LCD_V_RES;
    disp_drv.flush_cb = example_lvgl_flush_cb;
    disp_drv.rounder_cb = example_lvgl_rounder_cb;
    disp_drv.draw_buf = &disp_buf;
    disp_drv.user_data = panel_handle;
    lv_disp_t *disp = lv_disp_drv_register(&disp_drv);

    ESP_LOGI(TAG, "Install LVGL tick timer");
    //Tick interface for LVGL (using esp_timer to generate 2ms periodic event)
    const esp_timer_create_args_t lvgl_tick_timer_args = {
        .callback = &example_increase_lvgl_tick,
        .name = "lvgl_tick"
    };
    esp_timer_handle_t lvgl_tick_timer = NULL;
    ESP_ERROR_CHECK(esp_timer_create(&lvgl_tick_timer_args, &lvgl_tick_timer));
    ESP_ERROR_CHECK(esp_timer_start_periodic(lvgl_tick_timer, EXAMPLE_LVGL_TICK_PERIOD_MS * 1000));

#if EXAMPLE_USE_TOUCH
    static lv_indev_drv_t indev_drv;           // Input device driver (Touch)
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.disp = disp;
    indev_drv.read_cb = example_lvgl_touch_cb;
    lv_indev_drv_register(&indev_drv);
#endif

    lvgl_mux = xSemaphoreCreateMutex();
    assert(lvgl_mux);
    xTaskCreate(example_lvgl_port_task, "LVGL", EXAMPLE_LVGL_TASK_STACK_SIZE, NULL, EXAMPLE_LVGL_TASK_PRIORITY, NULL);

    ESP_LOGI(TAG, "Display LVGL demos");
    // Lock the mutex due to the LVGL APIs are not thread-safe
    if (example_lvgl_lock(-1))
    {
        // 立即设置屏幕为黑色背景，消除白屏闪烁
        lv_obj_t *scr = lv_scr_act();
        lv_obj_set_style_bg_color(scr, lv_color_black(), 0);
        lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
        lv_refr_now(NULL);  // 立即刷新显示
        
        // 设置UI状态管理器的操作回调
        ui_operations_t ui_ops = {
            .get_screen_object = lv_scr_act,
            .acquire_ui_lock = example_lvgl_lock,
            .release_ui_lock = example_lvgl_unlock,
            .init_angle_ui = angle_display_init,
            .cleanup_angle_ui = angle_display_cleanup,
            .init_level_ui = level_display_init,
            .cleanup_level_ui = level_display_cleanup,
            .init_laser_ui = laser_ui_init,
            .cleanup_laser_ui = laser_ui_cleanup,
            .init_settings_ui = settings_ui_init,
            .cleanup_settings_ui = settings_ui_cleanup
        };
        
        esp_err_t ops_ret = ui_state_set_operations(&ui_ops);
        if (ops_ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to set UI operations: %s", esp_err_to_name(ops_ret));
        } else {
            ESP_LOGI(TAG, "UI operations set successfully");
        }
        
        ESP_LOGI(TAG, "[user_top_init] call begin");
        user_top_init();
        ESP_LOGI(TAG, "[user_top_init] call end");
        
        // 在UI初始化完成后再初始化ADC，提升启动速度
        ESP_LOGI(TAG, "Initializing ADC for battery voltage monitoring (post-UI)");
        adc_bsp_init();
        
        // 初始化激光测距硬件
        ESP_LOGI(TAG, "Initializing laser hardware...");
        esp_err_t laser_ret = laser_hardware_init();
        if (laser_ret == ESP_OK) {
            ESP_LOGI(TAG, "Laser hardware initialized successfully");
        } else {
            ESP_LOGE(TAG, "Failed to initialize laser hardware: %s", esp_err_to_name(laser_ret));
        }
        
        ESP_LOGI(TAG, "Initializing AMOLED burn protection (5min / ±2px even)...");
        amoled_burn_protection_config_t burn_cfg = {
            .offset_interval_ms = 300000,  /* 5 minutes */
            .max_offset_pixels = 2,        /* even only: avoids 1/4-buffer seam artifacts */
            .enable_random_offset = false,
            .on_offset_changed = burn_protection_on_offset_changed,
        };
        esp_err_t burn_ret = amoled_burn_protection_init(&burn_cfg);
        if (burn_ret == ESP_OK) {
            burn_ret = amoled_burn_protection_start();
            if (burn_ret == ESP_OK) {
                ESP_LOGI(TAG, "AMOLED burn protection started");
            } else {
                ESP_LOGE(TAG, "Failed to start AMOLED burn protection: %s",
                         esp_err_to_name(burn_ret));
            }
        } else {
            ESP_LOGE(TAG, "Failed to init AMOLED burn protection: %s",
                     esp_err_to_name(burn_ret));
        }
        
        //lv_demo_widgets();      /* A widgets example */
        //lv_demo_music();      /* A modern, smartphone-like music player demo. */
        // lv_demo_stress();    /* A stress test for LVGL. */
        //lv_demo_benchmark();  /* A demo to measure the performance of LVGL or to compare different settings. */
        // Release the mutex
        example_lvgl_unlock();
    }
}

void setBrightnes(uint8_t brig) 
{
    uint32_t lcd_cmd = 0x51;
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= 0x02 << 24;
    uint8_t param = brig;
    current_brightness = brig; // 保存当前亮度值
    esp_lcd_panel_io_tx_param(amoled_bl_handle, lcd_cmd, &param,1);
    
    // 重新启用NVS保存功能，带错误处理
    static uint8_t last_saved_brightness = 255;
    if (brig != last_saved_brightness) {
        esp_err_t err = save_brightness_to_nvs(brig);
        if (err == ESP_OK) {
            last_saved_brightness = brig;
        } else {
            ESP_LOGW(TAG, "Failed to save brightness to NVS, but display updated successfully");
        }
    }
}

// 临时设置亮度，不保存到NVS（用于电池保护等临时场景）
void setBrightnessTemporary(uint8_t brig) {
    uint32_t lcd_cmd = 0x51;
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= 0x02 << 24;
    uint8_t param = brig;
    // 注意：不更新current_brightness，保持原有用户设置的亮度值
    esp_lcd_panel_io_tx_param(amoled_bl_handle, lcd_cmd, &param, 1);
    ESP_LOGI(TAG, "Temporary brightness set to %d (%.1f%%) - not saved to NVS", brig, (brig * 100.0f) / 255.0f);
}

// 专门用于深度休眠的显示关闭函数，不触发NVS保存
void shutdownDisplay(void) {
    uint32_t lcd_cmd = 0x51;
    lcd_cmd &= 0xff;
    lcd_cmd <<= 8;
    lcd_cmd |= 0x02 << 24;
    uint8_t brightness_zero = 0;
    esp_lcd_panel_io_tx_param(amoled_bl_handle, lcd_cmd, &brightness_zero, 1);
    ESP_LOGI(TAG, "Display shutdown for deep sleep");
}

uint8_t getBrightness(void)
{
    return current_brightness;
}

// 公共函数：从NVS加载保存的亮度值
uint8_t loadSavedBrightness(void)
{
    return load_brightness_from_nvs();
}