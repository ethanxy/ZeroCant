#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
分析ESP32-S3 IRAM分配机制和IRAM_ATTR标记
"""

import re
import os

def analyze_iram_allocation_mechanism():
    print("🔍 ESP32-S3 IRAM分配机制分析")
    print("=" * 60)
    
    map_file = "/Users/xuanyuliu/164/build/FactoryProgram.map"
    
    if not os.path.exists(map_file):
        print("❌ Map文件不存在")
        return
    
    with open(map_file, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    print("📋 1. IRAM分配机制说明:")
    print("-" * 40)
    print("💡 IRAM分配有三种方式:")
    print("   1️⃣ IRAM_ATTR明确标记的函数")
    print("   2️⃣ ESP-IDF配置自动分配到IRAM的函数")
    print("   3️⃣ 链接器脚本自动分配的关键函数")
    print()
    
    # 分析.iram0.text段
    iram_section_start = content.find('.iram0.text')
    if iram_section_start == -1:
        print("❌ 未找到IRAM段")
        return
    
    next_section = content.find('\n.', iram_section_start + 1)
    if next_section == -1:
        next_section = len(content)
    
    iram_content = content[iram_section_start:next_section]
    lines = iram_content.split('\n')
    
    # 分析不同类型的IRAM函数
    explicit_iram = []  # 明确标记IRAM_ATTR的
    config_iram = []    # 配置驱动的IRAM
    auto_iram = []      # 自动分配的IRAM
    
    print("📊 2. IRAM段详细分析:")
    print("-" * 40)
    
    for line in lines:
        if 'PROVIDE' in line or '*fill*' in line or line.strip() == '':
            continue
            
        # 查找函数和大小
        func_match = re.search(r'0x[0-9a-f]+\s+0x([0-9a-f]+)\s+(.+)', line)
        if func_match:
            size = int(func_match.group(1), 16)
            location = func_match.group(2).strip()
            
            if size >= 10:  # 只关注较大的函数
                # 分类分析
                if any(keyword in location.lower() for keyword in ['wifi', 'bt', 'bluetooth']):
                    config_iram.append((location, size, "WiFi/BT配置"))
                elif any(keyword in location.lower() for keyword in ['freertos', 'task', 'queue']):
                    config_iram.append((location, size, "FreeRTOS配置"))
                elif any(keyword in location.lower() for keyword in ['flash', 'spi']):
                    config_iram.append((location, size, "Flash/SPI配置"))
                elif any(keyword in location.lower() for keyword in ['hal_', 'esp_']):
                    config_iram.append((location, size, "HAL/ESP配置"))
                elif 'lvgl' in location.lower() or 'lv_' in location.lower():
                    config_iram.append((location, size, "LVGL配置"))
                else:
                    auto_iram.append((location, size, "系统自动"))
    
    # 显示配置驱动的IRAM分配
    print("🎯 配置驱动的IRAM分配 (可通过CONFIG控制):")
    config_total = 0
    config_by_type = {}
    
    for location, size, category in config_iram:
        config_total += size
        if category not in config_by_type:
            config_by_type[category] = 0
        config_by_type[category] += size
    
    for category, total_size in sorted(config_by_type.items(), key=lambda x: x[1], reverse=True):
        print(f"  {category:15}: {total_size/1024:6.1f} KB")
    
    print(f"\n📊 配置驱动总计: {config_total/1024:.1f} KB")
    
    # 显示自动分配的IRAM
    print(f"\n⚙️  系统自动分配的IRAM:")
    auto_total = 0
    for location, size, category in auto_iram[:10]:  # 显示前10个
        auto_total += size
        print(f"  {location[:50]:50} {size:6} bytes")
    
    if len(auto_iram) > 10:
        print(f"  ... 还有 {len(auto_iram)-10} 个函数")
    
    print(f"\n📊 自动分配总计: {auto_total/1024:.1f} KB")
    
    print(f"\n🔄 3. MMU重定向机制:")
    print("-" * 40)
    print("💡 ESP32-S3的IRAM超出处理:")
    print("   • 物理IRAM: 16KB (0x40380000-0x40384000)")
    print("   • 虚拟IRAM: 584KB+ (通过MMU映射)")
    print("   • 超出部分: 自动重定向到Flash执行")
    print("   • Cache机制: 使用ICache缓存Flash中的代码")
    print()
    print("📈 当前状态:")
    total_iram = (config_total + auto_total) / 1024
    physical_limit = 16
    overflow = total_iram - physical_limit
    print(f"   总IRAM需求: {total_iram:.1f} KB")
    print(f"   物理IRAM:   {physical_limit:.1f} KB")
    print(f"   MMU重定向: {overflow:.1f} KB ({overflow/total_iram*100:.1f}%)")
    
    print(f"\n🛠️  4. 优化策略:")
    print("-" * 40)
    print("✅ 可以通过CONFIG优化的组件:")
    for category, size in sorted(config_by_type.items(), key=lambda x: x[1], reverse=True):
        savings_percent = (size / (config_total + auto_total)) * 100
        print(f"   {category:15}: {size/1024:5.1f} KB (节省{savings_percent:4.1f}%)")
    
    potential_savings = config_total * 0.8  # 假设80%可以优化
    remaining = total_iram - potential_savings/1024
    
    print(f"\n💡 优化潜力:")
    print(f"   可优化部分: {potential_savings/1024:.1f} KB")
    print(f"   优化后预期: {remaining:.1f} KB")
    
    if remaining <= physical_limit:
        print(f"   🎉 可以完全消除MMU重定向!")
    else:
        new_overflow = remaining - physical_limit
        print(f"   ⚡ 剩余MMU重定向: {new_overflow:.1f} KB (大幅改善)")

if __name__ == "__main__":
    analyze_iram_allocation_mechanism()
