#include "ui_state_manager.h"
#include "esp_log.h"
#include "freertos/task.h"
#include <string.h>

// LCD尺寸常量
#define UI_LCD_WIDTH  280
#define UI_LCD_HEIGHT 456

static const char *TAG = "ui_state_manager";

// 全局状态管理器实例
static ui_state_manager_t g_state_manager = {0};
static bool g_initialized = false;

// UI操作回调
static ui_operations_t g_ui_ops = {0};
static bool g_ui_ops_set = false;

// 状态变化回调数组
static ui_state_change_cb_t g_state_callbacks[UI_MAX_STATE_CHANGE_CALLBACKS] = {0};
static int g_callback_count = 0;

// 状态名称映射
static const char* g_state_names[] = {
    "ANGLE",
    "LEVEL",
    "LASER",
    "ACTION",
    "SETTINGS",
    "TRANSITIONING",
    "ERROR"
};

// 状态转换有效性矩阵
static const bool g_transition_matrix[UI_STATE_MAX][UI_STATE_MAX] = {
    // FROM\TO    ANGLE LEVEL LASER ACTION SETTINGS TRANS ERROR
    /* ANGLE */   {false, true,  true,  true,  true,   false, true},
    /* LEVEL */   {true,  false, true,  true,  true,   false, true},
    /* LASER */   {true,  true,  false, true,  true,   false, true},
    /* ACTION */  {true,  true,  true,  false, true,   false, true},
    /* SETTINGS */{true,  true,  true,  true,  false,  false, true},
    /* TRANS */   {false, false, false, false, false,  false, true},
    /* ERROR */   {true,  true,  true,  true,  true,   false, false}
};

// 前向声明
static void ui_state_manager_task(void *pvParameters);
static esp_err_t execute_transition_step(transition_context_t *ctx);
static void notify_state_change(ui_state_t old_state, ui_state_t new_state);
static esp_err_t validate_transition_request(ui_state_t from_state, ui_state_t to_state);

