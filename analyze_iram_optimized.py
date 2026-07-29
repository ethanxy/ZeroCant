#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
IRAM使用情况详细分析脚本
分析优化后的内存使用
"""

import re
import os

def analyze_iram_usage():
    map_file = "/Users/xuanyuliu/164/build/FactoryProgram.map"
    
    print("🔍 ESP32-S3 IRAM使用情况分析")
    print("=" * 60)
    
    if not os.path.exists(map_file):
        print("❌ Map文件不存在")
        return
    
    try:
        with open(map_file, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
    except Exception as e:
        print(f"❌ 读取Map文件失败: {e}")
        return
    
    # 查找.iram0.text段的总大小
    iram_pattern = r'\.iram0\.text\s+0x[0-9a-f]+\s+0x([0-9a-f]+)'
    match = re.search(iram_pattern, content, re.IGNORECASE)
    
    if not match:
        print("❌ 未找到.iram0.text段信息")
        return
    
    total_size = int(match.group(1), 16)
    iram_limit = 16 * 1024  # 16KB物理IRAM
    
    print(f"📊 IRAM使用总览:")
    print(f"  总使用量: {total_size:,} bytes ({total_size/1024:.1f} KB)")
    print(f"  物理限制: {iram_limit:,} bytes (16.0 KB)")
    print(f"  使用率: {(total_size/iram_limit)*100:.1f}%")
    
    if total_size <= iram_limit:
        print("  状态: ✅ 在物理IRAM范围内")
    else:
        overflow = total_size - iram_limit
        print(f"  状态: ⚡ 超出 {overflow:,} bytes ({overflow/1024:.1f} KB)")
        print("         通过MMU重定向到Cache-Flash执行")
    
    print(f"\n📋 组件使用分析:")
    print("-" * 60)
    
    # 分析具体组件使用情况
    components = {}
    
    # 获取IRAM段的详细内容
    iram_section_start = content.find('.iram0.text')
    if iram_section_start != -1:
        next_section = content.find('\n.', iram_section_start + 1)
        if next_section == -1:
            next_section = len(content)
        
        iram_content = content[iram_section_start:next_section]
        
        # 分析每一行中的组件使用
        lines = iram_content.split('\n')
        for line in lines:
            # 查找包含大小信息的行
            size_match = re.search(r'0x[0-9a-f]+\s+0x([0-9a-f]+)', line)
            if size_match:
                size = int(size_match.group(1), 16)
                if size < 10:  # 忽略太小的条目
                    continue
                
                # 识别组件类型
                component_name = "Other"
                
                line_lower = line.lower()
                if 'lvgl' in line_lower or 'lv_' in line:
                    component_name = "LVGL"
                elif 'btdm_app' in line or 'bt' in line_lower:
                    component_name = "Bluetooth"
                elif 'wifi' in line_lower or 'pp.a' in line or 'net80211' in line:
                    component_name = "WiFi"
                elif 'freertos' in line_lower:
                    component_name = "FreeRTOS"
                elif 'esp_system' in line_lower or 'esp_' in line_lower:
                    component_name = "ESP-IDF"
                elif 'hal' in line_lower:
                    component_name = "HAL"
                elif 'soc' in line_lower:
                    component_name = "SOC"
                elif 'spi_flash' in line_lower:
                    component_name = "SPI_Flash"
                elif 'heap' in line_lower:
                    component_name = "Heap"
                elif 'newlib' in line_lower:
                    component_name = "Newlib"
                
                # 累加组件大小
                if component_name not in components:
                    components[component_name] = 0
                components[component_name] += size
    
    # 显示组件使用情况
    if components:
        sorted_components = sorted(components.items(), key=lambda x: x[1], reverse=True)
        
        for component, size in sorted_components:
            percentage = (size / total_size) * 100
            kb_size = size / 1024
            print(f"  {component:12}: {size:6,} bytes ({kb_size:5.1f} KB) - {percentage:5.1f}%")
    
    # WiFi和蓝牙优化效果检查
    print(f"\n✨ 优化效果检查:")
    print("-" * 60)
    
    wifi_in_iram = 'wifi0iram' in content or 'wifirxiram' in content
    bt_in_iram = 'libbtdm_app.a' in iram_content if 'iram_content' in locals() else False
    
    wifi_size = components.get("WiFi", 0)
    bt_size = components.get("Bluetooth", 0)
    
    print(f"🌐 WiFi IRAM优化:")
    print(f"   当前使用: {wifi_size:,} bytes ({wifi_size/1024:.1f} KB)")
    if wifi_size < 1000:  # 少于1KB说明优化生效
        print("   状态: ✅ WiFi IRAM优化生效")
    else:
        print("   状态: ⚠️  WiFi仍在使用较多IRAM")
    
    print(f"📱 蓝牙IRAM优化:")
    print(f"   当前使用: {bt_size:,} bytes ({bt_size/1024:.1f} KB)")
    if bt_size < 5000:  # 少于5KB说明优化生效
        print("   状态: ✅ 蓝牙IRAM优化生效")
    else:
        print("   状态: ⚠️  蓝牙仍在使用较多IRAM")
    
    # 检查配置生效情况
    print(f"\n🔧 配置检查:")
    print("-" * 60)
    
    try:
        with open("/Users/xuanyuliu/164/build/config/sdkconfig.h", 'r') as f:
            config_content = f.read()
        
        wifi_iram_opt = "#define CONFIG_ESP_WIFI_IRAM_OPT 1" in config_content
        wifi_rx_iram_opt = "#define CONFIG_ESP_WIFI_RX_IRAM_OPT 1" in config_content
        lvgl_iram = "#define CONFIG_LV_ATTRIBUTE_FAST_MEM_USE_IRAM 1" in config_content
        bt_spiram = "#define CONFIG_BT_ALLOCATION_FROM_SPIRAM_FIRST 1" in config_content
        
        print(f"WiFi IRAM优化: {'❌ 启用' if wifi_iram_opt else '✅ 禁用'}")
        print(f"WiFi RX IRAM优化: {'❌ 启用' if wifi_rx_iram_opt else '✅ 禁用'}")
        print(f"LVGL IRAM使用: {'⚠️  启用' if lvgl_iram else '✅ 禁用'}")
        print(f"蓝牙SPIRAM优先: {'✅ 启用' if bt_spiram else '❌ 禁用'}")
        
    except Exception as e:
        print(f"配置检查失败: {e}")

if __name__ == "__main__":
    analyze_iram_usage()
