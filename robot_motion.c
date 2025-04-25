/**
 * @file robot_motion.c
 * @author 宁子希
 * @brief 机器人运动控制模块
 * @date 2025-04-25
 * 
 * @copyright Copyright (c) 2025
 * 
 */
#include "robot_motion.h"
#include <string.h> 
#include "cJSON.h"
#include "esp_log.h"
#include <math.h>

static const char* TAG = "RobotMotion";

// 缓动函数 - 二次缓入缓出
static float ease_in_out_quad(float t) {
    return t < 0.5 ? 2 * t * t : 1 - pow(-2 * t + 2, 2) / 2;
}

// 执行单舵机运动
static void execute_single_motion(const motion_single_cfg_t* motion) {
    float current_angle;
    iot_servo_read_angle(LEDC_LOW_SPEED_MODE, motion->channel, &current_angle);
    
    float start_angle = motion->start_angle >= 0 ? motion->start_angle : current_angle;
    uint32_t steps = motion->duration_ms / 20; // 每20ms一步
    if (steps == 0) steps = 1;
    
    for (uint32_t s = 0; s <= steps; s++) {
        float progress = (float)s / steps;
        float eased_progress = ease_in_out_quad(progress);
        float new_angle = start_angle + (motion->target_angle - start_angle) * eased_progress;
        if (new_angle < 0)  new_angle = 0;
        iot_servo_write_angle(LEDC_LOW_SPEED_MODE, motion->channel, new_angle);
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
}

// 执行并行运动
static void execute_parallel_motion(const motion_parallel_cfg_t* motion) {
    float* current_angles = malloc(motion->channel_count * sizeof(float));
    float* start_angles = malloc(motion->channel_count * sizeof(float));

    // 初始化角度
    for (int i = 0; i < motion->channel_count; i++) {
        iot_servo_read_angle(LEDC_LOW_SPEED_MODE, motion->channels[i], &current_angles[i]);
        start_angles[i] = motion->start_angles[i]>0 ? motion->start_angles[i] : current_angles[i];
    }
    
    uint32_t steps = motion->duration_ms / 20; // 每20ms一步
    if (steps == 0) steps = 1;
    
    for (uint32_t s = 0; s <= steps; s++) {
        float progress = (float)s / steps;
        float eased_progress = ease_in_out_quad(progress);
        
        for (int i = 0; i < motion->channel_count; i++) {
            float new_angle = start_angles[i] + 
                            (motion->target_angles[i] - start_angles[i]) * eased_progress;
            if (new_angle < 0)  new_angle = 0;
            iot_servo_write_angle(LEDC_LOW_SPEED_MODE, motion->channels[i], new_angle);
        }
        vTaskDelay(20 / portTICK_PERIOD_MS);
    }
    
    free(current_angles);
    free(start_angles);
}

// 运动控制任务
static void motion_task(void* arg) {
    motion_ctrl_t* controller = (motion_ctrl_t*)arg;
    motion_cmd_t cmd;
    
    while (1) {
        if (xQueueReceive(controller->command_queue, &cmd, portMAX_DELAY) == pdTRUE) {
            controller->is_running = true;
            ESP_LOGI(TAG, "Executing motion ID: %d", cmd.motion_id);
            
            switch (cmd.type) {
                case MOTION_TYPE_SINGLE:
                    execute_single_motion(&cmd.motion.single);
                    break;
                case MOTION_TYPE_PARALLEL:
                    execute_parallel_motion(&cmd.motion.parallel);
                    break;
                default:
                    ESP_LOGW(TAG, "Unknown motion type");
            }
            
            controller->is_running = false;
            ESP_LOGI(TAG, "Motion ID: %d completed", cmd.motion_id);
        }
    }
}

// 初始化运动控制器
esp_err_t motion_init(motion_ctrl_t* controller) {
    controller->command_queue = xQueueCreate(10, sizeof(motion_cmd_t));
    if (controller->command_queue == NULL) {
        ESP_LOGE(TAG, "Failed to create command queue");
        return ESP_FAIL;
    }
    
    BaseType_t ret = xTaskCreate(motion_task, "motion_task", 4096, controller, 5, &controller->motion_task);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create motion task");
        return ESP_FAIL;
    }
    
    controller->is_running = false;
    controller->current_motion_id = 0;
    return ESP_OK;
}

