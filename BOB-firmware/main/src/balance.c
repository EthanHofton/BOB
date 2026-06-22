#include "balance.h"
#include "drv8833.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "mpu6500.h"
#include "pid.h"

#include <string.h>

static const char *TAG = "balance";

static TaskHandle_t         s_balance_task = NULL;
static volatile bool        s_running      = false;
// s_manual_mode is written from the TCP task (core 0) and read from the
// balance task (core 1).  volatile alone doesn't flush the Xtensa write-buffer
// across cores — use GCC atomic builtins for acquire/release semantics.
static bool                 s_manual_mode  = false;
static pid_handle_t         s_pid;
static SemaphoreHandle_t    s_mutex        = NULL;
static balance_telemetry_t  s_telemetry    = {0};
static float                s_coast_threshold = 0.10f;

#define MANUAL_LOAD()  __atomic_load_n(&s_manual_mode,  __ATOMIC_ACQUIRE)
#define MANUAL_STORE(v) __atomic_store_n(&s_manual_mode, (v), __ATOMIC_RELEASE)

#define COAST_THRESHOLD 0.10f

static void balance_task(void *arg) {
    mpu6500_handle_t *imu = (mpu6500_handle_t *)arg;

    pid_init(&s_pid, BALANCE_KP, BALANCE_KI, BALANCE_KD, BALANCE_SETPOINT_DEG,
             BALANCE_OUTPUT_LIMIT);

    mpu6500_data_t data;
    memset(&data, 0, sizeof(data));
    int64_t last_us = esp_timer_get_time();
    TickType_t last_wake_tick = xTaskGetTickCount();

    ESP_LOGI(TAG, "Balance loop started at %dHz", BALANCE_LOOP_HZ);

    while (s_running) {
        int64_t now_us = esp_timer_get_time();
        float dt = (now_us - last_us) / 1e6f;
        last_us = now_us;

        // output and speed default to 0 — telemetry always reflects reality.
        float output = 0.0f;
        float speed  = 0.0f;

        // Always read the IMU so telemetry stays live regardless of mode.
        bool imu_ok = (mpu6500_read(imu, &data) == ESP_OK);
        if (!imu_ok)
            memset(&data, 0, sizeof(data));

        if (MANUAL_LOAD()) {
            // PID bypassed — motors driven externally via balance_manual_drive_*.
            // Don't touch motors here; don't update output/speed (stay 0).
        } else if (!imu_ok) {
            ESP_LOGW(TAG, "IMU read failed, coasting");
            motor1_coast();
            motor2_coast();
        } else if (data.roll > BALANCE_FALL_THRESHOLD_DEG ||
                   data.roll < -BALANCE_FALL_THRESHOLD_DEG) {
            ESP_LOGW(TAG, "Fall detected (roll=%.1f°), cutting motors", data.roll);
            motor1_coast();
            motor2_coast();
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            pid_reset(&s_pid);
            xSemaphoreGive(s_mutex);
        } else {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            output = -pid_update(&s_pid, data.roll, dt);
            xSemaphoreGive(s_mutex);

            speed = (output < 0.0f ? -output : output) / BALANCE_OUTPUT_LIMIT;

            if (speed < s_coast_threshold) {
                motor1_coast();
                motor2_coast();
                speed = 0.0f;
            } else if (output > 0.0f) {
                motor1_forward(speed);
                motor2_forward(speed);
            } else {
                motor1_reverse(speed);
                motor2_reverse(speed);
            }
        }

        ESP_LOGD(TAG, "roll=%6.2f  out=%6.1f  spd=%.3f", data.roll, output, speed);

        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_telemetry.roll         = data.roll;
        s_telemetry.pitch        = data.pitch;
        s_telemetry.accel_x      = data.accel_x;
        s_telemetry.accel_y      = data.accel_y;
        s_telemetry.accel_z      = data.accel_z;
        s_telemetry.gyro_x       = data.gyro_x;
        s_telemetry.gyro_y       = data.gyro_y;
        s_telemetry.gyro_z       = data.gyro_z;
        s_telemetry.temp_c       = data.temp_c;
        s_telemetry.pid_output   = output;
        s_telemetry.motor_speed  = (uint32_t)(speed * PWM_MAX_DUTY);
        s_telemetry.timestamp_us = now_us;
        xSemaphoreGive(s_mutex);

        vTaskDelayUntil(&last_wake_tick, pdMS_TO_TICKS(BALANCE_LOOP_MS));
    }

    motor1_coast();
    motor2_coast();
    ESP_LOGI(TAG, "Balance loop stopped");
    vTaskDelete(NULL);
}

