#pragma once

#include "esp_err.h"
#include <stdint.h>

// I2C + pin config
#define MPU6500_SDA_PIN 21
#define MPU6500_SCL_PIN 22
#define MPU6500_AD0_PIN 32 // driven HIGH = address 0x69
#define MPU6500_I2C_PORT I2C_NUM_0
#define MPU6500_I2C_ADDR 0x69
#define MPU6500_I2C_FREQ_HZ 400000

// Register map
#define MPU6500_REG_SMPLRT_DIV 0x19
#define MPU6500_REG_CONFIG 0x1A
#define MPU6500_REG_GYRO_CONFIG 0x1B
#define MPU6500_REG_ACCEL_CONFIG  0x1C
#define MPU6500_REG_ACCEL_CONFIG2 0x1D
#define MPU6500_REG_ACCEL_XOUT_H  0x3B
#define MPU6500_REG_TEMP_OUT_H 0x41
#define MPU6500_REG_GYRO_XOUT_H 0x43
#define MPU6500_REG_PWR_MGMT_1 0x6B
#define MPU6500_REG_WHO_AM_I 0x75
#define MPU6500_WHO_AM_I_VAL 0x68

typedef enum {
  ACCEL_FS_2G = 0x00,
  ACCEL_FS_4G = 0x08,
  ACCEL_FS_8G = 0x10,
  ACCEL_FS_16G = 0x18,
} mpu6500_accel_fs_t;

typedef enum {
  GYRO_FS_250DPS = 0x00,
  GYRO_FS_500DPS = 0x08,
  GYRO_FS_1000DPS = 0x10,
  GYRO_FS_2000DPS = 0x18,
} mpu6500_gyro_fs_t;

typedef struct {
  float accel_x, accel_y, accel_z; // m/s²
  float gyro_x, gyro_y, gyro_z;    // °/s
  float temp_c;                    // °C
  float pitch;                     // °
  float roll;                      // °
} mpu6500_data_t;

typedef struct {
  float gyro_offset_x;
  float gyro_offset_y;
  float gyro_offset_z;
  mpu6500_accel_fs_t accel_fs;
  mpu6500_gyro_fs_t gyro_fs;
  float pitch;           // complementary filter state
  float roll;
  float pitch_filtered;  // EMA output fed to PID
  float roll_filtered;
  int64_t last_update_us;
} mpu6500_handle_t;

esp_err_t mpu6500_init(mpu6500_handle_t *handle, mpu6500_accel_fs_t accel_fs,
                       mpu6500_gyro_fs_t gyro_fs);

esp_err_t mpu6500_calibrate(mpu6500_handle_t *handle, uint16_t num_samples);
esp_err_t mpu6500_read(mpu6500_handle_t *handle, mpu6500_data_t *out);
