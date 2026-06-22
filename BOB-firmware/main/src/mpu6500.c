#include "mpu6500.h"

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <math.h>
#include <string.h>

static const char *TAG = "mpu6500";

// Complementary filter weight — higher = more gyro, less accel correction.
// At 500 Hz, 0.98 gives a ~0.1 s time constant (≈ 1.6 Hz crossover).
#define ALPHA 0.98f

// Software EMA on the complementary filter output — second filtering stage.
// At 500 Hz, 0.8 gives fc ≈ 20 Hz.  Reduces derivative-term noise without
// adding meaningful lag for a slow-dynamics balance robot.
#define ANGLE_LPF_ALPHA 0.8f
#define I2C_TIMEOUT_MS 50

// ── Helpers ──────────────────────────────────────────────────────────────────

static float accel_lsb_to_ms2(mpu6500_accel_fs_t fs) {
  const float g = 9.80665f;
  switch (fs) {
  case ACCEL_FS_2G:
    return g / 16384.0f;
  case ACCEL_FS_4G:
    return g / 8192.0f;
  case ACCEL_FS_8G:
    return g / 4096.0f;
  case ACCEL_FS_16G:
    return g / 2048.0f;
  default:
    return g / 16384.0f;
  }
}

static float gyro_lsb_to_dps(mpu6500_gyro_fs_t fs) {
  switch (fs) {
  case GYRO_FS_250DPS:
    return 1.0f / 131.0f;
  case GYRO_FS_500DPS:
    return 1.0f / 65.5f;
  case GYRO_FS_1000DPS:
    return 1.0f / 32.8f;
  case GYRO_FS_2000DPS:
    return 1.0f / 16.4f;
  default:
    return 1.0f / 131.0f;
  }
}

static inline int16_t bytes_to_int16(uint8_t hi, uint8_t lo) {
  return (int16_t)((hi << 8) | lo);
}

// ── I2C ──────────────────────────────────────────────────────────────────────

