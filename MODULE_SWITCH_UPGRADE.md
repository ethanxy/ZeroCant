# 模组切换功能升级

## 🎯 升级目标

将原有的四模组循环切换改为：
- **水平滑动**：仅在Angle、Level、Laser三个主模组之间切换
- **垂直滑动**：控制Settings模组的进入和退出

## 🔄 新的切换逻辑

### **水平滑动（左右滑动）**
仅适用于主模组：Angle ↔ Level ↔ Laser

- **向左滑** → 下一个主模组：
  - Angle → Level → Laser → Angle (循环)
  
- **向右滑** → 上一个主模组：
  - Angle ← Level ← Laser ← Angle (循环)

- **Settings模组中的水平滑动**：
  - 被忽略，不产生任何切换效果

### **垂直滑动（上下滑动）**
控制Settings模组：

- **向下滑** → 进入Settings模组：
  - 从任何主模组都可以进入Settings
  - 自动保存当前主模组状态
  
- **向上滑** → 退出Settings模组：
  - 从Settings返回到上次的主模组
  - 如果已在主模组中，向上滑动被忽略

## 🛠️ 技术实现

### **核心修改文件**
- `components/user_app/user_app.c`

### **主要变更**

#### **1. 增加状态记录变量**
```c
static display_mode_t previous_main_mode = DISPLAY_MODE_ANGLE; // 记录进入settings前的模组
```

#### **2. 扩展滑动方向枚举**
```c
typedef enum {
    SWIPE_NONE,
    SWIPE_LEFT,   // 向左滑 - 切换到下一个主模组
    SWIPE_RIGHT,  // 向右滑 - 切换到上一个主模组
    SWIPE_DOWN,   // 向下滑 - 进入settings模组
    SWIPE_UP      // 向上滑 - 退出settings模组
} swipe_direction_t;
```

#### **3. 更新滑动检测算法**
- 支持水平和垂直两个方向的滑动检测
- 根据 `abs_dx > abs_dy` 判断滑动主方向
- 水平滑动检查Y轴偏移限制
- 垂直滑动检查X轴偏移限制

#### **4. 重写滑动处理逻辑**
- **SWIPE_LEFT/RIGHT**: 仅在主模组(Angle/Level/Laser)间切换
- **SWIPE_DOWN**: 从主模组进入Settings，保存当前状态
- **SWIPE_UP**: 从Settings退出，恢复保存的主模组
- **Settings中的水平滑动**: 直接忽略

#### **5. 更新状态管理回调**
- 在进入Settings时自动保存previous_main_mode
- 确保状态同步的一致性

## 🎮 用户体验

### **主模组循环（水平滑动）**
```
Angle ←→ Level ←→ Laser
 ↑________________↓
```

### **Settings访问（垂直滑动）**
```
任何主模组 ↓ Settings
    ↓          ↑
保存状态    恢复状态
```

### **操作示例**
1. **在Angle模组** → 向左滑 → Level模组
2. **在Level模组** → 向下滑 → Settings模组 (记住Level)
3. **在Settings模组** → 向上滑 → Level模组 (恢复)
4. **在Settings模组** → 向左/右滑 → 无效果

## 🚫 限制和保护

### **无效操作**
- Settings模组中的水平滑动（左右滑）
- 主模组中向上滑动时已不在Settings
- 已在Settings时的向下滑动

### **安全机制**
- 深度休眠区域(右上角)触摸时取消所有滑动检测
- 转换中的重复滑动保护
- UI锁机制防止界面冲突

## 📝 调试信息

### **日志输出示例**
```
🔄 SWIPE LEFT: 0 -> 1 (next main module)
🔄 SWIPE DOWN: 1 -> Settings (saved previous: 1)
🔄 SWIPE UP: Settings -> 1 (restored previous module)
🚫 SWIPE LEFT: Settings mode doesn't support horizontal switching
```

### **状态变化追踪**
```
🔄 SAVED previous main mode: 1 before entering Settings
```

## ✅ 编译状态
- 代码修改完成
- 语法检查通过
- 功能逻辑验证完成

## 🧪 测试建议

### **基础功能测试**
1. 在三个主模组间水平滑动切换
2. 从各主模组向下滑进入Settings
3. 从Settings向上滑退出到正确的主模组

### **边界条件测试**
1. Settings中的水平滑动应被忽略
2. 主模组中的向上滑动应被忽略
3. 已在Settings时向下滑动应被忽略

### **深度休眠优先级测试**
1. 右上角区域长按应优先于滑动检测
2. 深度休眠区域触摸应取消滑动检测

---

**升级完成！新的模组切换逻辑已实现，提供更直观的垂直访问Settings模组方式。**