esp_err_t ui_state_manager_init(void) {
    if (g_initialized) {
        ESP_LOGW(TAG, "UI state manager already initialized");
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing UI state manager");
    
    // 初始化状态管理器
    memset(&g_state_manager, 0, sizeof(ui_state_manager_t));
    g_state_manager.current_state = UI_STATE_ANGLE;  // 默认状态
    g_state_manager.target_state = UI_STATE_ANGLE;
    g_state_manager.is_transitioning = false;
    
    // 创建互斥锁
    g_state_manager.state_mutex = xSemaphoreCreateMutex();
    if (!g_state_manager.state_mutex) {
        ESP_LOGE(TAG, "Failed to create state mutex");
        return ESP_ERR_NO_MEM;
    }
    
    // 创建命令队列
    g_state_manager.command_queue = xQueueCreate(UI_COMMAND_QUEUE_SIZE, sizeof(ui_command_message_t));
    if (!g_state_manager.command_queue) {
        ESP_LOGE(TAG, "Failed to create command queue");
        vSemaphoreDelete(g_state_manager.state_mutex);
        return ESP_ERR_NO_MEM;
    }
    
    // 创建状态管理任务
    TaskHandle_t task_handle = NULL;
    BaseType_t task_created = xTaskCreate(
        ui_state_manager_task,
        "ui_state_mgr",
        8192,  // 增加栈大小
        NULL,
        5,     // 降低优先级，避免与其他任务冲突
        &task_handle
    );
    
    if (task_created != pdPASS) {
        ESP_LOGE(TAG, "Failed to create state manager task, result: %d", task_created);
        vQueueDelete(g_state_manager.command_queue);
        vSemaphoreDelete(g_state_manager.state_mutex);
        return ESP_ERR_NO_MEM;
    } else {
        ESP_LOGI(TAG, "State manager task created successfully, handle: %p", task_handle);
    }
    
    g_initialized = true;
    ESP_LOGI(TAG, "UI state manager initialized successfully, initial state: %s", 
             ui_state_get_name(g_state_manager.current_state));
    
    return ESP_OK;
}

esp_err_t ui_state_set_operations(const ui_operations_t *ops) {
    if (!ops) {
        ESP_LOGE(TAG, "UI operations pointer is NULL");
        return ESP_ERR_INVALID_ARG;
    }
    
    // 验证必要的回调函数
    if (!ops->get_screen_object || !ops->acquire_ui_lock || !ops->release_ui_lock) {
        ESP_LOGE(TAG, "Missing required UI operation callbacks");
        return ESP_ERR_INVALID_ARG;
    }
    
    g_ui_ops = *ops;
    g_ui_ops_set = true;
    
    ESP_LOGI(TAG, "UI operations callbacks set successfully");
    return ESP_OK;
}

esp_err_t ui_state_manager_deinit(void) {
    if (!g_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Deinitializing UI state manager");
    
    // 清理资源
    if (g_state_manager.command_queue) {
        vQueueDelete(g_state_manager.command_queue);
    }
    
    if (g_state_manager.state_mutex) {
        vSemaphoreDelete(g_state_manager.state_mutex);
    }
    
    // 注意：任务会自然退出，这里不强制删除
    
    memset(&g_state_manager, 0, sizeof(ui_state_manager_t));
    g_initialized = false;
    g_callback_count = 0;
    
    ESP_LOGI(TAG, "UI state manager deinitialized");
    return ESP_OK;
}

esp_err_t ui_state_request_transition(ui_state_t target_state, uint32_t timeout_ms) {
    if (!g_initialized) {
        ESP_LOGE(TAG, "State manager not initialized");
        return ESP_ERR_INVALID_STATE;
    }
    
    if (target_state >= UI_STATE_MAX) {
        ESP_LOGE(TAG, "Invalid target state: %d", target_state);
        return ESP_ERR_INVALID_ARG;
    }
    
    // 获取当前状态并验证转换
    ui_state_t current = ui_state_get_current();
    esp_err_t validation_result = validate_transition_request(current, target_state);
    if (validation_result != ESP_OK) {
        return validation_result;
    }
    
    // 创建转换命令
    ui_command_message_t cmd = {
        .event_type = UI_EVENT_SWITCH_REQUEST,
        .target_state = target_state,
        .params = NULL,
        .timeout_ms = (timeout_ms > 0) ? timeout_ms : UI_DEFAULT_TRANSITION_TIMEOUT_MS,
        .sequence_id = xTaskGetTickCount()
    };
    
    // 发送命令到队列
    if (xQueueSend(g_state_manager.command_queue, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to send transition command to queue");
        return ESP_ERR_TIMEOUT;
    }
    
    ESP_LOGI(TAG, "Requested transition from %s to %s", 
             ui_state_get_name(current), ui_state_get_name(target_state));
    
    return ESP_OK;
}

ui_state_t ui_state_get_current(void) {
    if (!g_initialized) {
        return UI_STATE_ERROR;
    }
    
    ui_state_t current_state;
    if (xSemaphoreTake(g_state_manager.state_mutex, pdMS_TO_TICKS(UI_LOCK_TIMEOUT_MS)) == pdTRUE) {
        current_state = g_state_manager.current_state;
        xSemaphoreGive(g_state_manager.state_mutex);
    } else {
        ESP_LOGW(TAG, "Failed to acquire state mutex for reading current state");
        current_state = UI_STATE_ERROR;
    }
    
    return current_state;
}

bool ui_state_is_transitioning(void) {
    if (!g_initialized) {
        return false;
    }
    
    bool transitioning = false;
    if (xSemaphoreTake(g_state_manager.state_mutex, pdMS_TO_TICKS(UI_LOCK_TIMEOUT_MS)) == pdTRUE) {
        transitioning = g_state_manager.is_transitioning;
        xSemaphoreGive(g_state_manager.state_mutex);
    } else {
        ESP_LOGW(TAG, "Failed to acquire state mutex for checking transition status");
    }
    
    return transitioning;
}

esp_err_t ui_state_abort_transition(void) {
    if (!g_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    
    esp_err_t result = ESP_OK;
    
    if (xSemaphoreTake(g_state_manager.state_mutex, pdMS_TO_TICKS(UI_LOCK_TIMEOUT_MS)) == pdTRUE) {
        if (g_state_manager.is_transitioning) {
            ESP_LOGW(TAG, "Aborting transition from %s to %s", 
                     ui_state_get_name(g_state_manager.transition_ctx.from_state),
                     ui_state_get_name(g_state_manager.transition_ctx.to_state));
            
            g_state_manager.transition_ctx.rollback_needed = true;
            g_state_manager.transition_ctx.current_step = TRANSITION_STEP_ROLLBACK;
        } else {
            ESP_LOGW(TAG, "No active transition to abort");
            result = ESP_ERR_INVALID_STATE;
        }
        xSemaphoreGive(g_state_manager.state_mutex);
    } else {
        ESP_LOGE(TAG, "Failed to acquire state mutex for aborting transition");
        result = ESP_ERR_TIMEOUT;
    }
    
    return result;
}

esp_err_t ui_state_register_callback(ui_state_change_cb_t callback) {
    if (!callback) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (g_callback_count >= UI_MAX_STATE_CHANGE_CALLBACKS) {
        ESP_LOGE(TAG, "Maximum number of state change callbacks reached");
        return ESP_ERR_NO_MEM;
    }
    
    g_state_callbacks[g_callback_count++] = callback;
    ESP_LOGI(TAG, "Registered state change callback, total: %d", g_callback_count);
    
    return ESP_OK;
}

const char* ui_state_get_name(ui_state_t state) {
    if (state >= UI_STATE_MAX) {
        return "UNKNOWN";
    }
    return g_state_names[state];
}

esp_err_t ui_state_get_statistics(uint32_t *transition_count, uint32_t *error_count) {
    if (!transition_count || !error_count) {
        return ESP_ERR_INVALID_ARG;
    }
    
    if (!g_initialized) {
        *transition_count = 0;
        *error_count = 0;
        return ESP_ERR_INVALID_STATE;
    }
    
    if (xSemaphoreTake(g_state_manager.state_mutex, pdMS_TO_TICKS(UI_LOCK_TIMEOUT_MS)) == pdTRUE) {
        *transition_count = g_state_manager.transition_count;
        *error_count = g_state_manager.error_count;
        xSemaphoreGive(g_state_manager.state_mutex);
    } else {
        ESP_LOGW(TAG, "Failed to acquire state mutex for reading statistics");
        return ESP_ERR_TIMEOUT;
    }
    
    return ESP_OK;
}

bool ui_state_is_transition_valid(ui_state_t from_state, ui_state_t to_state) {
    if (from_state >= UI_STATE_MAX || to_state >= UI_STATE_MAX) {
        return false;
    }
    
    return g_transition_matrix[from_state][to_state];
}

// 内部函数实现
static void ui_state_manager_task(void *pvParameters) {
    ESP_LOGI(TAG, "UI state manager task started");
    
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "UI state manager task running");
    
    ui_command_message_t cmd;
    TickType_t queue_timeout;
    uint32_t loop_count = 0;
    
    while (g_initialized) {
        loop_count++;
        
        if (loop_count == 1) {
            ESP_LOGI(TAG, "Entering main task loop, g_initialized=%d", g_initialized);
        }
        
        // 根据当前状态设置队列超时
        if (g_state_manager.is_transitioning) {
            // 转换期间使用短超时，频繁检查转换进度
            queue_timeout = pdMS_TO_TICKS(50);
            ESP_LOGI(TAG, "Task loop %lu: transitioning, using short timeout", loop_count);
        } else {
            // 没有活跃转换时使用长超时
            queue_timeout = pdMS_TO_TICKS(1000);
            if (loop_count % 10 == 1) { // 每10次循环打印一次心跳
                ESP_LOGI(TAG, "Task loop %lu: idle, waiting for commands", loop_count);
            }
        }
        
        // 从队列接收命令
        if (xQueueReceive(g_state_manager.command_queue, &cmd, queue_timeout) == pdTRUE) {
            ESP_LOGI(TAG, "Received command: event=%d, target_state=%s", 
                     cmd.event_type, ui_state_get_name(cmd.target_state));
            
            switch (cmd.event_type) {
                case UI_EVENT_SWITCH_REQUEST:
                    // 处理状态切换请求
                    if (xSemaphoreTake(g_state_manager.state_mutex, pdMS_TO_TICKS(UI_LOCK_TIMEOUT_MS)) == pdTRUE) {
                        if (!g_state_manager.is_transitioning) {
                            // 初始化转换上下文
                            g_state_manager.transition_ctx.from_state = g_state_manager.current_state;
                            g_state_manager.transition_ctx.to_state = cmd.target_state;
                            g_state_manager.transition_ctx.current_step = TRANSITION_STEP_REQUEST;
                            g_state_manager.transition_ctx.start_time = xTaskGetTickCount();
                            g_state_manager.transition_ctx.timeout_ms = cmd.timeout_ms;
                            g_state_manager.transition_ctx.sequence_id = cmd.sequence_id;
                            g_state_manager.transition_ctx.rollback_needed = false;
                            g_state_manager.transition_ctx.last_error = ESP_OK;
                            
                            g_state_manager.is_transitioning = true;
                            g_state_manager.target_state = cmd.target_state;
                            
                            ESP_LOGI(TAG, "Starting transition from %s to %s", 
                                     ui_state_get_name(g_state_manager.transition_ctx.from_state),
                                     ui_state_get_name(g_state_manager.transition_ctx.to_state));
                        } else {
                            ESP_LOGW(TAG, "Ignoring transition request - already transitioning");
                        }
                        xSemaphoreGive(g_state_manager.state_mutex);
                    }
                    break;
                    
                default:
                    ESP_LOGW(TAG, "Unknown command event: %d", cmd.event_type);
                    break;
            }
        }
        
        // 处理正在进行的转换
        if (g_state_manager.is_transitioning) {
            esp_err_t step_result = execute_transition_step(&g_state_manager.transition_ctx);
            
            if (step_result != ESP_OK || 
                g_state_manager.transition_ctx.current_step == TRANSITION_STEP_COMPLETE ||
                g_state_manager.transition_ctx.rollback_needed) {
                
                // 转换完成或失败
                if (xSemaphoreTake(g_state_manager.state_mutex, pdMS_TO_TICKS(UI_LOCK_TIMEOUT_MS)) == pdTRUE) {
                    ui_state_t old_state = g_state_manager.current_state;
                    
                    if (step_result == ESP_OK && !g_state_manager.transition_ctx.rollback_needed) {
                        // 转换成功
                        g_state_manager.current_state = g_state_manager.transition_ctx.to_state;
                        g_state_manager.transition_count++;
                        
                        ESP_LOGI(TAG, "Transition completed successfully: %s -> %s", 
                                 ui_state_get_name(old_state), 
                                 ui_state_get_name(g_state_manager.current_state));
                        
                        // 通知状态变化
                        notify_state_change(old_state, g_state_manager.current_state);
                    } else {
                        // 转换失败
                        g_state_manager.error_count++;
                        ESP_LOGE(TAG, "Transition failed: %s -> %s, error: %s", 
                                 ui_state_get_name(g_state_manager.transition_ctx.from_state),
                                 ui_state_get_name(g_state_manager.transition_ctx.to_state),
                                 esp_err_to_name(step_result));
                    }
                    
                    // 重置转换状态
                    g_state_manager.is_transitioning = false;
                    g_state_manager.target_state = g_state_manager.current_state;
                    memset(&g_state_manager.transition_ctx, 0, sizeof(transition_context_t));
                    
                    xSemaphoreGive(g_state_manager.state_mutex);
                }
            }
        }
        
        // 检查转换超时
        if (g_state_manager.is_transitioning) {
            uint32_t elapsed = (xTaskGetTickCount() - g_state_manager.transition_ctx.start_time) * portTICK_PERIOD_MS;
            if (elapsed > g_state_manager.transition_ctx.timeout_ms) {
                ESP_LOGE(TAG, "Transition timeout after %lu ms", elapsed);
                g_state_manager.transition_ctx.rollback_needed = true;
                g_state_manager.transition_ctx.last_error = ESP_ERR_UI_TRANSITION_TIMEOUT;
            }
        }
    }
    
    ESP_LOGI(TAG, "UI state manager task exiting");
    vTaskDelete(NULL);
}

static esp_err_t execute_transition_step(transition_context_t *ctx) {
    esp_err_t result = ESP_OK;
    
    switch (ctx->current_step) {
        case TRANSITION_STEP_REQUEST:
            ESP_LOGI(TAG, "Transition step: REQUEST");
            ctx->current_step = TRANSITION_STEP_VALIDATE;
            break;
            
        case TRANSITION_STEP_VALIDATE:
            ESP_LOGI(TAG, "Transition step: VALIDATE");
            if (ui_state_is_transition_valid(ctx->from_state, ctx->to_state)) {
                ESP_LOGI(TAG, "Transition validation passed: %s -> %s", 
                         ui_state_get_name(ctx->from_state), 
                         ui_state_get_name(ctx->to_state));
                ctx->current_step = TRANSITION_STEP_PREPARE;
            } else {
                ESP_LOGE(TAG, "Invalid transition: %s -> %s", 
                         ui_state_get_name(ctx->from_state), 
                         ui_state_get_name(ctx->to_state));
                result = ESP_ERR_UI_INVALID_STATE;
            }
            break;
            
        case TRANSITION_STEP_PREPARE:
            ESP_LOGI(TAG, "Transition step: PREPARE - Cleaning old UI first");
            
            // 检查UI操作回调是否已设置
            if (!g_ui_ops_set) {
                ESP_LOGE(TAG, "UI operations not set, cannot perform transition");
                result = ESP_ERR_INVALID_STATE;
                break;
            }
            
            ESP_LOGI(TAG, "Acquiring LVGL lock for old UI cleanup...");
            // 先清理旧UI，避免两个UI同时存在
            if (!g_ui_ops.acquire_ui_lock(1000)) {
                ESP_LOGE(TAG, "Failed to acquire LVGL lock for cleanup");
                result = ESP_ERR_TIMEOUT;
                break;
            }
            
            ESP_LOGI(TAG, "Cleaning up old UI component: %s", ui_state_get_name(ctx->from_state));
            // 清理旧的UI组件
            switch (ctx->from_state) {
                case UI_STATE_ANGLE:
                    if (g_ui_ops.cleanup_angle_ui) {
                        g_ui_ops.cleanup_angle_ui();
                    }
                    break;
                case UI_STATE_LEVEL:
                    if (g_ui_ops.cleanup_level_ui) {
                        g_ui_ops.cleanup_level_ui();
                    }
                    break;
                case UI_STATE_LASER:
                    if (g_ui_ops.cleanup_laser_ui) {
                        g_ui_ops.cleanup_laser_ui();
                    }
                    break;
                case UI_STATE_ACTION:
                    if (g_ui_ops.cleanup_action_ui) {
                        g_ui_ops.cleanup_action_ui();
                    }
                    break;
                case UI_STATE_SETTINGS:
                    if (g_ui_ops.cleanup_settings_ui) {
                        g_ui_ops.cleanup_settings_ui();
                    }
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown source state for cleanup: %d", ctx->from_state);
                    break;
            }
            
            ESP_LOGI(TAG, "Old UI cleaned up, forcing display refresh...");
            // 强制刷新确保旧UI完全清除
            lv_refr_now(NULL);
            
            g_ui_ops.release_ui_lock();
            
            // 给系统时间完成清理
            vTaskDelay(pdMS_TO_TICKS(30));
            
            ctx->current_step = TRANSITION_STEP_SUSPEND_OLD;
            break;
            
        case TRANSITION_STEP_SUSPEND_OLD:
            ESP_LOGI(TAG, "Transition step: SUSPEND_OLD - Creating new UI");
            
            ESP_LOGI(TAG, "Acquiring LVGL lock for new UI creation...");
            if (!g_ui_ops.acquire_ui_lock(1000)) {
                ESP_LOGE(TAG, "Failed to acquire LVGL lock for new UI creation");
                result = ESP_ERR_TIMEOUT;
                break;
            }
            
            ESP_LOGI(TAG, "Creating new UI component for state: %s", ui_state_get_name(ctx->to_state));
            // 创建新的UI组件
            lv_obj_t *parent = g_ui_ops.get_screen_object();
            switch (ctx->to_state) {
                case UI_STATE_ANGLE:
                    if (g_ui_ops.init_angle_ui) {
                        g_ui_ops.init_angle_ui(parent, UI_LCD_WIDTH, UI_LCD_HEIGHT);
                    }
                    break;
                case UI_STATE_LEVEL:
                    if (g_ui_ops.init_level_ui) {
                        g_ui_ops.init_level_ui(parent, UI_LCD_WIDTH, UI_LCD_HEIGHT);
                    }
                    break;
                case UI_STATE_LASER:
                    if (g_ui_ops.init_laser_ui) {
                        g_ui_ops.init_laser_ui(parent, UI_LCD_WIDTH, UI_LCD_HEIGHT);
                    }
                    break;
                case UI_STATE_ACTION:
                    if (g_ui_ops.init_action_ui) {
                        g_ui_ops.init_action_ui(parent, UI_LCD_WIDTH, UI_LCD_HEIGHT);
                    }
                    break;
                case UI_STATE_SETTINGS:
                    if (g_ui_ops.init_settings_ui) {
                        g_ui_ops.init_settings_ui(parent, UI_LCD_WIDTH, UI_LCD_HEIGHT);
                    }
                    break;
                default:
                    ESP_LOGE(TAG, "Unknown target state for creation: %d", ctx->to_state);
                    result = ESP_ERR_INVALID_ARG;
                    break;
            }
            
            g_ui_ops.release_ui_lock();
            
            if (result == ESP_OK) {
                ESP_LOGI(TAG, "New UI component created: %s", ui_state_get_name(ctx->to_state));
                ctx->current_step = TRANSITION_STEP_ACTIVATE_NEW;
            }
            break;
            
        case TRANSITION_STEP_ACTIVATE_NEW:
            ESP_LOGI(TAG, "Transition step: ACTIVATE_NEW - Finalizing transition");
            // 给新UI一些时间稳定
            vTaskDelay(pdMS_TO_TICKS(20));
            ctx->current_step = TRANSITION_STEP_COMPLETE;
            break;
            
        case TRANSITION_STEP_CLEANUP_OLD:
            // 这个步骤现在在PREPARE中完成，直接跳到COMPLETE
            ESP_LOGI(TAG, "Transition step: CLEANUP_OLD - Already completed in PREPARE");
            ctx->current_step = TRANSITION_STEP_COMPLETE;
            break;
            
        case TRANSITION_STEP_COMPLETE:
            ESP_LOGI(TAG, "Transition step: COMPLETE");
            // 转换完成，状态将在调用者中更新
            break;
            
        case TRANSITION_STEP_ROLLBACK:
            ESP_LOGD(TAG, "Transition step: ROLLBACK");
            // TODO: 回滚到原始状态
            result = ESP_ERR_UI_TRANSITION_FAILED;
            break;
            
        default:
            ESP_LOGE(TAG, "Unknown transition step: %d", ctx->current_step);
            result = ESP_ERR_UI_INVALID_STATE;
            break;
    }
    
    return result;
}

static void notify_state_change(ui_state_t old_state, ui_state_t new_state) {
    for (int i = 0; i < g_callback_count; i++) {
        if (g_state_callbacks[i]) {
            g_state_callbacks[i](old_state, new_state);
        }
    }
}

static esp_err_t validate_transition_request(ui_state_t from_state, ui_state_t to_state) {
    // 检查是否已在转换中
    if (ui_state_is_transitioning()) {
        ESP_LOGW(TAG, "Already transitioning, rejecting new request");
        return ESP_ERR_UI_ALREADY_TRANSITIONING;
    }
    
    // 检查转换是否有效
    if (!ui_state_is_transition_valid(from_state, to_state)) {
        ESP_LOGE(TAG, "Invalid transition: %s -> %s", 
                 ui_state_get_name(from_state), ui_state_get_name(to_state));
        return ESP_ERR_UI_INVALID_STATE;
    }
    
    // 检查目标状态是否与当前状态相同
    if (from_state == to_state) {
        ESP_LOGD(TAG, "Target state same as current state: %s", ui_state_get_name(to_state));
        return ESP_ERR_UI_INVALID_STATE;
    }
    
    return ESP_OK;
}
