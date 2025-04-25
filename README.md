# 机器人运动控制模块

## 快速开始

### 1️⃣初始化舵机
```c
    servo_config_t servo_cfg = {
        .max_angle = 180,
        .min_width_us = 500,
        .max_width_us = 2500,
        .freq = 50,
        .timer_number = LEDC_TIMER_0,
        .channels = {
            .servo_pin = {
                GPIO_NUM_18, GPIO_NUM_19, GPIO_NUM_21, GPIO_NUM_22,
            },
            .ch = {
                LEDC_CHANNEL_0,
                LEDC_CHANNEL_1,
                LEDC_CHANNEL_2,
                LEDC_CHANNEL_3,
            },
        },
        .channel_number = 4,
    };
    
    ESP_ERROR_CHECK(iot_servo_init(LEDC_LOW_SPEED_MODE, &servo_cfg_ls));
```
### 2️⃣初始化运动控制器
```c
    motion_ctrl_t controller;
    motion_init(&controller);
```

### 添加运动命令

```c
// 单舵机运动
motion_add_single(&controller, 0, 90.0f, 1000); // 通道0转到90度，耗时1秒

// 多舵机并行运动
uint8_t channels[] = {0,1,2}; // 通道0,1,2
float angles[] = {45.0f, 60.0f, 30.0f}; // 通道0转到45度，通道1转到60度，通道2转到30度
motion_add_parallel(&controller, 3, channels, angles, 1500);  // 三个舵机并行运动，耗时1.5秒
```
### 执行数组动作序列
```c
// 预设动作序列
motion_sequence_t sequence_demo[] = {
    {1, {0,1,2,3}, {30,150,30,150}, 4, 1000, 300},
    {1, {0,1,2,3}, {150,30,150,30}, 4, 1000, 300},
    {1, {0,1,2,3}, {90,90,90,90}, 4, 800, 500},
    {1, {0,1,2,3}, {10,170,170,10}, 4, 1200, 0}
};
motion_exec_sequence(&controller, sequence_demo, sizeof(sequence_demo) / sizeof(sequence_demo[0]));
```
每一个motion_sequence_t类型的元素代表一个动作，其中：
- `type`: 动作类型，0为单舵机，1为多舵机
- `channels`: 舵机通道数组
- `target_angles`: 目标角度数组
- `duration_ms`: 动作持续时间
- `delay_after`: 动作后延迟时间

使用`motion_exec_sequence`会自动按照顺序执行动作序列，直到所有动作执行完毕。

### 执行JSON动作序列

#### 默认JSON格式 (详细字段名)
```json
{
  "motions": [
    {
      "type": "parallel",       // 动作类型: parallel(并行)或single(单舵机)
      "channels": [0,1,2,3],    // 舵机通道数组
      "target_angles": [90,90,90,90], // 目标角度数组
      "duration_ms": 1000,     // 动作持续时间(毫秒)
      "delay_after": 500        // 动作完成后的延迟时间(毫秒)
    },
    {
      "type": "single",
      "channel": 0,             // 单舵机通道号
      "target_angle": 45,       // 单舵机目标角度
      "duration_ms": 800,
      "delay_after": 200
    }
  ]
}
```

#### 简化版JSON格式 (短字段名)
```json
{
  "m": [
    {
      "t": "p",                // 类型: p(并行)或s(单舵机)
      "cs": [0,1,2,3],         // 通道数组
      "as": [90,90,90,90],     // 角度数组
      "d": 1000,               // 持续时间
      "w": 500                 // 等待时间
    },
    {
      "t": "s",
      "c": 0,                  // 单通道
      "a": 45,                 // 单角度
      "d": 800,
      "w": 200
    }
  ]
}
```

#### 字段对照表

| 原版字段名       | 简化字段名 | 说明                          |
|------------------|------------|-------------------------------|
| motions          | m          | 动作序列数组                  |
| type             | t          | 动作类型(parallel/p, single/s)|
| channels         | cs         | 舵机通道数组                  |
| target_angles    | as         | 目标角度数组                  |
| duration_ms      | d          | 动作持续时间(毫秒)            |
| delay_after      | w          | 动作后延迟时间(毫秒)          |
| channel          | c          | 单舵机通道号                  |
| target_angle     | a          | 单舵机目标角度                |

关于json文本到C/C++ 字符串的转换，可以使用在线工具，如 [tomeko](https://tomeko.net/online_tools/cpp_text_escape.php)。

**使用示例**

```c
// 使用原版JSON
const char* full_json = "..."; // 原版JSON字符串
motion_exec_json(&controller, full_json);

// 使用简化版JSON 
const char* compact_json = "..."; // 简化版JSON字符串
motion_exec_json(&controller, compact_json);
```
注意：两种格式可以混用，解析器会自动识别字段名。
