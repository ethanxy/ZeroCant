#include "laser.h"
#include "lvgl.h"
#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "ballistic_calc.h"
#include "ballistic_profile.h"
#include "angle_calc.h"
#include "idle_sleep.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static const char* TAG = "laser";

static lv_obj_t *laser_screen = NULL;
static lv_obj_t *status_label = NULL;
static lv_obj_t *measure_btn = NULL;  // 新增：测量按钮
static lv_obj_t *hold_guide = NULL;   // Hold 左右方向引导
static lv_obj_t *hold_hint_left = NULL;
static lv_obj_t *hold_hint_right = NULL;
static lv_obj_t *hold_value_label = NULL;
static lv_obj_t *hold_unit_label = NULL;
static int screen_width = 240;
static int screen_height = 240;

#define HOLD_DEADBAND_MIL   0.05f
#define HOLD_SIDE_SIZE      56
#define HOLD_COLOR_ACTIVE   0x3DFF9A
#define HOLD_COLOR_IDLE     0x555555

static void laser_ui_hold_guide_hide(void);
static void laser_ui_hold_guide_show(float hold_mil, bool valid);

// 硬件相关变量
static bool hardware_initialized = false;
static bool laser_powered = false;

// SDDM测量命令
static const uint8_t sddm_measure_cmd[] = {
    0xFA, 0x01, 0xFF, 0x04, 0x01, 0x00, 0x01, 0x00, 0x00
};
#define SDDM_MEASURE_CMD_LEN    (sizeof(sddm_measure_cmd))
#define SDDM_RESPONSE_MAX_LEN   32

// CRC计算函数
static uint8_t calculate_crc(const uint8_t *data, size_t len) {
    uint8_t crc = 0;
    for (size_t i = 0; i < len; i++) {
        crc += data[i];
    }
    return crc & 0xFF;  // 取低8位
}

// 测量状态相关变量
static bool measurement_in_progress = false;
static bool measurement_result_displayed = false;  // 跟踪是否显示了测量结果

// 按钮点击事件处理函数
static void measure_btn_event_cb(lv_event_t * e) {
    lv_event_code_t code = lv_event_get_code(e);
    
    if (code == LV_EVENT_CLICKED) {
        ESP_LOGI(TAG, "Measure button clicked");
        laser_start_measurement();
    }
}

static void hold_pulse_anim_cb(void *obj, int32_t v)
{
    if (obj && lv_obj_is_valid(obj)) {
        lv_obj_set_style_text_opa((lv_obj_t *)obj, (lv_opa_t)v, 0);
    }
}

static void laser_ui_hold_guide_stop_anim(void)
{
    if (hold_hint_left && lv_obj_is_valid(hold_hint_left)) {
        lv_anim_del(hold_hint_left, hold_pulse_anim_cb);
        lv_obj_set_style_text_opa(hold_hint_left, LV_OPA_COVER, 0);
    }
    if (hold_hint_right && lv_obj_is_valid(hold_hint_right)) {
        lv_anim_del(hold_hint_right, hold_pulse_anim_cb);
        lv_obj_set_style_text_opa(hold_hint_right, LV_OPA_COVER, 0);
    }
}

static void laser_ui_hold_guide_pulse(lv_obj_t *label)
{
    if (!label || !lv_obj_is_valid(label)) {
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, label);
    lv_anim_set_values(&a, LV_OPA_50, LV_OPA_COVER);
    lv_anim_set_time(&a, 650);
    lv_anim_set_playback_time(&a, 650);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_set_exec_cb(&a, hold_pulse_anim_cb);
    lv_anim_start(&a);
}

static void style_hold_side(lv_obj_t *label, bool active)
{
    lv_color_t color = lv_color_hex(active ? HOLD_COLOR_ACTIVE : HOLD_COLOR_IDLE);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_text_opa(label, active ? LV_OPA_COVER : LV_OPA_40, 0);
}

