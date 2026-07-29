#!/bin/bash

# ESP32 重启监控脚本
# 使用方法: ./restart_monitor.sh

echo "ESP32 重启监控开始..."
echo "监控端口: /dev/cu.usbmodem31301"
echo "按 Ctrl+C 退出监控"
echo "========================================"

# 设置ESP-IDF环境
source /Users/xuanyuliu/esp/v5.4.2/esp-idf/export.sh

# 启动监控
cd /Users/xuanyuliu/164

# 使用idf.py monitor并过滤重启相关信息
idf.py monitor --port /dev/cu.usbmodem31301 | while IFS= read -r line; do
    # 获取当前时间戳
    timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    
    # 检查是否包含重启相关信息
    if [[ "$line" == *"rst:"* ]] || \
       [[ "$line" == *"Reset reason"* ]] || \
       [[ "$line" == *"RESTART ANALYSIS"* ]] || \
       [[ "$line" == *"Free heap"* ]] || \
       [[ "$line" == *"panic"* ]] || \
       [[ "$line" == *"WDT"* ]] || \
       [[ "$line" == *"Brownout"* ]] || \
       [[ "$line" == *"Exception"* ]] || \
       [[ "$line" == *"abort"* ]] || \
       [[ "$line" == *"assert"* ]]; then
        echo "[$timestamp] 🔴 $line"
    else
        # 普通日志，简化显示
        echo "[$timestamp] $line"
    fi
done
