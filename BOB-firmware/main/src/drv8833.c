#include "drv8833.h"

#include "driver/gpio.h"

static float s_deadpoint = MOTOR_DEADPOINT_DEFAULT;

float drv8833_get_deadpoint(void) { return s_deadpoint; }
void  drv8833_set_deadpoint(float dp) {
    if (dp < 0.0f) dp = 0.0f;
    if (dp > 0.99f) dp = 0.99f;
    s_deadpoint = dp;
}

static void set_duty(ledc_channel_t ch, uint32_t duty) {
  ledc_set_duty(LEDC_LOW_SPEED_MODE, ch, duty);
  ledc_update_duty(LEDC_LOW_SPEED_MODE, ch);
}

// speed 0–1 → [s_deadpoint, 1.0] → duty.  0 stays 0.
static uint32_t speed_to_duty(float speed) {
  if (speed <= 0.0f) return 0;
  if (speed >= 1.0f) return PWM_MAX_DUTY;
  float dp = s_deadpoint;
  float mapped = dp + (1.0f - dp) * speed;
  return (uint32_t)(mapped * PWM_MAX_DUTY);
}

// speed 0–1 → duty with no remapping — for deadpoint calibration only.
static uint32_t speed_to_duty_raw(float speed) {
  if (speed <= 0.0f) return 0;
  if (speed >= 1.0f) return PWM_MAX_DUTY;
  return (uint32_t)(speed * PWM_MAX_DUTY);
}

static void pwm_init(void) {
  ledc_timer_config_t timer = {
      .speed_mode = LEDC_LOW_SPEED_MODE,
      .timer_num = LEDC_TIMER_0,
      .duty_resolution = PWM_RESOLUTION,
      .freq_hz = PWM_FREQ_HZ,
      .clk_cfg = LEDC_AUTO_CLK,
  };
  ledc_timer_config(&timer);

  const ledc_channel_config_t channels[] = {
      {.channel = CH_M1_IN1, .gpio_num = MOTOR1_IN1, .duty = 0},
      {.channel = CH_M1_IN2, .gpio_num = MOTOR1_IN2, .duty = 0},
      {.channel = CH_M2_IN1, .gpio_num = MOTOR2_IN1, .duty = 0},
      {.channel = CH_M2_IN2, .gpio_num = MOTOR2_IN2, .duty = 0},
  };

  for (int i = 0; i < 4; i++) {
    ledc_channel_config_t ch = channels[i];
    ch.speed_mode = LEDC_LOW_SPEED_MODE;
    ch.timer_sel = LEDC_TIMER_0;
    ch.hpoint = 0;
    ch.intr_type = LEDC_INTR_DISABLE;
    ledc_channel_config(&ch);
  }
}

void drv8833_init(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << DRV_SLEEP),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
  gpio_set_level(DRV_SLEEP, 0);

  pwm_init();
}

void drv8833_wake(void)  { gpio_set_level(DRV_SLEEP, 1); }
void drv8833_sleep(void) { gpio_set_level(DRV_SLEEP, 0); }

void motor1_forward(float speed) { set_duty(CH_M1_IN1, speed_to_duty(speed));     set_duty(CH_M1_IN2, 0); }
void motor1_reverse(float speed) { set_duty(CH_M1_IN1, 0);                         set_duty(CH_M1_IN2, speed_to_duty(speed)); }
void motor1_coast(void)          { set_duty(CH_M1_IN1, 0);                         set_duty(CH_M1_IN2, 0); }
void motor1_brake(void)          { set_duty(CH_M1_IN1, PWM_MAX_DUTY);              set_duty(CH_M1_IN2, PWM_MAX_DUTY); }

void motor2_forward(float speed) { set_duty(CH_M2_IN1, speed_to_duty(speed));     set_duty(CH_M2_IN2, 0); }
void motor2_reverse(float speed) { set_duty(CH_M2_IN1, 0);                         set_duty(CH_M2_IN2, speed_to_duty(speed)); }
void motor2_coast(void)          { set_duty(CH_M2_IN1, 0);                         set_duty(CH_M2_IN2, 0); }
void motor2_brake(void)          { set_duty(CH_M2_IN1, PWM_MAX_DUTY);              set_duty(CH_M2_IN2, PWM_MAX_DUTY); }

void motor1_forward_raw(float speed) { set_duty(CH_M1_IN1, speed_to_duty_raw(speed)); set_duty(CH_M1_IN2, 0); }
void motor1_reverse_raw(float speed) { set_duty(CH_M1_IN1, 0); set_duty(CH_M1_IN2, speed_to_duty_raw(speed)); }
void motor2_forward_raw(float speed) { set_duty(CH_M2_IN1, speed_to_duty_raw(speed)); set_duty(CH_M2_IN2, 0); }
void motor2_reverse_raw(float speed) { set_duty(CH_M2_IN1, 0); set_duty(CH_M2_IN2, speed_to_duty_raw(speed)); }