static void laser_ui_hold_guide_hide(void)
{
    laser_ui_hold_guide_stop_anim();
    if (hold_guide && lv_obj_is_valid(hold_guide)) {
        lv_obj_add_flag(hold_guide, LV_OBJ_FLAG_HIDDEN);
    }
}

static void laser_ui_hold_guide_show(float hold_mil, bool valid)
{
    if (!hold_guide || !lv_obj_is_valid(hold_guide)) {
        return;
    }

    laser_ui_hold_guide_stop_anim();
    lv_obj_clear_flag(hold_guide, LV_OBJ_FLAG_HIDDEN);

    lv_label_set_text(hold_hint_left, LV_SYMBOL_UP "\nUP");
    lv_label_set_text(hold_hint_right, LV_SYMBOL_DOWN "\nDN");

    if (!valid) {
        lv_label_set_text(hold_value_label, "--");
        style_hold_side(hold_hint_left, false);
        style_hold_side(hold_hint_right, false);
        lv_obj_set_style_text_color(hold_value_label, lv_color_hex(HOLD_COLOR_IDLE), 0);
        return;
    }

    char mil_text[16];
    snprintf(mil_text, sizeof(mil_text), "%.2f", fabsf(hold_mil));
    lv_label_set_text(hold_value_label, mil_text);

    bool up = hold_mil > HOLD_DEADBAND_MIL;
    bool dn = hold_mil < -HOLD_DEADBAND_MIL;

    style_hold_side(hold_hint_left, up);
    style_hold_side(hold_hint_right, dn);
    lv_obj_set_style_text_color(hold_value_label, (up || dn) ? lv_color_hex(HOLD_COLOR_ACTIVE) : lv_color_white(), 0);

    if (up) {
        laser_ui_hold_guide_pulse(hold_hint_left);
    } else if (dn) {
        laser_ui_hold_guide_pulse(hold_hint_right);
    }
}

