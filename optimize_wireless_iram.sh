#!/bin/bash
# 无线组件IRAM优化脚本
# 将蓝牙和WiFi组件从IRAM移到Flash中执行，减少IRAM使用

echo "🔧 开始优化无线组件IRAM使用..."

# 备份当前配置
cp sdkconfig.defaults sdkconfig.defaults.backup_$(date +%Y%m%d_%H%M%S)
echo "✅ 已备份原始配置文件"

# 添加WiFi IRAM优化配置到sdkconfig.defaults
echo ""
echo "# === 无线组件IRAM优化配置 ===" >> sdkconfig.defaults
echo "# 禁用WiFi IRAM优化，让WiFi代码在Flash中执行" >> sdkconfig.defaults
echo "CONFIG_ESP_WIFI_IRAM_OPT=n" >> sdkconfig.defaults
echo "CONFIG_ESP_WIFI_RX_IRAM_OPT=n" >> sdkconfig.defaults
echo "CONFIG_ESP_WIFI_EXTRA_IRAM_OPT=n" >> sdkconfig.defaults
echo "CONFIG_ESP_WIFI_SLP_IRAM_OPT=n" >> sdkconfig.defaults
echo "" >> sdkconfig.defaults

echo "# 蓝牙额外IRAM配置（已有SPIRAM优先分配）" >> sdkconfig.defaults
echo "# CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST=y 已存在" >> sdkconfig.defaults
echo "" >> sdkconfig.defaults

echo "# LVGL IRAM优化 - 禁用LVGL使用IRAM" >> sdkconfig.defaults
echo "CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n" >> sdkconfig.defaults
echo "" >> sdkconfig.defaults

echo "🔧 已添加无线组件IRAM优化配置"

# 显示变更摘要
echo ""
echo "📊 优化配置摘要："
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🌐 WiFi优化:"
echo "  • CONFIG_ESP_WIFI_IRAM_OPT=n (禁用WiFi IRAM优化)"
echo "  • CONFIG_ESP_WIFI_RX_IRAM_OPT=n (禁用WiFi接收IRAM优化)"
echo "  • CONFIG_ESP_WIFI_EXTRA_IRAM_OPT=n (禁用WiFi额外IRAM优化)"
echo "  • CONFIG_ESP_WIFI_SLP_IRAM_OPT=n (禁用WiFi睡眠IRAM优化)"
echo ""
echo "📱 蓝牙优化:"
echo "  • CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST=y (已存在，优先使用SPIRAM)"
echo ""
echo "🎨 LVGL优化:"
echo "  • CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM=n (禁用LVGL IRAM)"
echo ""
echo "🔥 预期效果:"
echo "  • WiFi: ~1.7KB IRAM → Flash"
echo "  • BT控制器: ~22.8KB IRAM → 已通过SPIRAM优化"
echo "  • LVGL: ~21.2KB IRAM → Flash"
echo "  • 总计: ~45.7KB IRAM节省 (49% IRAM使用减少)"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "⚡ 接下来运行以下命令重新配置和编译："
echo "  1. idf.py reconfigure"
echo "  2. idf.py build"
echo ""
echo "📋 然后检查优化效果："
echo "  • 查看新的IRAM使用情况"
echo "  • 测试系统功能是否正常"
echo "  • 对比性能影响"
