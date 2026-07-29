#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ESP32-S3 IRAM根本性解决方案分析
找出所有可以移出IRAM的组件
"""

import re
import os

def analyze_iram_fundamental_solution():
    print("🔧 ESP32-S3 IRAM根本性解决方案分析")
    print("=" * 60)
    
    map_file = "/Users/xuanyuliu/164/build/FactoryProgram.map"
    
    if not os.path.exists(map_file):
        print("❌ Map文件不存在")
        return
    
    with open(map_file, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    # 分析IRAM段的详细内容
    iram_section_start = content.find('.iram0.text')
    if iram_section_start == -1:
        print("❌ 未找到IRAM段")
        return
    
    next_section = content.find('\n.', iram_section_start + 1)
    if next_section == -1:
        next_section = len(content)
    
    iram_content = content[iram_section_start:next_section]
    
    # 详细分析每个库的IRAM使用
    libraries = {}
    lines = iram_content.split('\n')
    
    for line in lines:
        # 查找库文件和大小
        lib_match = re.search(r'(lib\w+\.a)\([^)]+\)', line)
        size_match = re.search(r'0x[0-9a-f]+\s+0x([0-9a-f]+)', line)
        
        if lib_match and size_match:
            lib_name = lib_match.group(1)
            size = int(size_match.group(1), 16)
            
            if size >= 10:  # 只关注大于10字节的
                if lib_name not in libraries:
                    libraries[lib_name] = 0
                libraries[lib_name] += size
    
    # 按大小排序
    sorted_libs = sorted(libraries.items(), key=lambda x: x[1], reverse=True)
    
    print("📊 IRAM使用详细分析 (按库排序):")
    print("-" * 60)
    
    total_analyzed = 0
    for lib, size in sorted_libs:
        percentage = (size / 95755) * 100  # 基于总IRAM大小
        kb_size = size / 1024
        total_analyzed += size
        print(f"{lib:25}: {size:6,} bytes ({kb_size:5.1f} KB) - {percentage:5.1f}%")
    
    print(f"\n已分析总计: {total_analyzed:,} bytes ({total_analyzed/1024:.1f} KB)")
    
    # 分析可优化的组件
    print(f"\n🎯 根本性优化策略:")
    print("-" * 60)
    
    # 策略1: 将所有非关键组件移到Flash
    flash_candidates = [
        ('libesp_system.a', '系统组件', '大部分可移动'),
        ('libfreertos.a', 'FreeRTOS', '部分关键ISR需保留'),
        ('libhal.a', 'HAL层', '部分关键驱动需保留'),
        ('liblvgl.a', 'LVGL图形库', '完全可移动'),
        ('libesp_hw_support.a', '硬件支持', '部分可移动'),
        ('libheap.a', '内存管理', '关键路径需保留'),
        ('libspi_flash.a', 'SPI Flash', '部分可移动'),
        ('libnewlib.a', '标准库', '大部分可移动')
    ]
    
    print("📋 可移动到Flash的组件:")
    potential_savings = 0
    
    for lib, desc, status in flash_candidates:
        if lib in libraries:
            size = libraries[lib]
            potential_savings += size * 0.7  # 假设70%可以移动
            print(f"  {lib:20} ({desc:12}): {size/1024:5.1f} KB - {status}")
    
    print(f"\n💡 预期节省空间: {potential_savings/1024:.1f} KB")
    
    # ESP32-S3特定的解决方案
    print(f"\n🚀 ESP32-S3根本性解决方案:")
    print("-" * 60)
    
    print("1. 🔧 激进的IRAM优化配置:")
    optimizations = [
        "CONFIG_FREERTOS_PLACE_FUNCTIONS_INTO_FLASH=y",
        "CONFIG_HAL_DEFAULT_ASSERTION_LEVEL=0",  
        "CONFIG_ESP_SYSTEM_PANIC_PRINT_HALT=y",
        "CONFIG_ESP_DEBUG_STUBS_ENABLE=n",
        "CONFIG_ESP_INT_WDT=n",
        "CONFIG_ESP_TASK_WDT_EN=n",
        "CONFIG_HEAP_PLACE_FUNCTION_INTO_FLASH=y",
        "CONFIG_SPI_MASTER_IN_IRAM=n",
        "CONFIG_SPI_MASTER_ISR_IN_IRAM=n"
    ]
    
    for opt in optimizations:
        print(f"   {opt}")
    
    print(f"\n2. 🎯 针对性组件优化:")
    print("   • 禁用所有非必要的IRAM优化")
    print("   • 将关键中断处理保留在IRAM")
    print("   • 其他代码全部移至Flash执行")
    
    print(f"\n3. 📐 内存架构重新设计:")
    print("   • 利用ESP32-S3的8MB PSRAM")
    print("   • 关键数据结构使用PSRAM")
    print("   • 代码执行主要依赖Flash+Cache")
    
    print(f"\n4. 🔍 目标效果:")
    iram_limit = 16 * 1024
    current_total = 95755
    
    # 保守估计可以减少的IRAM使用
    essential_iram = 12 * 1024  # 12KB用于真正必要的中断处理等
    
    print(f"   当前IRAM: {current_total/1024:.1f} KB")
    print(f"   目标IRAM: {essential_iram/1024:.1f} KB")
    print(f"   需要减少: {(current_total - essential_iram)/1024:.1f} KB")
    print(f"   减少比例: {((current_total - essential_iram)/current_total)*100:.1f}%")
    
    if essential_iram <= iram_limit:
        print("   🎉 可以实现完全在物理IRAM内运行!")
    else:
        overflow = essential_iram - iram_limit
        print(f"   ⚡ 仍需MMU重定向: {overflow/1024:.1f} KB (大幅改善)")

if __name__ == "__main__":
    analyze_iram_fundamental_solution()