static esp_err_t i2c_write_reg(uint8_t reg, uint8_t val) {
  uint8_t buf[2] = {reg, val};
  return i2c_master_write_to_device(MPU6500_I2C_PORT, MPU6500_I2C_ADDR, buf,
                                    sizeof(buf), pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

static esp_err_t i2c_read_regs(uint8_t reg, uint8_t *dst, size_t len) {
  return i2c_master_write_read_device(MPU6500_I2C_PORT, MPU6500_I2C_ADDR, &reg,
                                      1, dst, len,
                                      pdMS_TO_TICKS(I2C_TIMEOUT_MS));
}

// ── AD0 ──────────────────────────────────────────────────────────────────────

static void ad0_init(void) {
  gpio_config_t io_conf = {
      .pin_bit_mask = (1ULL << MPU6500_AD0_PIN),
      .mode = GPIO_MODE_OUTPUT,
      .pull_up_en = GPIO_PULLUP_DISABLE,
      .pull_down_en = GPIO_PULLDOWN_DISABLE,
      .intr_type = GPIO_INTR_DISABLE,
  };
  gpio_config(&io_conf);
  gpio_set_level(MPU6500_AD0_PIN, 1); // HIGH = 0x69
}

// ── Public API ───────────────────────────────────────────────────────────────

esp_err_t mpu6500_init(mpu6500_handle_t *handle, mpu6500_accel_fs_t accel_fs,
                       mpu6500_gyro_fs_t gyro_fs) {
  memset(handle, 0, sizeof(*handle));
  handle->accel_fs = accel_fs;
  handle->gyro_fs = gyro_fs;

  ad0_init();

  i2c_config_t conf = {
      .mode = I2C_MODE_MASTER,
      .sda_io_num = MPU6500_SDA_PIN,
      .scl_io_num = MPU6500_SCL_PIN,
      .sda_pullup_en = GPIO_PULLUP_ENABLE,
      .scl_pullup_en = GPIO_PULLUP_ENABLE,
      .master.clk_speed = MPU6500_I2C_FREQ_HZ,
  };
  ESP_ERROR_CHECK(i2c_param_config(MPU6500_I2C_PORT, &conf));
  ESP_ERROR_CHECK(
      i2c_driver_install(MPU6500_I2C_PORT, I2C_MODE_MASTER, 0, 0, 0));
  ESP_ERROR_CHECK(
      i2c_set_timeout(MPU6500_I2C_PORT, 0xFFFFF)); // ~13ms at 80MHz APB

  uint8_t who_am_i = 0;
  esp_err_t ret = i2c_read_regs(MPU6500_REG_WHO_AM_I, &who_am_i, 1);
  if (ret != ESP_OK || who_am_i != MPU6500_WHO_AM_I_VAL) {
    ESP_LOGE(TAG, "WHO_AM_I check failed: got 0x%02X, expected 0x%02X",
             who_am_i, MPU6500_WHO_AM_I_VAL);
    return ESP_ERR_NOT_FOUND;
  }
  ESP_LOGI(TAG, "MPU6500 found (WHO_AM_I=0x%02X)", who_am_i);

  ESP_ERROR_CHECK(i2c_write_reg(MPU6500_REG_PWR_MGMT_1, 0x80)); // reset
  vTaskDelay(pdMS_TO_TICKS(100));
  ESP_ERROR_CHECK(
      i2c_write_reg(MPU6500_REG_PWR_MGMT_1, 0x01)); // wake, PLL clock
  ESP_ERROR_CHECK(i2c_write_reg(MPU6500_REG_SMPLRT_DIV, 0x00));
  // DLPF_CFG=3: gyro BW=42Hz, 4.9ms delay — attenuates motor-vibration noise
  ESP_ERROR_CHECK(i2c_write_reg(MPU6500_REG_CONFIG, 0x03));
  ESP_ERROR_CHECK(i2c_write_reg(MPU6500_REG_GYRO_CONFIG, (uint8_t)gyro_fs));
  ESP_ERROR_CHECK(i2c_write_reg(MPU6500_REG_ACCEL_CONFIG, (uint8_t)accel_fs));
  // A_DLPF_CFG=3: accel BW=41Hz, 11.8ms delay — accel only corrects slow drift so extra lag is fine
  ESP_ERROR_CHECK(i2c_write_reg(MPU6500_REG_ACCEL_CONFIG2, 0x03));

  ESP_LOGI(TAG, "MPU6500 initialised");
  return ESP_OK;
}

esp_err_t mpu6500_calibrate(mpu6500_handle_t *handle, uint16_t num_samples) {
  ESP_LOGI(TAG, "Calibrating gyro — keep device still (%d samples)",
           num_samples);

  double sum_x = 0, sum_y = 0, sum_z = 0;
  uint16_t valid_samples = 0;
  uint8_t buf[6];
  float scale = gyro_lsb_to_dps(handle->gyro_fs);

  for (uint16_t i = 0; i < num_samples; i++) {
    esp_err_t ret = i2c_read_regs(MPU6500_REG_GYRO_XOUT_H, buf, 6);
    if (ret != ESP_OK) {
      ESP_LOGW(TAG, "Sample %d failed (%s), skipping", i, esp_err_to_name(ret));
      vTaskDelay(pdMS_TO_TICKS(10));
      continue;
    }
    sum_x += bytes_to_int16(buf[0], buf[1]) * scale;
    sum_y += bytes_to_int16(buf[2], buf[3]) * scale;
    sum_z += bytes_to_int16(buf[4], buf[5]) * scale;
    valid_samples++;
    vTaskDelay(pdMS_TO_TICKS(5)); // 200Hz
  }

  if (valid_samples == 0) {
    ESP_LOGE(TAG, "Calibration failed — no valid samples");
    return ESP_FAIL;
  }

  if (valid_samples < num_samples) {
    ESP_LOGW(TAG, "Calibration used %d/%d samples", valid_samples, num_samples);
  }

  handle->gyro_offset_x = (float)(sum_x / valid_samples);
  handle->gyro_offset_y = (float)(sum_y / valid_samples);
  handle->gyro_offset_z = (float)(sum_z / valid_samples);

  ESP_LOGI(TAG, "Gyro offsets: X=%.4f Y=%.4f Z=%.4f °/s", handle->gyro_offset_x,
           handle->gyro_offset_y, handle->gyro_offset_z);

  return ESP_OK;
}

esp_err_t mpu6500_read(mpu6500_handle_t *handle, mpu6500_data_t *out) {
  uint8_t buf[14];
  esp_err_t ret = i2c_read_regs(MPU6500_REG_ACCEL_XOUT_H, buf, 14);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "I2C read failed: %s", esp_err_to_name(ret));
    return ret;
  }

  float a_scale = accel_lsb_to_ms2(handle->accel_fs);
  float g_scale = gyro_lsb_to_dps(handle->gyro_fs);

  out->accel_x = bytes_to_int16(buf[0], buf[1]) * a_scale;
  out->accel_y = bytes_to_int16(buf[2], buf[3]) * a_scale;
  out->accel_z = bytes_to_int16(buf[4], buf[5]) * a_scale;

  int16_t raw_temp = bytes_to_int16(buf[6], buf[7]);
  out->temp_c = (raw_temp / 340.0f) + 36.53f;

  out->gyro_x =
      bytes_to_int16(buf[8], buf[9]) * g_scale - handle->gyro_offset_x;
  out->gyro_y =
      bytes_to_int16(buf[10], buf[11]) * g_scale - handle->gyro_offset_y;
  out->gyro_z =
      bytes_to_int16(buf[12], buf[13]) * g_scale - handle->gyro_offset_z;

  // Accel-derived angles
  float accel_pitch = atan2f(out->accel_x, sqrtf(out->accel_y * out->accel_y +
                                                 out->accel_z * out->accel_z)) *
                      (180.0f / M_PI);
  float accel_roll = atan2f(out->accel_y, sqrtf(out->accel_x * out->accel_x +
                                                out->accel_z * out->accel_z)) *
                     (180.0f / M_PI);

  // Seed filters from accel on first read rather than assuming 0°
  if (handle->last_update_us == 0) {
    handle->pitch          = accel_pitch;
    handle->roll           = accel_roll;
    handle->pitch_filtered = accel_pitch;
    handle->roll_filtered  = accel_roll;
    handle->last_update_us = esp_timer_get_time();
    out->pitch = handle->pitch_filtered;
    out->roll  = handle->roll_filtered;
    return ESP_OK;
  }

  int64_t now_us = esp_timer_get_time();
  float dt = (now_us - handle->last_update_us) / 1e6f;
  handle->last_update_us = now_us;

  if (dt <= 0.0f || dt > 0.5f)
    dt = 0.01f;

  handle->pitch =
      ALPHA * (handle->pitch + out->gyro_x * dt) + (1.0f - ALPHA) * accel_pitch;
  handle->roll =
      ALPHA * (handle->roll + out->gyro_y * dt) + (1.0f - ALPHA) * accel_roll;

  // Software EMA — second stage after complementary filter
  handle->pitch_filtered =
      ANGLE_LPF_ALPHA * handle->pitch_filtered + (1.0f - ANGLE_LPF_ALPHA) * handle->pitch;
  handle->roll_filtered =
      ANGLE_LPF_ALPHA * handle->roll_filtered + (1.0f - ANGLE_LPF_ALPHA) * handle->roll;

  out->pitch = handle->pitch_filtered;
  out->roll  = handle->roll_filtered;

  return ESP_OK;
}