// 添加单舵机运动命令
esp_err_t motion_add_single(motion_ctrl_t* controller, 
                          uint8_t channel, 
                          float target_angle, 
                          uint32_t duration_ms) {
    motion_cmd_t cmd = {
        .type = MOTION_TYPE_SINGLE,
        .motion_id = controller->current_motion_id++,
        .motion.single = {
            .channel = channel,
            .start_angle = -1.0, // 使用-1表示从当前位置开始
            .target_angle = target_angle,
            .duration_ms = duration_ms
        }
    };
    
    if (xQueueSend(controller->command_queue, &cmd, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to add single motion to queue");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// 添加并行运动命令
esp_err_t motion_add_parallel(motion_ctrl_t* controller,
                            uint8_t channel_count,
                            uint8_t* channels,
                            float* target_angles,
                            uint32_t duration_ms) {
    if(channel_count > MAX_CHANNELS) {
        ESP_LOGE(TAG, "Channel count exceeds maximum");
        return ESP_FAIL;
    }
    
    motion_cmd_t cmd = {
        .type = MOTION_TYPE_PARALLEL,
        .motion_id = controller->current_motion_id++,
        .motion.parallel = {
            .channel_count = channel_count,
            .duration_ms = duration_ms
        }
    };
    
    // 拷贝数组数据
    memcpy(cmd.motion.parallel.channels, channels, channel_count * sizeof(uint8_t));
    memcpy(cmd.motion.parallel.target_angles, target_angles, channel_count * sizeof(float));
    memset(cmd.motion.parallel.start_angles, 0, sizeof(cmd.motion.parallel.start_angles));

    if (xQueueSend(controller->command_queue, &cmd, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Failed to add parallel motion to queue");
        return ESP_FAIL;
    }
    return ESP_OK;
}

// 解析并执行JSON格式的运动序列
esp_err_t motion_exec_json(motion_ctrl_t* controller, const char* json_sequence) {
    cJSON* root = cJSON_Parse(json_sequence);
    if (root == NULL) {
        ESP_LOGE(TAG, "Failed to parse JSON");
        return ESP_FAIL;
    }

    cJSON* motions = cJSON_GetObjectItem(root, "motions");
    if (motions == NULL) {
        motions = cJSON_GetObjectItem(root, "m"); // 尝试简化格式
    }
    
    if (!cJSON_IsArray(motions)) {
        ESP_LOGE(TAG, "Invalid motions format");
        cJSON_Delete(root);
        return ESP_FAIL;
    }

    esp_err_t ret = ESP_OK;
    cJSON* motion = NULL;
    cJSON_ArrayForEach(motion, motions) {
        cJSON* type = cJSON_GetObjectItem(motion, "type");
        if (type == NULL) type = cJSON_GetObjectItem(motion, "t");
        
        if (!cJSON_IsString(type)) {
            ESP_LOGE(TAG, "Missing motion type");
            ret = ESP_FAIL;
            break;
        }

        if (strcmp(type->valuestring, "single") == 0 || strcmp(type->valuestring, "s") == 0) {
            cJSON* channel = cJSON_GetObjectItem(motion, "channel");
            if (channel == NULL) channel = cJSON_GetObjectItem(motion, "c");
            cJSON* target_angle = cJSON_GetObjectItem(motion, "target_angle");
            if (target_angle == NULL) target_angle = cJSON_GetObjectItem(motion, "a");
            cJSON* duration_ms = cJSON_GetObjectItem(motion, "duration_ms");
            if (duration_ms == NULL) duration_ms = cJSON_GetObjectItem(motion, "d");

            if (!cJSON_IsNumber(channel) || !cJSON_IsNumber(target_angle) || !cJSON_IsNumber(duration_ms)) {
                ESP_LOGE(TAG, "Invalid single motion parameters");
                ret = ESP_FAIL;
                break;
            }

            ret = motion_add_single(controller, 
                                  channel->valueint,
                                  target_angle->valuedouble,
                                  duration_ms->valueint);
        } 
        else if (strcmp(type->valuestring, "parallel") == 0 || strcmp(type->valuestring, "p") == 0) {
            cJSON* channels = cJSON_GetObjectItem(motion, "channels");
            if (channels == NULL) channels = cJSON_GetObjectItem(motion, "cs");
            cJSON* target_angles = cJSON_GetObjectItem(motion, "target_angles");
            if (target_angles == NULL) target_angles = cJSON_GetObjectItem(motion, "as");
            cJSON* duration_ms = cJSON_GetObjectItem(motion, "duration_ms");
            if (duration_ms == NULL) duration_ms = cJSON_GetObjectItem(motion, "d");

            if (!cJSON_IsArray(channels) || !cJSON_IsArray(target_angles) || 
                !cJSON_IsNumber(duration_ms) || 
                cJSON_GetArraySize(channels) != cJSON_GetArraySize(target_angles)) {
                ESP_LOGE(TAG, "Invalid parallel motion parameters");
                ret = ESP_FAIL;
                break;
            }

            uint8_t channel_array[MAX_CHANNELS];
            float angle_array[MAX_CHANNELS];
            int count = cJSON_GetArraySize(channels);

            for (int i = 0; i < count; i++) {
                cJSON* ch = cJSON_GetArrayItem(channels, i);
                cJSON* angle = cJSON_GetArrayItem(target_angles, i);
                if (!cJSON_IsNumber(ch) || !cJSON_IsNumber(angle)) {
                    ESP_LOGE(TAG, "Invalid channel or angle in parallel motion");
                    ret = ESP_FAIL;
                    break;
                }
                channel_array[i] = ch->valueint;
                angle_array[i] = angle->valuedouble;
            }

            if (ret == ESP_OK) {
                ret = motion_add_parallel(controller, count, channel_array, angle_array, duration_ms->valueint);
            }
        } else {
            ESP_LOGE(TAG, "Unknown motion type: %s", type->valuestring);
            ret = ESP_FAIL;
            break;
        }

        cJSON* delay_after = cJSON_GetObjectItem(motion, "delay_after");
        if (delay_after == NULL) delay_after = cJSON_GetObjectItem(motion, "w");
        if (cJSON_IsNumber(delay_after) && delay_after->valueint > 0) {
            vTaskDelay(delay_after->valueint / portTICK_PERIOD_MS);
        }

        if (ret != ESP_OK) {
            break;
        }
    }

    cJSON_Delete(root);
    return ret;
}

// 执行预设动作序列
esp_err_t motion_exec_sequence(motion_ctrl_t* controller, 
                           const motion_sequence_t* motions, 
                           uint16_t count) {
    if (controller == NULL || motions == NULL) {
        return ESP_FAIL;
    }

    for (uint16_t i = 0; i < count; i++) {
        const motion_sequence_t* m = &motions[i];
        esp_err_t ret = ESP_OK;

        if (m->type == MOTION_TYPE_SINGLE) {
            ret = motion_add_single(controller, m->channels[0], 
                                 m->angles[0], m->duration_ms);
        } else if (m->type == MOTION_TYPE_PARALLEL) {
            ret = motion_add_parallel(controller, m->channel_count,
                                    (uint8_t*)m->channels,
                                    (float*)m->angles,
                                    m->duration_ms);
        } else {
            ESP_LOGE(TAG, "Invalid motion type in preset");
            return ESP_FAIL;
        }

        if (ret != ESP_OK) {
            return ret;
        }

        if (m->delay_after > 0) {
            vTaskDelay(m->delay_after / portTICK_PERIOD_MS);
        }
    }
    return ESP_OK;
}
// 停止所有运动
esp_err_t motion_stop_all(motion_ctrl_t* controller) {
    xQueueReset(controller->command_queue);
    controller->is_running = false;
    return ESP_OK;
}

// 获取当前运动状态
bool motion_is_running(motion_ctrl_t* controller) {
    return controller->is_running;
}

//销毁运动控制器并释放资源
void motion_destroy(motion_ctrl_t* controller) {
    if (controller == NULL) return;
    
    // 停止所有运动
    motion_stop_all(controller);
    
    // 删除任务
    if (controller->motion_task != NULL) {
        vTaskDelete(controller->motion_task);
        controller->motion_task = NULL;
    }
    
    // 删除队列
    if (controller->command_queue != NULL) {
        vQueueDelete(controller->command_queue);
        controller->command_queue = NULL;
    }
    
    controller->is_running = false;
    controller->current_motion_id = 0;
}