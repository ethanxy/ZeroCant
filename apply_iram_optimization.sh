#!/bin/bash
# ESP32-S3 根本性IRAM优化脚本

echo "🚀 开始ESP32-S3根本性IRAM优化..."
echo "目标：将IRAM使用从93.5KB降至12KB以内"
echo ""

# 备份当前配置
echo "📋 备份当前配置..."
cp sdkconfig sdkconfig.backup.$(date +%Y%m%d_%H%M%S)
cp sdkconfig.defaults sdkconfig.defaults.backup.$(date +%Y%m%d_%H%M%S)

echo "✅ 配置已备份"

# 应用新的优化配置
echo ""
echo "🔧 应用激进IRAM优化配置..."

# 将优化配置追加到sdkconfig.defaults
cat sdkconfig_iram_optimized.defaults >> sdkconfig.defaults

echo "✅ 新配置已添加到 sdkconfig.defaults"

# 清理并重新配置
echo ""
echo "🧹 清理构建缓存..."
rm -rf build/
rm -f sdkconfig

echo "✅ 构建缓存已清理"

echo ""
echo "⚙️  重新配置项目..."
idf.py reconfigure

echo ""
echo "🔨 开始构建项目..."
echo "预期效果："
echo "  • LVGL: 从21.4KB降至0KB (-21.4KB)"
echo "  • FreeRTOS: 从10.7KB降至~3KB (-7KB)"
echo "  • HAL: 从9.7KB降至~3KB (-6KB)"
echo "  • ESP HW Support: 从14.4KB降至~4KB (-10KB)"
echo "  • SPI Flash: 从9.7KB降至~3KB (-6KB)"
echo "  • 其他系统组件: 显著减少"
echo ""
echo "  总预期减少: ~50KB"
echo "  目标IRAM使用: <16KB"
echo ""

idf.py build

if [ $? -eq 0 ]; then
    echo ""
    echo "🎉 构建成功！"
    echo ""
    echo "📊 运行IRAM分析..."
    python3 analyze_iram_optimized.py
    
    echo ""
    echo "🔍 检查优化效果..."
    python3 iram_optimization_comparison.py
else
    echo ""
    echo "❌ 构建失败！"
    echo "可能需要调整某些配置以适应你的具体应用需求"
    echo ""
    echo "🔧 故障排除建议："
    echo "1. 检查是否有应用特定的IRAM依赖"
    echo "2. 某些配置可能与现有代码不兼容"
    echo "3. 考虑逐步应用优化而不是一次性全部应用"
    echo ""
    echo "📋 恢复备份："
    echo "  cp sdkconfig.backup.* sdkconfig"
    echo "  cp sdkconfig.defaults.backup.* sdkconfig.defaults"
fi

echo ""
echo "🏁 优化过程完成！"
