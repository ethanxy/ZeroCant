#ifndef UI_STATE_MANAGER_H
#define UI_STATE_MANAGER_H

#include "esp_err.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "lvgl.h"  // 添加LVGL支持以使用lv_obj_t类型
#include <stdbool.h>
#include <stdint.h>

// 确保ESP_ERR_USER_BASE定义可用，如果没有则使用备用值
#ifndef ESP_ERR_USER_BASE
#define ESP_ERR_USER_BASE 0x6000
#endif

#ifdef __cplusplus
extern "C" {
#endif

// UI状态定义
typedef enum {
    UI_STATE_ANGLE = 0,      // 角度显示模式
    UI_STATE_LEVEL = 1,      // 水平仪模式
    UI_STATE_LASER = 2,      // 激光测距模式
    UI_STATE_ACTION = 3,     // 动作监控模式
    UI_STATE_SETTINGS = 4,   // 设置模式
    UI_STATE_TRANSITIONING = 5,  // 过渡状态
    UI_STATE_ERROR = 6,      // 错误状态
    UI_STATE_MAX
} ui_state_t;

// 状态转换事件
typedef enum {
    UI_EVENT_SWITCH_REQUEST,     // 切换请求
    UI_EVENT_TRANSITION_COMPLETE,// 转换完成
    UI_EVENT_TRANSITION_FAILED,  // 转换失败
    UI_EVENT_DATA_UPDATE,        // 数据更新
    UI_EVENT_USER_INPUT,         // 用户输入
    UI_EVENT_TIMEOUT,            // 超时事件
    UI_EVENT_ERROR_RECOVERY      // 错误恢复
} ui_event_t;

// 转换步骤
typedef enum {
    TRANSITION_STEP_IDLE,        // 空闲状态
    TRANSITION_STEP_REQUEST,     // 请求转换
    TRANSITION_STEP_VALIDATE,    // 验证转换
    TRANSITION_STEP_PREPARE,     // 准备新状态
    TRANSITION_STEP_SUSPEND_OLD, // 暂停旧状态
    TRANSITION_STEP_ACTIVATE_NEW,// 激活新状态
    TRANSITION_STEP_CLEANUP_OLD, // 清理旧状态
    TRANSITION_STEP_COMPLETE,    // 转换完成
    TRANSITION_STEP_ROLLBACK     // 回滚
} transition_step_t;

// UI命令消息
typedef struct {
    ui_event_t event_type;
    ui_state_t target_state;
    void *params;
    uint32_t timeout_ms;
    uint32_t sequence_id;
} ui_command_message_t;

// 转换上下文
typedef struct {
    ui_state_t from_state;
    ui_state_t to_state;
    transition_step_t current_step;
    uint32_t start_time;
    uint32_t timeout_ms;
    uint32_t sequence_id;
    bool rollback_needed;
    esp_err_t last_error;
} transition_context_t;

// 状态管理器
typedef struct {
    ui_state_t current_state;
    ui_state_t target_state;
    bool is_transitioning;
    SemaphoreHandle_t state_mutex;
    QueueHandle_t command_queue;
    transition_context_t transition_ctx;
    uint32_t transition_count;
    uint32_t error_count;
} ui_state_manager_t;

// 状态变化回调
typedef void (*ui_state_change_cb_t)(ui_state_t old_state, ui_state_t new_state);

// UI操作回调函数类型
typedef struct {
    lv_obj_t* (*get_screen_object)(void);                     // 获取屏幕对象 (lv_scr_act)
    bool (*acquire_ui_lock)(int timeout_ms);                  // 获取UI锁 (example_lvgl_lock)
    void (*release_ui_lock)(void);                            // 释放UI锁 (example_lvgl_unlock)
    void (*init_angle_ui)(lv_obj_t *parent, int w, int h);    // 初始化角度UI
    void (*cleanup_angle_ui)(void);                           // 清理角度UI
    void (*init_level_ui)(lv_obj_t *parent, int w, int h);    // 初始化水平仪UI
    void (*cleanup_level_ui)(void);                           // 清理水平仪UI
    void (*init_laser_ui)(lv_obj_t *parent, int w, int h);    // 初始化激光UI
    void (*cleanup_laser_ui)(void);                           // 清理激光UI
    void (*init_action_ui)(lv_obj_t *parent, int w, int h);   // 初始化动作监控UI
    void (*cleanup_action_ui)(void);                          // 清理动作监控UI
    void (*init_settings_ui)(lv_obj_t *parent, int w, int h); // 初始化设置UI
    void (*cleanup_settings_ui)(void);                        // 清理设置UI
} ui_operations_t;

// 主要接口函数
/**
 * @brief 初始化UI状态管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_state_manager_init(void);

/**
 * @brief 设置UI操作回调函数
 * @param ops UI操作回调函数结构体指针
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_state_set_operations(const ui_operations_t *ops);

/**
 * @brief 反初始化UI状态管理器
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_state_manager_deinit(void);

/**
 * @brief 请求UI状态转换
 * @param target_state 目标状态
 * @param timeout_ms 超时时间(毫秒)
 * @return ESP_OK 请求成功，其他值表示错误
 */
esp_err_t ui_state_request_transition(ui_state_t target_state, uint32_t timeout_ms);

/**
 * @brief 获取当前UI状态
 * @return 当前UI状态
 */
ui_state_t ui_state_get_current(void);

/**
 * @brief 检查是否正在转换状态
 * @return true 正在转换，false 不在转换
 */
bool ui_state_is_transitioning(void);

/**
 * @brief 强制停止当前转换
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_state_abort_transition(void);

/**
 * @brief 注册状态变化回调
 * @param callback 回调函数
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_state_register_callback(ui_state_change_cb_t callback);

/**
 * @brief 获取状态名称字符串
 * @param state UI状态
 * @return 状态名称字符串
 */
const char* ui_state_get_name(ui_state_t state);

/**
 * @brief 获取状态管理器统计信息
 * @param transition_count 转换次数
 * @param error_count 错误次数
 * @return ESP_OK 成功，其他值表示错误
 */
esp_err_t ui_state_get_statistics(uint32_t *transition_count, uint32_t *error_count);

/**
 * @brief 检查状态转换是否有效
 * @param from_state 源状态
 * @param to_state 目标状态
 * @return true 有效，false 无效
 */
bool ui_state_is_transition_valid(ui_state_t from_state, ui_state_t to_state);

// 配置参数
#define UI_COMMAND_QUEUE_SIZE       10
#define UI_DEFAULT_TRANSITION_TIMEOUT_MS  5000
#define UI_LOCK_TIMEOUT_MS          1000
#define UI_MAX_STATE_CHANGE_CALLBACKS 4

// 错误代码
#define ESP_ERR_UI_INVALID_STATE    (ESP_ERR_USER_BASE + 1)
#define ESP_ERR_UI_TRANSITION_TIMEOUT (ESP_ERR_USER_BASE + 2)
#define ESP_ERR_UI_TRANSITION_FAILED  (ESP_ERR_USER_BASE + 3)
#define ESP_ERR_UI_ALREADY_TRANSITIONING (ESP_ERR_USER_BASE + 4)

#ifdef __cplusplus
}
#endif

#endif // UI_STATE_MANAGER_H
