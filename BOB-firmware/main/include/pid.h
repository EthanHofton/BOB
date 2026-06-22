#pragma once

typedef struct {
  // Tuning — set these before calling pid_update
  float kp;
  float ki;
  float kd;

  float
      setpoint; // target angle (degrees) — trim this to find true balance point
  float output_limit; // clamps output to ±output_limit

  // Internal state
  float integral;
  float prev_error;
  float integral_limit; // anti-windup clamp on integral term alone
} pid_handle_t;

void pid_init(pid_handle_t *pid, float kp, float ki, float kd, float setpoint,
              float output_limit);
float pid_update(pid_handle_t *pid, float measurement, float dt);

// Like pid_update but uses a directly measured rate (e.g. gyro °/s) for the
// derivative term instead of a finite difference on measurement. Eliminates
// filter lag in the D path — pass the angular-rate axis that matches the
// balancing plane.
float pid_update_with_rate(pid_handle_t *pid, float measurement, float rate,
                           float dt);

void pid_reset(pid_handle_t *pid);
