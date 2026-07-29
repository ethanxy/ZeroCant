#include "memory_diag.h"
#include <string.h>

static const char* TAG = "memory_diag";

void memory_diag_print_status(const char* tag) {
    size_t free_heap = heap_caps_get_free_size(MALLOC_CAP_8BIT);
    size_t total_heap = heap_caps_get_total_size(MALLOC_CAP_8BIT);
    size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    size_t total_psram = heap_caps_get_total_size(MALLOC_CAP_SPIRAM);
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    size_t total_dma = heap_caps_get_total_size(MALLOC_CAP_DMA);
    size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    size_t total_internal = heap_caps_get_total_size(MALLOC_CAP_INTERNAL);
    
    ESP_LOGI(tag ? tag : TAG, "=== Memory Status ===");
    ESP_LOGI(tag ? tag : TAG, "Heap     : %6u / %6u KB (%.1f%% free)", 
             (unsigned)(free_heap/1024), (unsigned)(total_heap/1024), 
             100.0f * free_heap / total_heap);
    ESP_LOGI(tag ? tag : TAG, "PSRAM    : %6u / %6u KB (%.1f%% free)", 
             (unsigned)(free_psram/1024), (unsigned)(total_psram/1024), 
             total_psram ? 100.0f * free_psram / total_psram : 0.0f);
    ESP_LOGI(tag ? tag : TAG, "DMA      : %6u / %6u KB (%.1f%% free)", 
             (unsigned)(free_dma/1024), (unsigned)(total_dma/1024), 
             100.0f * free_dma / total_dma);
    ESP_LOGI(tag ? tag : TAG, "Internal : %6u / %6u KB (%.1f%% free)", 
             (unsigned)(free_internal/1024), (unsigned)(total_internal/1024), 
             100.0f * free_internal / total_internal);
    ESP_LOGI(tag ? tag : TAG, "==================");
}

bool memory_diag_check_available(size_t size, bool prefer_psram) {
    if (prefer_psram) {
        size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
        if (free_psram >= size + 1024) { // 保留1KB安全边界
            return true;
        }
    }
    
    size_t free_dma = heap_caps_get_free_size(MALLOC_CAP_DMA);
    if (free_dma >= size + 1024) { // 保留1KB安全边界
        return true;
    }
    
    return false;
}

void* memory_diag_smart_malloc(size_t size, bool prefer_psram, bool allow_fallback) {
    void* ptr = NULL;
    
    ESP_LOGI(TAG, "Smart malloc: size=%u prefer_psram=%d fallback=%d", 
             (unsigned)size, prefer_psram, allow_fallback);
    
    if (prefer_psram) {
        // 尝试PSRAM分配
        ptr = heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
        if (ptr) {
            ESP_LOGI(TAG, "Allocated in PSRAM: addr=%p size=%u", ptr, (unsigned)size);
            return ptr;
        } else {
            ESP_LOGW(TAG, "PSRAM allocation failed");
        }
        
        if (!allow_fallback) {
            return NULL;
        }
    }
    
    // 尝试DMA内存
    ptr = heap_caps_malloc(size, MALLOC_CAP_DMA);
    if (ptr) {
        ESP_LOGI(TAG, "Allocated in DMA: addr=%p size=%u", ptr, (unsigned)size);
        return ptr;
    } else {
        ESP_LOGW(TAG, "DMA allocation failed");
    }
    
    if (!allow_fallback) {
        return NULL;
    }
    
    // 最后尝试普通堆内存
    ptr = malloc(size);
    if (ptr) {
        ESP_LOGI(TAG, "Allocated in heap: addr=%p size=%u", ptr, (unsigned)size);
        return ptr;
    }
    
    ESP_LOGE(TAG, "All allocations failed for size=%u", (unsigned)size);
    return NULL;
}

const char* memory_diag_get_type_str(void* ptr) {
    if (!ptr) {
        return "NULL";
    }
    
    // 基于地址范围的简单判断
    uintptr_t addr = (uintptr_t)ptr;
    
    // ESP32-S3 PSRAM 地址范围
    if (addr >= 0x3C000000 && addr < 0x3E000000) {
        return "PSRAM";
    }
    // ESP32-S3 内部 SRAM 地址范围
    else if (addr >= 0x3FC00000 && addr < 0x40000000) {
        return "Internal";
    }
    // 其他情况
    else {
        return "Other";
    }
}
