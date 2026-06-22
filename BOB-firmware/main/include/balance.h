#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

// Robot is considered fallen if pitch exceeds this — motors cut to protect the
// driver
#define BALANCE_FALL_THRESHOLD_DEG 45.0f

// Loop rate — requires CONFIG_FREERTOS_HZ=1000 to achieve sub-10ms periods
#define BALANCE_LOOP_HZ 500
#define BALANCE_LOOP_MS (1000 / BALANCE_LOOP_HZ)

// Starting PID gains — expect to tune these on hardware
// kp: primary restoring force, ki: corrects steady-state lean, kd: damps
// oscillation
#define BALANCE_KP 100.92f
#define BALANCE_KI 0.5f
#define BALANCE_KD 1.2f

// True upright angle — trim until robot holds position
// Positive = lean forward, negative = lean back
#define BALANCE_SETPOINT_DEG 0.0f

// Maps to PWM_MAX_DUTY (1023) from drv8833
#define BALANCE_OUTPUT_LIMIT 1023.0f

// Snapshot of the last completed control loop iteration — safe to read from
// any task via balance_get_telemetry().
typedef struct {
    float roll;           // degrees (complementary filter)
    float pitch;          // degrees (complementary filter)
    float accel_x;        // m/s²
    float accel_y;
    float accel_z;
    float gyro_x;         // °/s
    float gyro_y;
    float gyro_z;
    float temp_c;         // °C
    float pid_output;     // raw PID output before deadband (signed, ±output_limit)
    uint32_t motor_speed; // applied PWM magnitude (0–1023); 0 when coasting
    int64_t timestamp_us; // esp_timer_get_time() at end of that iteration
} balance_telemetry_t;

esp_err_t balance_start(void);
void      balance_stop(void);

// PID parameter updates — safe to call from any task.
void balance_set_gains(float kp, float ki, float kd);
void balance_set_setpoint(float setpoint_deg);
void balance_reset_pid(void);

// Thread-safe reads.
void balance_get_params(float *kp, float *ki, float *kd, float *setpoint,
                        float *coast_threshold);
void balance_get_telemetry(balance_telemetry_t *out);
void balance_set_coast_threshold(float threshold);

// Manual motor override — suspends PID output so motors can be driven
// directly.  Intended for deadband characterisation.  IMU is still read
// every loop and telemetry is still published.
void  balance_set_manual(bool enabled);
bool  balance_is_manual(void);
// -1.0 to +1.0, through the deadpoint remap (same path as the balance PID).
void  balance_manual_drive_speed(float speed);
// -1.0 to +1.0, bypasses the deadpoint remap — use to find where the motor
// actually starts moving.
void  balance_manual_drive_raw(float speed);
void  balance_manual_coast(void);
