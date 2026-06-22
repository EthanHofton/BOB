#pragma once
#include "driver/ledc.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

// DRV8833 pin definitions
#define MOTOR1_IN1 GPIO_NUM_33
#define MOTOR1_IN2 GPIO_NUM_25
#define MOTOR2_IN1 GPIO_NUM_27
#define MOTOR2_IN2 GPIO_NUM_26
#define DRV_SLEEP GPIO_NUM_14

// PWM config
#define PWM_FREQ_HZ 20000                // 20kHz — above hearing range
#define PWM_RESOLUTION LEDC_TIMER_10_BIT // 0–1023
#define PWM_MAX_DUTY ((1 << 10) - 1)     // 1023

// Default motor deadpoint (normalised 0–1) — overridden at runtime via
// drv8833_set_deadpoint().  Calibrate with the Motor Control panel.
#define MOTOR_DEADPOINT_DEFAULT 0.643f

// Get/set the runtime deadpoint.
float drv8833_get_deadpoint(void);
void  drv8833_set_deadpoint(float dp);

// LEDC channel assignments
#define CH_M1_IN1 LEDC_CHANNEL_0
#define CH_M1_IN2 LEDC_CHANNEL_1
#define CH_M2_IN1 LEDC_CHANNEL_2
#define CH_M2_IN2 LEDC_CHANNEL_3

extern void drv8833_init(void);
extern void drv8833_wake(void);
extern void drv8833_sleep(void);

// speed 0.0–1.0: remapped to [MOTOR_DEADPOINT, 1.0] — use for all normal drive.
extern void motor1_forward(float speed);
extern void motor1_reverse(float speed);
extern void motor1_coast(void);
extern void motor1_brake(void);
extern void motor2_forward(float speed);
extern void motor2_reverse(float speed);
extern void motor2_coast(void);
extern void motor2_brake(void);

// speed 0.0–1.0: maps directly to [0, PWM_MAX] with NO deadpoint remap.
// Use only for deadpoint calibration via the Motor Control panel.
extern void motor1_forward_raw(float speed);
extern void motor1_reverse_raw(float speed);
extern void motor2_forward_raw(float speed);
extern void motor2_reverse_raw(float speed);
