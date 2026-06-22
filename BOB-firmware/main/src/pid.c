#include "pid.h"

void pid_init(pid_handle_t *pid, float kp, float ki, float kd, float setpoint,
              float output_limit) {
  pid->kp = kp;
  pid->ki = ki;
  pid->kd = kd;
  pid->setpoint = setpoint;
  pid->output_limit = output_limit;
  pid->integral_limit =
      output_limit / 2.0f; // integral can contribute at most half of max output
  pid->integral = 0.0f;
  pid->prev_error = 0.0f;
}

float pid_update(pid_handle_t *pid, float measurement, float dt) {
  if (dt <= 0.0f)
    return 0.0f;

  float error = pid->setpoint - measurement;

  // Integral with anti-windup clamp
  pid->integral += error * dt;
  if (pid->integral > pid->integral_limit)
    pid->integral = pid->integral_limit;
  else if (pid->integral < -pid->integral_limit)
    pid->integral = -pid->integral_limit;

  float derivative = (error - pid->prev_error) / dt;
  pid->prev_error = error;

  float output =
      (pid->kp * error) + (pid->ki * pid->integral) + (pid->kd * derivative);

  // Clamp output
  if (output > pid->output_limit)
    output = pid->output_limit;
  else if (output < -pid->output_limit)
    output = -pid->output_limit;

  return output;
}

float pid_update_with_rate(pid_handle_t *pid, float measurement, float rate,
                           float dt) {
  if (dt <= 0.0f) return 0.0f;

  float error = pid->setpoint - measurement;

  pid->integral += error * dt;
  if (pid->integral > pid->integral_limit)
    pid->integral = pid->integral_limit;
  else if (pid->integral < -pid->integral_limit)
    pid->integral = -pid->integral_limit;

  // d(error)/dt = -d(measurement)/dt, so negate the measured rate.
  float derivative = -rate;
  pid->prev_error  = error;

  float output = pid->kp * error + pid->ki * pid->integral + pid->kd * derivative;

  if (output > pid->output_limit)
    output = pid->output_limit;
  else if (output < -pid->output_limit)
    output = -pid->output_limit;

  return output;
}

void pid_reset(pid_handle_t *pid) {
  pid->integral = 0.0f;
  pid->prev_error = 0.0f;
}
