/**
 * @file robot_motion.h
 * @author 宁子希
 * @brief 机器人运动控制模块
 * @date 2025-04-25
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#ifndef _ROBOT_MOTION_H_
#define _ROBOT_MOTION_H_

#include "iot_servo.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#define MAX_CHANNELS LEDC_CHANNEL_MAX

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 运动类型枚举定义
 */
typedef enum {
    MOTION_TYPE_SINGLE,      // 单点动作
    MOTION_TYPE_PARALLEL,    // 多舵机并行动作
} motion_type_t;

/**
 * @brief 单舵机运动配置参数
 */
typedef struct {
    uint8_t channel;         // 舵机通道
    float start_angle;       // 起始角度(可选，如未设置则从当前位置开始)
    float target_angle;      // 目标角度
    uint32_t duration_ms;    // 运动持续时间(ms)
} motion_single_cfg_t;

/**
 * @brief 多舵机并行运动配置参数
 */
typedef struct {
    uint8_t channel_count;               // 舵机通道数量
    uint8_t channels[MAX_CHANNELS];      // 舵机通道数组
    float target_angles[MAX_CHANNELS];   // 目标角度数组
    float start_angles[MAX_CHANNELS];    // 起始角度数组(可选)
    uint32_t duration_ms;                // 运动持续时间(ms)
} motion_parallel_cfg_t;

/**
 * @brief 动作序列配置参数
 */
typedef struct {
    motion_type_t type;                    // 运动类型
    uint8_t channels[MAX_CHANNELS];        // 舵机通道数组
    float angles[MAX_CHANNELS];            // 目标角度数组
    uint8_t channel_count;                 // 舵机通道数量
    uint32_t duration_ms;                  // 运动持续时间(ms)
    uint16_t delay_after;                  // 运动后的延迟时间(ms)
} motion_sequence_t;

/**
 * @brief 运动命令结构
 */
typedef struct {
    motion_type_t type;                     // 运动类型
    uint16_t motion_id;                     // 运动ID
    union {
        motion_single_cfg_t single;         // 单舵机运动配置参数
        motion_parallel_cfg_t parallel;     // 多舵机并行运动配置参数
    } motion;
} motion_cmd_t;

/**
 * @brief 运动控制器状态结构
 */
typedef struct {
    QueueHandle_t command_queue;           // 运动命令队列句柄
    TaskHandle_t motion_task;              // 运动任务句柄
    bool is_running;                       // 运动是否正在进行
    uint16_t current_motion_id;            // 当前正在进行的运动ID
} motion_ctrl_t;

/**
 * @brief 初始化运动控制器
 * @param controller 指向运动控制器结构的指针
 * @return 成功时返回 ESP_OK，失败时返回错误代码
 */
esp_err_t motion_init(motion_ctrl_t* controller);

/**
 * @brief 添加单舵机运动命令
 * @param controller 指向运动控制器结构的指针
 * @param channel 舵机通道号
 * @param target_angle 目标角度
 * @param duration_ms 运动持续时间(毫秒)
 * @return 成功时返回 ESP_OK，失败时返回错误代码
 */
esp_err_t motion_add_single(motion_ctrl_t* controller, 
                           uint8_t channel, 
                           float target_angle, 
                           uint32_t duration_ms);

/**
 * @brief 添加并行运动命令
 * @param controller 指向运动控制器结构的指针
 * @param channel_count 舵机数量
 * @param channels 舵机通道数组
 * @param target_angles 目标角度数组
 * @param duration_ms 运动持续时间(毫秒)
 * @return 成功时返回 ESP_OK，失败时返回错误代码
 */
esp_err_t motion_add_parallel(motion_ctrl_t* controller, 
                             uint8_t channel_count,
                             uint8_t* channels,
                             float* target_angles,
                             uint32_t duration_ms);

/**
 * @brief 解析并执行JSON格式的运动序列
 * @param controller 指向运动控制器结构的指针
 * @param json_sequence JSON格式的运动序列字符串
 * @return 成功时返回 ESP_OK，失败时返回错误代码
 */
esp_err_t motion_exec_json(motion_ctrl_t* controller, const char* json_sequence);

/**
 * @brief 执行预设动作序列
 * @param controller 指向运动控制器结构的指针
 * @param motions 动作序列数组 motion_sequence_t 类型的指针，指向动作序列数组的起始地址。
 * @param count 动作数量
 * @return 成功时返回 ESP_OK，失败时返回错误代码
 */
esp_err_t motion_exec_sequence(motion_ctrl_t* controller, const motion_sequence_t* motions, uint16_t count);

/**
 * @brief 停止所有运动
 * @param controller 指向运动控制器结构的指针
 * @return 成功时返回 ESP_OK，失败时返回错误代码
 */
esp_err_t motion_stop_all(motion_ctrl_t* controller);

/**
 * @brief 获取当前运动状态
 * @param controller 指向运动控制器结构的指针
 * @return true表示正在运动，false表示空闲
 */
bool motion_is_running(motion_ctrl_t* controller);

/**
 * @brief 销毁运动控制器并释放资源
 * @param controller 指向运动控制器结构的指针
 */
void motion_destroy(motion_ctrl_t* controller);

#ifdef __cplusplus
}
#endif

#endif /* _ROBOT_MOTION_H_ */