esp_err_t balance_start(void) {
    if (s_balance_task != NULL) {
        ESP_LOGW(TAG, "Balance task already running");
        return ESP_ERR_INVALID_STATE;
    }

    if (s_mutex == NULL) {
        s_mutex = xSemaphoreCreateMutex();
        configASSERT(s_mutex);
    }

    static mpu6500_handle_t imu;
    ESP_ERROR_CHECK(mpu6500_init(&imu, ACCEL_FS_4G, GYRO_FS_500DPS));
    ESP_ERROR_CHECK(mpu6500_calibrate(&imu, 300));

    s_running = true;
    BaseType_t ret = xTaskCreatePinnedToCore(balance_task, "balance", 4096, &imu,
                                             5, &s_balance_task, 1);
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "Failed to create balance task");
        s_running = false;
        return ESP_FAIL;
    }

    return ESP_OK;
}

void balance_stop(void) {
    s_running      = false;
    s_balance_task = NULL;
}

void balance_set_gains(float kp, float ki, float kd) {
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_pid.kp = kp;
        s_pid.ki = ki;
        s_pid.kd = kd;
        pid_reset(&s_pid);
        xSemaphoreGive(s_mutex);
    } else {
        s_pid.kp = kp;
        s_pid.ki = ki;
        s_pid.kd = kd;
        pid_reset(&s_pid);
    }
    ESP_LOGI(TAG, "Gains updated: kp=%.4f ki=%.4f kd=%.4f", kp, ki, kd);
}

void balance_set_setpoint(float setpoint_deg) {
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_pid.setpoint = setpoint_deg;
        xSemaphoreGive(s_mutex);
    } else {
        s_pid.setpoint = setpoint_deg;
    }
    ESP_LOGI(TAG, "Setpoint updated: %.4f deg", setpoint_deg);
}

void balance_reset_pid(void) {
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        pid_reset(&s_pid);
        xSemaphoreGive(s_mutex);
    } else {
        pid_reset(&s_pid);
    }
    ESP_LOGI(TAG, "PID reset");
}

void balance_get_params(float *kp, float *ki, float *kd, float *setpoint,
                        float *coast_threshold) {
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *kp              = s_pid.kp;
        *ki              = s_pid.ki;
        *kd              = s_pid.kd;
        *setpoint        = s_pid.setpoint;
        *coast_threshold = s_coast_threshold;
        xSemaphoreGive(s_mutex);
    } else {
        *kp = *ki = *kd = *setpoint = *coast_threshold = 0.0f;
    }
}

void balance_set_coast_threshold(float threshold) {
    if (threshold < 0.0f) threshold = 0.0f;
    if (threshold > 1.0f) threshold = 1.0f;
    if (s_mutex) {
        xSemaphoreTake(s_mutex, portMAX_DELAY);
        s_coast_threshold = threshold;
        xSemaphoreGive(s_mutex);
    } else {
        s_coast_threshold = threshold;
    }
}

void balance_get_telemetry(balance_telemetry_t *out) {
    if (s_mutex && xSemaphoreTake(s_mutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        *out = s_telemetry;
        xSemaphoreGive(s_mutex);
    } else {
        memset(out, 0, sizeof(*out));
    }
}

// ---- Manual motor override --------------------------------------------------

void balance_set_manual(bool enabled) {
    MANUAL_STORE(enabled);
    if (enabled) {
        // Immediately coast so the balance task doesn't leave motors running.
        motor1_coast();
        motor2_coast();
        if (s_mutex) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
            pid_reset(&s_pid);
            xSemaphoreGive(s_mutex);
        }
        ESP_LOGI(TAG, "Manual mode ON — PID suspended");
    } else {
        motor1_coast();
        motor2_coast();
        ESP_LOGI(TAG, "Manual mode OFF — PID resumed");
    }
}

bool balance_is_manual(void) {
    return MANUAL_LOAD();
}

void balance_manual_drive_speed(float speed) {
    if (!MANUAL_LOAD()) return;
    float abs_s = speed < 0.0f ? -speed : speed;
    if (abs_s > 1.0f) abs_s = 1.0f;
    if (speed > 0.0f) {
        motor1_forward(abs_s);
        motor2_forward(abs_s);
    } else if (speed < 0.0f) {
        motor1_reverse(abs_s);
        motor2_reverse(abs_s);
    } else {
        motor1_coast();
        motor2_coast();
    }
}

void balance_manual_drive_raw(float speed) {
    if (!MANUAL_LOAD()) return;
    // Bypasses deadpoint remapping — use to sweep for the actual deadpoint.
    float abs_s = speed < 0.0f ? -speed : speed;
    if (abs_s > 1.0f) abs_s = 1.0f;
    if (speed > 0.0f) {
        motor1_forward_raw(abs_s);
        motor2_forward_raw(abs_s);
    } else if (speed < 0.0f) {
        motor1_reverse_raw(abs_s);
        motor2_reverse_raw(abs_s);
    } else {
        motor1_coast();
        motor2_coast();
    }
}

void balance_manual_coast(void) {
    motor1_coast();
    motor2_coast();
}
