# 弹道计算组件 (Ballistic Calculator)

基于py-ballisticcalc算法的ESP32弹道计算引擎，提供完整的弹道学功能。

## 功能特性

### 核心计算引擎
- **RK4数值积分**: 四阶龙格-库塔数值积分算法
- **多拖拽模型**: 支持G1, G2, G5, G6, G7, G8, GI标准拖拽表
- **多BC模型**: 速度相关的弹道系数插值
- **温度敏感性**: 粉末温度对初速的影响建模

### 高级物理效应
- **自旋漂移**: 弹丸自旋导致的侧向偏移
- **科里奥利效应**: 地球自转影响
- **大气建模**: ICAO标准大气模型
- **风层效应**: 多层风速建模

### 实用功能
- **归零计算**: 自动寻找归零角度
- **轨迹计算**: 完整弹道轨迹数据
- **射程卡**: 生成详细射程卡数据
- **危险空间**: 计算目标高度相关的危险空间

## 文件结构

```
components/ballistic_calc/
├── include/
│   ├── ballistic_engine.h              # 主要API接口
│   ├── multi_bc_model.h               # 多BC模型支持
│   └── drag_models_complete.h         # 完整拖拽模型库
├── ballistic_engine.c                 # 核心计算引擎
├── multi_bc_model.c                   # 多BC和温度敏感性
├── drag_models_complete.c             # 所有标准拖拽表
├── ballistic_math.c                   # 数学运算函数
├── ballistic_demo.c                   # 使用示例和演示
└── CMakeLists.txt                     # 构建配置
```

## 快速开始

### 1. 基本使用

```c
#include "ballistic_engine.h"

// 初始化计算器
ballistic_calc_t calc;
ballistic_init(&calc);

// 配置弹药(.308 Winchester)
calc.shot.ammo.bullet_weight_gr = 175.0f;
calc.shot.ammo.base_muzzle_velocity_fps = 2600.0f;

// 设置G7拖拽模型
multi_bc_init(&calc.shot.ammo.bc_model, DRAG_MODEL_G7);
multi_bc_add_point(&calc.shot.ammo.bc_model, 0.0f, 0.25f);
multi_bc_sort_points(&calc.shot.ammo.bc_model);

// 计算轨迹
float zero_angle;
ballistic_find_zero(&calc, 100.0f, &zero_angle);
ballistic_trajectory(&calc, 1000.0f, 100.0f);
```

### 2. 多BC模型

```c
// 配置速度相关的BC模型
multi_bc_init(&calc.shot.ammo.bc_model, DRAG_MODEL_G7);
multi_bc_add_point(&calc.shot.ammo.bc_model, 0.0f, 0.25f);   // 亚音速
multi_bc_add_point(&calc.shot.ammo.bc_model, 1.0f, 0.23f);   // 跨音速
multi_bc_add_point(&calc.shot.ammo.bc_model, 1.5f, 0.22f);   // 超音速
multi_bc_sort_points(&calc.shot.ammo.bc_model);
```

### 3. 温度敏感性

```c
// 配置温度敏感性(-1.6 fps/°F)
temp_sens_init(&calc.shot.ammo.temp_sens, 2600.0f, 70.0f);
temp_sens_calc_coefficient(&calc.shot.ammo.temp_sens, 2540.0f, 32.0f);

// 获取当前温度下的初速
float current_velocity = ammo_get_muzzle_velocity(&calc.shot.ammo, 32.0f);
```

### 4. 高级效应

```c
// 启用自旋漂移
calc.shot.spin_drift.enable_calculation = true;
calc.shot.spin_drift.twist_rate_in = 12.0f;
calc.shot.spin_drift.right_hand_twist = true;

// 启用科里奥利效应
calc.shot.coriolis.enable_calculation = true;
calc.shot.coriolis.latitude_deg = 45.0f;
calc.shot.coriolis.azimuth_deg = 0.0f;  // 正北方向
```

## 拖拽模型对比

| 模型 | 用途 | 典型弹丸 | BC转换系数(相对G1) |
|------|------|----------|------------------|
| G1   | 标准弹丸 | 平底弹、通用弹丸 | 1.000 |
| G2   | J型弹丸 | Aberdeen J弹丸 | 1.365 |
| G5   | 低阻弹丸 | 尖头低阻弹 | 1.833 |
| G6   | 平底弹丸 | 平底标准弹 | 0.839 |
| G7   | VLD弹丸 | 长距离高BC弹 | 2.621 |
| G8   | 短距离弹 | 手枪弹等 | 1.249 |
| GI   | Ingalls | 经典Ingalls表 | 1.000 |

## 主要API函数

### 初始化和配置
```c
esp_err_t ballistic_init(ballistic_calc_t* calc);
void ballistic_set_default_config(engine_config_t* config);
```

### 轨迹计算
```c
esp_err_t ballistic_trajectory(ballistic_calc_t* calc, 
                              float max_range_ft, 
                              float range_step_ft);
```

