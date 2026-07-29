#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
优化后IRAM使用分析脚本
比较优化前后的IRAM使用情况
"""

import re
import os

def parse_iram_section(map_file_path):
    """解析.map文件中的.iram0.text段"""
    print(f"📁 分析文件: {map_file_path}")
    
    try:
        with open(map_file_path, 'r', encoding='utf-8', errors='ignore') as f:
            content = f.read()
    except Exception as e:
        print(f"❌ 无法读取文件: {e}")
        return {}
    
    # 查找.iram0.text段
    iram_pattern = r'\.iram0\.text\s+0x[0-9a-f]+\s+0x([0-9a-f]+)'
    iram_match = re.search(iram_pattern, content, re.IGNORECASE)
    
    if not iram_match:
        print("⚠️  未找到.iram0.text段")
        return {}
    
    total_size = int(iram_match.group(1), 16)
    print(f"📊 .iram0.text总大小: {total_size} bytes ({total_size/1024:.1f} KB)")
    
    # 查找详细的模块占用
    components = {}
    
    # 查找模块占用模式
    component_patterns = [
        (r'.*lib(\w+)\.a.*?0x[0-9a-f]+\s+0x([0-9a-f]+)', 'library'),
        (r'.*components/(\w+)/.*?0x[0-9a-f]+\s+0x([0-9a-f]+)', 'component'),
        (r'.*(\w+)\.c\.obj.*?0x[0-9a-f]+\s+0x([0-9a-f]+)', 'source_file')
    ]
    
    iram_section_start = content.find('.iram0.text')
    if iram_section_start != -1:
        # 获取IRAM段的内容（到下一个段开始）
        next_section = content.find('\n.', iram_section_start + 1)
        if next_section == -1:
            next_section = len(content)
        
        iram_content = content[iram_section_start:next_section]
        
        # 分析每一行
        for line in iram_content.split('\n'):
            # 查找包含大小信息的行
            size_match = re.search(r'0x[0-9a-f]+\s+0x([0-9a-f]+)', line)
            if size_match:
                size = int(size_match.group(1), 16)
                if size < 10:  # 忽略太小的条目
                    continue
                
                # 尝试识别组件
                component_name = "unknown"
                
                # LVGL相关
                if 'lvgl' in line.lower() or 'lv_' in line:
                    component_name = "LVGL"
                # 蓝牙相关
                elif 'bt' in line.lower() or 'ble' in line.lower() or 'btdm' in line:
                    component_name = "Bluetooth"
                # WiFi相关
                elif 'wifi' in line.lower() or 'pp.a' in line or 'net80211' in line:
                    component_name = "WiFi"
                # 系统组件
                elif 'freertos' in line.lower():
                    component_name = "FreeRTOS"
                elif 'esp_' in line.lower():
                    component_name = "ESP-IDF"
                elif 'hal' in line.lower():
                    component_name = "HAL"
                elif 'soc' in line.lower():
                    component_name = "SOC"
                
                # 累加同类组件的大小
                if component_name not in components:
                    components[component_name] = 0
                components[component_name] += size
    
    return {
        'total_size': total_size,
        'components': components
    }

def compare_optimization():
    """比较优化前后的效果"""
    print("🔄 ESP32-S3 IRAM优化效果分析")
    print("=" * 60)
    
    # 优化前的数据（从之前的分析中获得）
    before_data = {
        'total_size': 95774,  # 93.5KB
        'components': {
            'LVGL': 21716,     # 21.2KB
            'Bluetooth': 23347, # 22.8KB  
            'WiFi': 1740,      # 1.7KB
            'ESP-IDF': 25000,  # 估算
            'FreeRTOS': 8000,  # 估算
            'HAL': 6000,       # 估算
            'SOC': 4000,       # 估算
            'Other': 5971      # 其他
        }
    }
    
    # 分析优化后的数据
    map_file = "/Users/xuanyuliu/164/build/FactoryProgram.map"
    after_data = parse_iram_section(map_file)
    
    if not after_data:
        print("❌ 无法获取优化后的数据")
        return
    
    print("\n📈 优化对比结果:")
    print("-" * 60)
    
    # 总体对比
    before_kb = before_data['total_size'] / 1024
    after_kb = after_data['total_size'] / 1024
    reduction_kb = before_kb - after_kb
    reduction_percent = (reduction_kb / before_kb) * 100
    
    print(f"📊 IRAM总使用:")
    print(f"  优化前: {before_kb:.1f} KB")
    print(f"  优化后: {after_kb:.1f} KB")
    print(f"  减少:   {reduction_kb:.1f} KB ({reduction_percent:.1f}%)")
    
    # ESP32-S3 IRAM限制分析
    iram_limit = 16 * 1024  # 16KB物理IRAM
    print(f"\n🔥 相对于16KB物理IRAM:")
    print(f"  优化前: {(before_data['total_size']/iram_limit)*100:.1f}% (需要MMU重定向)")
    print(f"  优化后: {(after_data['total_size']/iram_limit)*100:.1f}% ", end="")
    if after_data['total_size'] <= iram_limit:
        print("(✅ 在物理IRAM范围内!)")
    else:
        print("(仍需MMU重定向)")
    
    # 详细组件对比
    print(f"\n📋 组件级别对比:")
    print("-" * 60)
    
    all_components = set(before_data['components'].keys()) | set(after_data['components'].keys())
    
    for component in sorted(all_components):
        before_size = before_data['components'].get(component, 0)
        after_size = after_data['components'].get(component, 0)
        change = before_size - after_size
        
        before_kb = before_size / 1024
        after_kb = after_size / 1024
        change_kb = change / 1024
        
        if change > 0:
            status = f"📉 -{change_kb:.1f}KB"
        elif change < 0:
            status = f"📈 +{abs(change_kb):.1f}KB"
        else:
            status = "🔄 无变化"
        
        print(f"  {component:12}: {before_kb:6.1f}KB → {after_kb:6.1f}KB  {status}")
    
    print("\n✨ 优化配置生效情况:")
    print("-" * 60)
    
    # 检查各项优化是否生效
    lvgl_reduction = before_data['components'].get('LVGL', 0) - after_data['components'].get('LVGL', 0)
    wifi_reduction = before_data['components'].get('WiFi', 0) - after_data['components'].get('WiFi', 0)
    bt_reduction = before_data['components'].get('Bluetooth', 0) - after_data['components'].get('Bluetooth', 0)
    
    print(f"🎨 LVGL IRAM禁用: {'✅ 生效' if lvgl_reduction > 0 else '❌ 未生效'} ({lvgl_reduction} bytes)")
    print(f"🌐 WiFi IRAM禁用: {'✅ 生效' if wifi_reduction > 0 else '❌ 未生效'} ({wifi_reduction} bytes)")
    print(f"📱 蓝牙SPIRAM优先: {'✅ 可能生效' if bt_reduction > 0 else '🔄 需检查'} ({bt_reduction} bytes)")
    
    print(f"\n🏆 性能影响评估:")
    print("-" * 60)
    if after_data['total_size'] <= iram_limit:
        print("🚀 所有代码在真实IRAM中运行，性能最佳")
    else:
        overflow = after_data['total_size'] - iram_limit
        print(f"⚡ {overflow} bytes ({overflow/1024:.1f}KB) 通过MMU重定向到Cache-Flash")
        print("   性能略有影响，但仍可接受")

if __name__ == "__main__":
    compare_optimization()
