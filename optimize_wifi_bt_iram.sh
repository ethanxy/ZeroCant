#!/bin/bash
# WiFi和蓝牙IRAM优化脚本
# 将WiFi和蓝牙组件从IRAM移到Flash中执行，释放IRAM空间

echo "🔧 开始WiFi和蓝牙IRAM优化..."

# 备份当前配置
cp sdkconfig.defaults sdkconfig.defaults.backup_wifi_bt_$(date +%Y%m%d_%H%M%S)
echo "✅ 已备份配置文件"

# 添加WiFi IRAM禁用配置
echo ""
echo "# === WiFi和蓝牙IRAM优化配置 ===" >> sdkconfig.defaults
echo "# 禁用WiFi IRAM优化，让WiFi代码在Flash中执行" >> sdkconfig.defaults
echo "CONFIG_ESP_WIFI_IRAM_OPT=n" >> sdkconfig.defaults
echo "CONFIG_ESP_WIFI_RX_IRAM_OPT=n" >> sdkconfig.defaults
echo "CONFIG_ESP_WIFI_EXTRA_IRAM_OPT=n" >> sdkconfig.defaults
echo "CONFIG_ESP_WIFI_SLP_IRAM_OPT=n" >> sdkconfig.defaults
echo "" >> sdkconfig.defaults

echo "# 蓝牙IRAM优化 - 确保使用SPIRAM和Flash" >> sdkconfig.defaults
echo "# CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST=y 已存在，保持启用" >> sdkconfig.defaults
echo "" >> sdkconfig.defaults

echo "🔧 已添加WiFi和蓝牙IRAM优化配置"

# 显示变更摘要
echo ""
echo "📊 优化配置摘要："
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo "🌐 WiFi优化:"
echo "  • CONFIG_ESP_WIFI_IRAM_OPT=n (禁用WiFi基础IRAM优化)"
echo "  • CONFIG_ESP_WIFI_RX_IRAM_OPT=n (禁用WiFi接收IRAM优化)"
echo "  • CONFIG_ESP_WIFI_EXTRA_IRAM_OPT=n (禁用WiFi额外IRAM优化)"
echo "  • CONFIG_ESP_WIFI_SLP_IRAM_OPT=n (禁用WiFi睡眠IRAM优化)"
echo ""
echo "📱 蓝牙优化:"
echo "  • CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST=y (已存在，优先使用SPIRAM)"
echo ""
echo "🔥 预期效果:"
echo "  • WiFi相关代码: IRAM → Flash执行"
echo "  • 蓝牙控制器: 继续使用SPIRAM优化"
echo "  • 估计节省IRAM: 5-15KB"
echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
echo ""

echo "⚡ 接下来执行以下步骤:"
echo "  1. 清理旧的构建文件"
echo "  2. 重新配置项目"
echo "  3. 编译项目"
echo "  4. 分析新的IRAM使用情况"