### 归零计算
```c
esp_err_t ballistic_find_zero(ballistic_calc_t* calc,
                             float zero_distance_yd,
                             float* zero_angle_deg);
```

### 多BC模型
```c
esp_err_t multi_bc_init(multi_bc_model_t* model, uint8_t drag_table_type);
esp_err_t multi_bc_add_point(multi_bc_model_t* model, float mach, float bc);
float multi_bc_get_bc_for_mach(const multi_bc_model_t* model, float mach);
```

### 温度敏感性
```c
void temp_sens_init(temp_sensitivity_t* temp_sens, float ref_velocity_fps, float ref_temp_f);
esp_err_t temp_sens_calc_coefficient(temp_sensitivity_t* temp_sens, float velocity_at_temp_fps, float test_temp_f);
float temp_sens_get_velocity_for_temp(const temp_sensitivity_t* temp_sens, float current_temp_f);
```

## 示例程序

运行完整的演示程序：

```c
// 在main.c中调用
void app_main(void) {
    run_ballistic_demos();
}
```

演示包括：
1. **多BC模型轨迹计算** - 展示速度相关BC模型
2. **不同拖拽模型对比** - 比较G1, G7, G2, G5模型差异
3. **高级效应演示** - 自旋漂移和科里奥利效应
4. **温度敏感性分析** - 温度对弹道的影响

## 性能规格

### 内存使用
- **Flash存储**: ~50KB (包含完整拖拽表)
- **RAM使用**: ~10KB (轨迹点和计算缓存)
- **轨迹点**: 最大100个数据点

### 计算性能
- **积分步长**: 2.5ms (可调节)
- **计算精度**: 单精度浮点(7位有效数字)
- **轨迹计算**: 1000码轨迹 < 50ms @ 240MHz
- **归零算法**: 通常 < 10次迭代收敛

### 支持范围
- **最大射程**: 5000码 (可扩展)
- **海拔范围**: -1000到20000英尺
- **温度范围**: -40°F到120°F
- **马赫数范围**: 0.5到5.0

## 与py-ballisticcalc对比

###  已完全实现
- **多种拖拽模型**: G1, G2, G5, G6, G7, G8, GI标准表
- **多BC模型**: DragModelMultiBC等价功能，速度相关BC插值
- **温度敏感性**: 粉末温度敏感性完整建模
- **RK4积分引擎**: 高精度数值积分算法
- **大气建模**: ICAO标准大气模型
- **风效应**: 多层风速风向建模

### ⚖ 简化实现
- **自旋漂移**: 基于经验公式的简化计算
- **科里奥利效应**: 地球自转一阶近似
- **自定义拖拽函数**: 基于标准表插值替代

### 🔧 ESP32优化
- **内存优化**: 压缩拖拽表，优化数据结构
- **性能优化**: 单精度浮点，快速插值算法
- **API简化**: 适合嵌入式使用的简洁接口

## 物理模型

### 弹道方程
基于牛顿运动定律的6自由度简化模型：

```
dx/dt = vx
dy/dt = vy
dz/dt = vz
dvx/dt = -Drag_x/m
dvy/dt = -g - Drag_y/m
dvz/dt = -Drag_z/m
```

### 阻力计算
```
Drag = 0.5 × ρ × Cd × A × v² × (v/|v|)
```

其中：
- ρ: 空气密度 (随海拔和温度变化)
- Cd: 拖拽系数 (基于马赫数和拖拽模型)
- A: 弹丸截面积
- v: 速度矢量

### 大气模型
ICAO标准大气模型：
```
ρ(h) = ρ₀ × (T(h)/T₀) × (P(h)/P₀)
T(h) = T₀ - L × h    (h < 11km)
P(h) = P₀ × (T(h)/T₀)^(gM/RL)
```

## 应用场景

### 教育研究
- 弹道学原理演示
- 物理仿真实验
- 算法验证测试

### 工程应用
- 嵌入式弹道计算
- 实时轨迹预测
- 射击辅助系统

### 技术验证
- 算法性能测试
- 精度对比分析
- 优化方案验证

## 注意事项

 **重要免责声明**: 

本组件仅用于**教育和技术研究目的**。计算结果包含数学近似和物理简化，**不应用于实际射击活动**。使用者需要：

1. 理解这是简化的物理模型
2. 认识到计算结果的局限性
3. 不将结果用于实际弹道应用
4. 遵守当地法律法规

## 技术支持

### 文档参考
- [py-ballisticcalc GitHub](https://github.com/o-murphy/py-ballisticcalc)
- Applied Ballistics (Bryan Litz)
- Modern Exterior Ballistics (Robert McCoy)

### 开发信息
- **版本**: v2.0 统一版
- **平台**: ESP32-S3 (ESP-IDF 5.x)
- **许可**: 教育研究用途
- **更新**: 2025年8月

---

*本组件是对py-ballisticcalc优秀工作的致敬，专为ESP32平台优化实现。*
