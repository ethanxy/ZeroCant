# SDDM激光测距无效距离检测功能说明

## 功能概述

为SDDM激光测距模块添加了完善的无效距离检测功能，根据SDDM产品说明书的通信协议，实现了多层次的数据验证机制。

## 检测机制

### 1. 数据有效性检查
```c
// 检查SDDM协议中的DataValidInd字段
if (data_valid != 0x0001) {
    ESP_LOGW(TAG, "SDDM测量数据无效: %04X", data_valid);
    return -1;
}
```

**作用**: 根据SDDM协议，当DataValidInd != 1时表示测量数据无效
**解决问题**: 防止65436mm等无效数据被当作有效结果输出

### 2. 距离范围检查
```c
// 检查距离是否在SDDM量程范围内
if (distance_dm < 30 || distance_dm > 20000) {
    ESP_LOGW(TAG, "距离数据超出合理范围: %d分米", distance_dm);
    return -1;
}
```

**作用**: 验证测量结果是否在SDDM传感器的有效量程内(3-2000米)
**解决问题**: 过滤明显超出物理量程的错误数据

### 3. 错误代码检查
```c
// 检查是否为错误状态码
if (distance_dm >= 0xFF00) {
    ESP_LOGW(TAG, "SDDM返回错误代码: %04X", distance_dm);
    return -1;
}
```

**作用**: 识别0xFF00-0xFFFF范围的错误代码(包括0xFF9C)
**解决问题**: 直接拦截传感器返回的错误状态码

## 用户界面改进

### 详细错误提示
根据不同的错误类型显示相应的用户提示：

- **ESP_ERR_TIMEOUT**: "Measurement timeout - Check target & try again"
- **ESP_FAIL**: "No valid target - Aim at solid surface"  
- **ESP_ERR_INVALID_STATE**: "Sensor error - Check connection"
- **其他错误**: "Measurement failed - Try again"

## 触发无效检测的场景

### 1. 测量天空
- **现象**: DataValidInd = 0, Distance = 0xFF9C
- **检测**: 第1层检查拦截
- **用户看到**: "No valid target - Aim at solid surface"

### 2. 测量透明物体
- **现象**: 激光穿透，无有效反射
- **检测**: 第1层检查拦截
- **用户看到**: "No valid target - Aim at solid surface"

### 3. 超出量程
- **现象**: 距离 > 2000米
- **检测**: 第2层检查拦截
- **用户看到**: "Measurement timeout - Check target & try again"

### 4. 环境干扰
- **现象**: 强光、雾气等影响
- **检测**: 第1层检查拦截
- **用户看到**: "No valid target - Aim at solid surface"

## 技术细节

### SDDM协议响应格式
```
Bytes: 0  1  2  3    4-5      6-7      8
Data:  FB 03 XX 04  AAAA     BBBB     ZZ
意义:  响应头   长度 有效指示  距离(dm) CRC
```

### 65436mm的产生原理
1. 测量无效目标时，SDDM返回DataValidInd=0
2. Distance字段可能包含0xFF9C(65436十进制)
3. 旧代码忽略有效性检查，直接输出65436mm
4. 新代码在第1层检查时就拦截了无效数据

## 日志输出示例

### 成功测量
```
I (12345) laser: 数据有效性指示: 0001
I (12345) laser: SDDM测量成功: 150分米 = 15000毫米
```

### 无效数据(测量天空)
```
I (12345) laser: 数据有效性指示: 0000
W (12345) laser: SDDM测量数据无效: 0000 (可能原因: 无反射目标、超出量程、环境干扰)
```

### 错误代码(65436mm)
```
I (12345) laser: 数据有效性指示: 0000
W (12345) laser: SDDM测量数据无效: 0000
```

## 总结

通过三层检测机制，彻底解决了测量天空等无效目标时返回65436mm的问题：

1. **协议层检查**: 验证DataValidInd字段
2. **物理层检查**: 验证距离范围合理性  
3. **代码层检查**: 识别已知错误代码

现在系统能够：
- ✅ 准确识别无效测量
- ✅ 提供有意义的错误提示
- ✅ 防止显示错误的距离数据
- ✅ 符合SDDM官方协议规范