static void laser_ui_hold_guide_create(void)
{
    hold_guide = lv_obj_create(laser_screen);
    lv_obj_set_size(hold_guide, screen_width - 16, 88);
    lv_obj_align(hold_guide, LV_ALIGN_CENTER, 0, -6);
    lv_obj_set_style_bg_opa(hold_guide, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(hold_guide, 0, 0);
    lv_obj_set_style_pad_all(hold_guide, 0, 0);
    lv_obj_clear_flag(hold_guide, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(hold_guide, LV_OBJ_FLAG_HIDDEN);

    /* 左侧：大号 ↑ / UP（占原半圆区域） */
    lv_obj_t *left_box = lv_obj_create(hold_guide);
    lv_obj_set_size(left_box, HOLD_SIDE_SIZE, HOLD_SIDE_SIZE);
    lv_obj_set_style_bg_opa(left_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(left_box, 0, 0);
    lv_obj_set_style_pad_all(left_box, 0, 0);
    lv_obj_clear_flag(left_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(left_box, LV_ALIGN_LEFT_MID, 8, 0);

    hold_hint_left = lv_label_create(left_box);
    lv_label_set_text(hold_hint_left, LV_SYMBOL_UP "\nUP");
    lv_obj_set_style_text_font(hold_hint_left, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(hold_hint_left, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(hold_hint_left, -4, 0);
    style_hold_side(hold_hint_left, false);
    lv_obj_center(hold_hint_left);

    /* 中间：Hold 数值 */
    lv_obj_t *hold_caption = lv_label_create(hold_guide);
    lv_label_set_text(hold_caption, "HOLD");
    lv_obj_set_style_text_font(hold_caption, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hold_caption, lv_color_hex(0x888888), 0);
    lv_obj_align(hold_caption, LV_ALIGN_CENTER, 0, -28);

    hold_value_label = lv_label_create(hold_guide);
    lv_label_set_text(hold_value_label, "0.00");
    lv_obj_set_style_text_font(hold_value_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(hold_value_label, lv_color_white(), 0);
    lv_obj_align(hold_value_label, LV_ALIGN_CENTER, 0, -4);

    hold_unit_label = lv_label_create(hold_guide);
    lv_label_set_text(hold_unit_label, "mil");
    lv_obj_set_style_text_font(hold_unit_label, &lv_font_montserrat_12, 0);
    lv_obj_set_style_text_color(hold_unit_label, lv_color_hex(0xAAAAAA), 0);
    lv_obj_align(hold_unit_label, LV_ALIGN_CENTER, 0, 20);

    /* 右侧：大号 ↓ / DN */
    lv_obj_t *right_box = lv_obj_create(hold_guide);
    lv_obj_set_size(right_box, HOLD_SIDE_SIZE, HOLD_SIDE_SIZE);
    lv_obj_set_style_bg_opa(right_box, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(right_box, 0, 0);
    lv_obj_set_style_pad_all(right_box, 0, 0);
    lv_obj_clear_flag(right_box, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE);
    lv_obj_align(right_box, LV_ALIGN_RIGHT_MID, -8, 0);

    hold_hint_right = lv_label_create(right_box);
    lv_label_set_text(hold_hint_right, LV_SYMBOL_DOWN "\nDN");
    lv_obj_set_style_text_font(hold_hint_right, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_align(hold_hint_right, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_line_space(hold_hint_right, -4, 0);
    style_hold_side(hold_hint_right, false);
    lv_obj_center(hold_hint_right);
}

void laser_ui_init(lv_obj_t *parent, int scr_width, int scr_height) {
    screen_width = scr_width;
    screen_height = scr_height;
    
    printf("laser_ui_init: initializing %dx%d laser UI\n", screen_width, screen_height);
    
    // 强制清理任何已存在的激光UI对象
    if (laser_screen || status_label || measure_btn || hold_guide) {
        printf("laser_ui_init: Forcing cleanup of existing UI objects\n");
        laser_ui_cleanup();
    }
    
    // 短暂延迟确保清理完成
    vTaskDelay(pdMS_TO_TICKS(10));
    
    // 创建主屏幕容器（全黑背景）
    laser_screen = lv_obj_create(parent);
    if (!laser_screen) {
        printf("laser_ui_init: Failed to create laser_screen!\n");
        return;
    }
    
    lv_obj_set_size(laser_screen, screen_width, screen_height);
    lv_obj_align(laser_screen, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(laser_screen, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(laser_screen, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(laser_screen, 0, 0);
    lv_obj_set_style_pad_all(laser_screen, 0, 0);
    lv_obj_clear_flag(laser_screen, LV_OBJ_FLAG_SCROLLABLE);
    
    // 创建状态文本标签（屏幕上方）
    status_label = lv_label_create(laser_screen);
    if (!status_label) {
        printf("laser_ui_init: Failed to create status_label!\n");
        return;
    }
    lv_label_set_text(status_label, "Ready to measure");
    lv_obj_set_style_text_color(status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(status_label, &lv_font_montserrat_14, 0);
    lv_label_set_long_mode(status_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_width(status_label, screen_width - 36);
    lv_obj_align(status_label, LV_ALIGN_TOP_MID, 0, 28);
    lv_obj_set_style_text_align(status_label, LV_TEXT_ALIGN_CENTER, 0);

    laser_ui_hold_guide_create();
    
    // 创建测量按钮（屏幕下方）
    measure_btn = lv_btn_create(laser_screen);
    if (!measure_btn) {
        printf("laser_ui_init: Failed to create measure_btn!\n");
        return;
    }
    lv_obj_set_size(measure_btn, 200, 64);
    lv_obj_align(measure_btn, LV_ALIGN_BOTTOM_MID, 0, -18);
    lv_obj_set_ext_click_area(measure_btn, 24);
    /* 手指轻微滑动时仍锁定在按钮上，避免取消 CLICKED */
    lv_obj_add_flag(measure_btn, LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_clear_flag(measure_btn, LV_OBJ_FLAG_SCROLL_CHAIN);
    lv_obj_add_event_cb(measure_btn, measure_btn_event_cb, LV_EVENT_CLICKED, NULL);
    
    // 设置按钮样式（与settings组件Set Level按钮一致）
    lv_obj_set_style_bg_color(measure_btn, lv_color_hex(0x404040), 0);  // 深灰色背景
    lv_obj_set_style_bg_opa(measure_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(measure_btn, lv_color_hex(0x606060), LV_STATE_PRESSED);
    lv_obj_set_style_radius(measure_btn, 8, 0);
    lv_obj_set_style_border_width(measure_btn, 1, 0);
    lv_obj_set_style_border_color(measure_btn, lv_color_white(), 0);
    
    // 按钮文本
    lv_obj_t *btn_label = lv_label_create(measure_btn);
    lv_label_set_text(btn_label, "MEASURE");
    lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(btn_label, &lv_font_montserrat_14, 0);
    lv_obj_center(btn_label);
    
    printf("laser_ui_init: completed successfully\n");
}

void laser_ui_set_status_text(const char *text) {
    if (!status_label) {
        printf("laser_ui_set_status_text: status_label not initialized\n");
        return;
    }
    
    if (!text) {
        printf("laser_ui_set_status_text: text is NULL\n");
        return;
    }
    
    lv_label_set_text(status_label, text);
}

esp_err_t laser_hardware_init(void) {
    if (hardware_initialized) {
        ESP_LOGW(TAG, "Laser hardware already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing laser hardware...");
    
    // 配置电源控制引脚
    gpio_config_t power_pin_config = {
        .pin_bit_mask = (1ULL << LASER_POWER_PIN),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    esp_err_t ret = gpio_config(&power_pin_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure power pin: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 配置UART
    uart_config_t uart_config = {
        .baud_rate = LASER_UART_BAUD_RATE,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_APB,
    };
    
    ret = uart_driver_install(LASER_UART_NUM, 1024, 1024, 0, NULL, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install UART driver: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = uart_param_config(LASER_UART_NUM, &uart_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to configure UART: %s", esp_err_to_name(ret));
        uart_driver_delete(LASER_UART_NUM);
        return ret;
    }
    
    ret = uart_set_pin(LASER_UART_NUM, LASER_UART_TXD_PIN, LASER_UART_RXD_PIN, 
                       UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set UART pins: %s", esp_err_to_name(ret));
        uart_driver_delete(LASER_UART_NUM);
        return ret;
    }
    
    // 初始化时不通电，只在需要测量时才通电
    gpio_set_level(LASER_POWER_PIN, 0);
    laser_powered = false;
    
    hardware_initialized = true;
    ESP_LOGI(TAG, "Laser hardware initialized successfully (power off)");

    ballistic_profile_init();
    ballistic_run_sanity_check();
    
    return ESP_OK;
}

// 解析SDDM响应数据
static int parse_sddm_response(const uint8_t *data, size_t len, uint16_t *distance) {
    // 检查响应长度
    if (len < 9) {
        ESP_LOGW(TAG, "SDDM响应长度不足: %d", len);
        return -1;
    }
    
    // 检查响应头 FB 03
    if (data[0] != 0xFB || data[1] != 0x03) {
        ESP_LOGW(TAG, "SDDM响应头错误: %02X %02X", data[0], data[1]);
        return -1;
    }
    
    // 打印完整响应用于调试
    ESP_LOGI(TAG, "SDDM响应: %02X %02X %02X %02X %02X %02X %02X %02X %02X",
             data[0], data[1], data[2], data[3], data[4], data[5], data[6], data[7], data[8]);
    
    // 验证CRC (所有字节除最后一个字节的累加和)
    uint8_t calculated_crc = calculate_crc(data, len - 1);
    uint8_t received_crc = data[len - 1];
    if (calculated_crc != received_crc) {
        ESP_LOGW(TAG, "SDDM CRC错误: 计算值=%02X, 接收值=%02X", calculated_crc, received_crc);
        return -1;
    }
    
    // 解析数据有效性指示 (小端序，位置4-5)
    uint16_t data_valid = (uint16_t)data[5] << 8 | data[4];
    ESP_LOGI(TAG, "数据有效性指示: %04X", data_valid);
    
    // 检查数据有效性 - 根据SDDM协议，1表示有效数据，0表示无效数据
    if (data_valid != 0x0001) {
        ESP_LOGW(TAG, "SDDM测量数据无效: %04X (可能原因: 无反射目标、超出量程、环境干扰)", data_valid);
        return -1;
    }
    
    // 解析距离数据 (小端序，位置6-7，单位：分米)
    uint16_t distance_dm = (uint16_t)data[7] << 8 | data[6];
    
    // 额外检查：距离数据是否在合理范围内 (SDDM量程: 3-2000m = 30-20000分米)
    if (distance_dm < 30 || distance_dm > 20000) {
        ESP_LOGW(TAG, "SDDM距离数据超出合理范围: %d分米 (合理范围: 30-20000分米)", distance_dm);
        return -1;
    }
    
    // 检查是否为已知的错误代码
    if (distance_dm >= 0xFF00) {  // 0xFF00-0xFFFF范围通常为错误代码
        ESP_LOGW(TAG, "SDDM返回错误代码: %04X", distance_dm);
        return -1;
    }
    
    // 转换为毫米 (1分米 = 100毫米)
    *distance = distance_dm * 100;
    
    ESP_LOGI(TAG, "SDDM测量成功: %d分米 = %d毫米", distance_dm, *distance);
    return 0;
}

esp_err_t laser_measure(float *distance_mm, uint32_t timeout_ms) {
    if (!hardware_initialized) {
        ESP_LOGE(TAG, "Laser hardware not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (!distance_mm) {
        ESP_LOGE(TAG, "Distance pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }

    idle_sleep_on_activity();
    
    ESP_LOGI(TAG, "Starting laser measurement...");
    
    // 记录各阶段时间
    uint32_t stage_start = xTaskGetTickCount() * portTICK_PERIOD_MS;
    
    // 测量前通电
    ESP_LOGI(TAG, "Powering on SDDM device...");
    laser_ui_set_status_text("Powering device...");
    lv_refr_now(NULL);
    
    gpio_set_level(LASER_POWER_PIN, 1);
    laser_powered = true;
    
    uint32_t power_on_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Power on completed at %lu ms (took %lu ms)", 
            power_on_time, power_on_time - stage_start);
    
    vTaskDelay(pdMS_TO_TICKS(1000));  // 等待设备启动稳定，增加到1秒
    
    // 上电完成，开始测量
    laser_ui_set_status_text("Measuring distance...");
    lv_refr_now(NULL);
    
    uint32_t delay_done_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Startup delay completed at %lu ms (took %lu ms)", 
            delay_done_time, delay_done_time - power_on_time);
    
    esp_err_t result = ESP_OK;
    
    // 清空UART缓冲区
    uart_flush(LASER_UART_NUM);
    
    // 发送测量命令
    uint32_t cmd_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Sending measurement command at %lu ms", cmd_start_time);
    
    int bytes_written = uart_write_bytes(LASER_UART_NUM, sddm_measure_cmd, SDDM_MEASURE_CMD_LEN);
    if (bytes_written != SDDM_MEASURE_CMD_LEN) {
        ESP_LOGE(TAG, "Failed to send measurement command");
        result = ESP_ERR_INVALID_STATE;
        goto cleanup;
    }
    
    uint32_t cmd_sent_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Command sent at %lu ms (took %lu ms)", 
            cmd_sent_time, cmd_sent_time - cmd_start_time);
    
    ESP_LOGD(TAG, "Measurement command sent");
    
    // 等待响应 - 使用更高效的读取方式
    uint32_t read_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Starting to wait for response at %lu ms", read_start_time);
    
    uint8_t response_buffer[SDDM_RESPONSE_MAX_LEN];
    int bytes_read = 0;
    
    // 尝试多次小的读取，而不是一次大的阻塞读取
    for (int attempt = 0; attempt < (timeout_ms / 10); attempt++) {
        size_t available_bytes = 0;
        uart_get_buffered_data_len(LASER_UART_NUM, &available_bytes);
        
        if (available_bytes > 0) {
            ESP_LOGI(TAG, "Found %d bytes available after %d ms", available_bytes, attempt * 10);
            bytes_read = uart_read_bytes(LASER_UART_NUM, response_buffer, 
                                       SDDM_RESPONSE_MAX_LEN, pdMS_TO_TICKS(100));
            break;
        }
        
        vTaskDelay(pdMS_TO_TICKS(10)); // 等待10ms再检查
    }
    
    uint32_t read_done_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Response received at %lu ms (waited %lu ms, got %d bytes)", 
            read_done_time, read_done_time - read_start_time, bytes_read);
    
    if (bytes_read <= 0) {
        ESP_LOGE(TAG, "No response received from laser device");
        result = ESP_ERR_TIMEOUT;
        goto cleanup;
    }
    
    ESP_LOGD(TAG, "Received %d bytes from laser device", bytes_read);
    ESP_LOG_BUFFER_HEX_LEVEL(TAG, response_buffer, bytes_read, ESP_LOG_DEBUG);
    
    // 解析响应数据
    uint16_t distance_raw_mm;
    int parse_ret = parse_sddm_response(response_buffer, bytes_read, &distance_raw_mm);
    if (parse_ret != 0) {
        ESP_LOGE(TAG, "Failed to parse measurement response");
        result = ESP_FAIL;
        goto cleanup;
    }
    
    // 转换为float类型
    *distance_mm = (float)distance_raw_mm;
    
    ESP_LOGI(TAG, "Measurement successful: %.2f mm", *distance_mm);

cleanup:
    // 测量完成后断电
    ESP_LOGI(TAG, "Powering off SDDM device...");
    gpio_set_level(LASER_POWER_PIN, 0);
    laser_powered = false;
    
    return result;
}

void laser_start_measurement(void) {
    // 检查是否已经在测量中
    if (measurement_in_progress) {
        ESP_LOGW(TAG, "Measurement already in progress, ignoring request");
        return;
    }

    idle_sleep_on_activity();
    
    ESP_LOGI(TAG, "Starting single-click measurement");
    
    // 开始测量
    measurement_in_progress = true;
    measurement_result_displayed = false;
    laser_ui_hold_guide_hide();
    
    // 禁用按钮防止重复点击，并改变按钮文本
    if (measure_btn && lv_obj_is_valid(measure_btn)) {
        lv_obj_add_state(measure_btn, LV_STATE_DISABLED);
        // 改变按钮文本以显示正在测量
        lv_obj_t *btn_label = lv_obj_get_child(measure_btn, 0);
        if (btn_label && lv_obj_is_valid(btn_label)) {
            lv_label_set_text(btn_label, "MEASURING...");
        }
        lv_refr_now(NULL);
    }
    
    // 记录开始时间
    uint32_t measure_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Starting measurement at %lu ms", measure_start_time);
    
    // 执行激光测量（内部会进行上电和等待）
    float distance_mm;
    esp_err_t ret = laser_measure(&distance_mm, 1500);  // 1.5秒超时
    
    // 记录结束时间
    uint32_t measure_end_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
    ESP_LOGI(TAG, "Measurement completed at %lu ms, total time: %lu ms", 
            measure_end_time, measure_end_time - measure_start_time);
    
    if (ret == ESP_OK) {
        // 测量成功，显示距离 + 弹道修正（固定弹药 profile）
        float distance_m = distance_mm / 1000.0f;  // 转换为米

        float pitch = 0.0f, roll = 0.0f, yaw = 0.0f;
        angle_calc_get(&pitch, &roll, &yaw);
        /* Device pitch: upright≈0, tip-back (aim up) is negative → look angle = -pitch */
        float look_angle_deg = -pitch;

        ballistic_input_t bal_in = {
            .distance_m = distance_m,
            .look_angle_deg = look_angle_deg,
        };
        ballistic_output_t bal_out = {0};
        esp_err_t bal_ret = ballistic_solve_holdover(&bal_in, &bal_out);

        char distance_text[160];
        if (bal_ret == ESP_OK && bal_out.valid) {
            snprintf(distance_text, sizeof(distance_text),
                     "Distance: %.2f m\n"
                     "Drop: %.1f in (%.0f mm)\n"
                     "%s",
                     distance_m,
                     bal_out.drop_in,
                     bal_out.drop_m * 1000.0f,
                     ballistic_profile_name());
            laser_ui_hold_guide_show(bal_out.hold_mil, true);
        } else {
            snprintf(distance_text, sizeof(distance_text),
                     "Distance: %.2f m\n"
                     "%s",
                     distance_m,
                     ballistic_profile_name());
            laser_ui_hold_guide_show(0.0f, false);
        }
        laser_ui_set_status_text(distance_text);
        measurement_result_displayed = true;

        ESP_LOGI(TAG, "Measurement successful: %.2f mm look=%.1f° bal=%s",
                 distance_mm, look_angle_deg, esp_err_to_name(bal_ret));
    } else {
        // 测量失败，根据错误类型显示不同的提示信息
        const char* error_message;
        switch (ret) {
            case ESP_ERR_TIMEOUT:
                error_message = "Measurement timeout\nCheck target & try again";
                break;
            case ESP_FAIL:
                error_message = "No valid target\nAim at solid surface";
                break;
            case ESP_ERR_INVALID_STATE:
                error_message = "Sensor error\nCheck connection";
                break;
            default:
                error_message = "Measurement failed\nTry again";
                break;
        }
        
        laser_ui_hold_guide_hide();
        laser_ui_set_status_text(error_message);
        measurement_result_displayed = true;
        ESP_LOGE(TAG, "Measurement failed: %s", esp_err_to_name(ret));
    }
    
    // 重新启用按钮并恢复原始文本
    if (measure_btn && lv_obj_is_valid(measure_btn)) {
        lv_obj_clear_state(measure_btn, LV_STATE_DISABLED);
        // 恢复按钮文本
        lv_obj_t *btn_label = lv_obj_get_child(measure_btn, 0);
        if (btn_label && lv_obj_is_valid(btn_label)) {
            lv_label_set_text(btn_label, "MEASURE");
        }
    }
    
    measurement_in_progress = false;
}

void laser_handle_long_press(bool is_pressed, uint32_t press_duration_ms) {
    // 已禁用触摸并按住测量功能，现在使用按钮点击测量
    // 这个函数保留是为了兼容性，但不执行任何操作
    (void)is_pressed;           // 避免未使用参数警告
    (void)press_duration_ms;    // 避免未使用参数警告
    return;
    
    // 以下代码已被禁用
    /*
    static char status_text[64];
    
    if (is_pressed) {
        if (!touch_pressed) {
            // 开始按下
            touch_pressed = true;
            measurement_result_displayed = false;  // 重置结果显示状态
            press_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            laser_ui_set_status_text("Hold for 1.5s to measure...");
        } else {
            // 持续按下，检查是否达到长按时间
            uint32_t current_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
            uint32_t hold_duration = current_time - press_start_time;
            
            if (hold_duration >= LONG_PRESS_DURATION_MS && !measurement_in_progress) {
                // 开始测量
                measurement_in_progress = true;
                laser_ui_set_status_text("Measuring...");
                
                // 记录开始时间
                uint32_t measure_start_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                ESP_LOGI(TAG, "Starting measurement at %lu ms", measure_start_time);
                
                // 执行激光测量
                float distance_mm;
                esp_err_t ret = laser_measure(&distance_mm, 1500);  // 1.5秒超时
                
                // 记录结束时间
                uint32_t measure_end_time = xTaskGetTickCount() * portTICK_PERIOD_MS;
                ESP_LOGI(TAG, "Measurement completed at %lu ms, total time: %lu ms", 
                        measure_end_time, measure_end_time - measure_start_time);
                
                if (ret == ESP_OK) {
                    // 测量成功，在status_label上显示距离
                    float distance_m = distance_mm / 1000.0f;  // 转换为米
                    
                    char distance_text[64];
                    snprintf(distance_text, sizeof(distance_text), 
                            "Distance: %.2f m\n(%.0f mm)", distance_m, distance_mm);
                    laser_ui_set_status_text(distance_text);
                    measurement_result_displayed = true;  // 标记结果已显示
                    
                    ESP_LOGI(TAG, "Measurement successful: %.2f mm", distance_mm);
                } else {
                    // 测量失败
                    laser_ui_set_status_text("Measurement failed");
                    measurement_result_displayed = true;  // 标记结果已显示
                    ESP_LOGE(TAG, "Measurement failed: %s", esp_err_to_name(ret));
                }
                
                measurement_in_progress = false;
            } else if (hold_duration < LONG_PRESS_DURATION_MS) {
                // 显示进度
                float progress = (float)hold_duration / LONG_PRESS_DURATION_MS;
                snprintf(status_text, sizeof(status_text), 
                        "Hold... %.0f%%", progress * 100);
                laser_ui_set_status_text(status_text);
            }
        }
    } else {
        // 释放按钮
        if (touch_pressed) {
            touch_pressed = false;
            
            // 只有在没有显示测量结果且没有在测量中时才重置状态文本
            if (!measurement_in_progress && !measurement_result_displayed) {
                laser_ui_set_status_text("Touch and Hold to Measure");
            }
        }
    }
    */
}

void laser_hardware_deinit(void) {
    if (!hardware_initialized) {
        return;
    }
    
    ESP_LOGI(TAG, "Deinitializing laser hardware...");
    
    // 关闭电源
    gpio_set_level(LASER_POWER_PIN, 0);
    laser_powered = false;
    
    // 删除UART驱动
    uart_driver_delete(LASER_UART_NUM);
    
    hardware_initialized = false;
    ESP_LOGI(TAG, "Laser hardware deinitialized");
}

void laser_ui_cleanup(void) {
    printf("laser_ui_cleanup: cleaning up laser UI\n");

    laser_ui_hold_guide_stop_anim();
    
    // 删除LVGL对象，添加额外的有效性检查
    if (measure_btn && lv_obj_is_valid(measure_btn)) {
        lv_obj_del(measure_btn);
        measure_btn = NULL;
    }
    
    if (status_label && lv_obj_is_valid(status_label)) {
        lv_obj_del(status_label);
        status_label = NULL;
    }

    /* hold_guide 及其子对象随 laser_screen 一并删除 */
    hold_guide = NULL;
    hold_hint_left = NULL;
    hold_hint_right = NULL;
    hold_value_label = NULL;
    hold_unit_label = NULL;
    
    if (laser_screen && lv_obj_is_valid(laser_screen)) {
        lv_obj_del(laser_screen);
        laser_screen = NULL;
    }
    
    printf("laser_ui_cleanup: completed\n");
}
