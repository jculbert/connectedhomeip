/*
 * HallSensor.cpp
 *
 *  Created on: Sep. 11, 2022
 *      Author: jeff
 */

#include "em_device.h"
// The following (required by later includes) is set by em_device.h
//#define _SILICON_LABS_32B_SERIES_1

#include "em_gpio.h"

#include "sl_i2cspm.h"
#include "em_cmu.h"
#include "sl_i2cspm_sensor_config.h"

//#include "sl_i2cspm_instances.h"
#include "sl_si7210.h"

#include "sl_board_control.h"
#include "sl_board_control_config.h"

#include "HallSensor.h"

// Copied the following from Simplicity Studio I2C app
// Not longer needed as em_device.h seems to be setting this up
//#define I2C1_BASE (0x4000C400UL)
//#define I2C1 ((I2C_TypeDef *) I2C1_BASE)

#define SL_I2CSPM_SENSOR_PERIPHERAL              I2C1
#define SL_I2CSPM_SENSOR_PERIPHERAL_NO           1

sl_i2cspm_t *sl_i2cspm_sensor = SL_I2CSPM_SENSOR_PERIPHERAL;

#if SL_I2CSPM_SENSOR_SPEED_MODE == 0
#define SL_I2CSPM_SENSOR_HLR i2cClockHLRStandard
#define SL_I2CSPM_SENSOR_MAX_FREQ I2C_FREQ_STANDARD_MAX
#elif SL_I2CSPM_SENSOR_SPEED_MODE == 1
#define SL_I2CSPM_SENSOR_HLR i2cClockHLRAsymetric
#define SL_I2CSPM_SENSOR_MAX_FREQ I2C_FREQ_FAST_MAX
#elif SL_I2CSPM_SENSOR_SPEED_MODE == 2
#define SL_I2CSPM_SENSOR_HLR i2cClockHLRFast
#define SL_I2CSPM_SENSOR_MAX_FRHallTimerEventHandlerEQ I2C_FREQ_FASTPLUS_MAX
#endif

static I2CSPM_Init_TypeDef init_sensor = { 
  .port = SL_I2CSPM_SENSOR_PERIPHERAL,
  .sclPort = SL_I2CSPM_SENSOR_SCL_PORT,
  .sclPin = SL_I2CSPM_SENSOR_SCL_PIN,
  .sdaPort = SL_I2CSPM_SENSOR_SDA_PORT,
  .sdaPin = SL_I2CSPM_SENSOR_SDA_PIN,
  .portLocationScl = SL_I2CSPM_SENSOR_SCL_LOC,
  .portLocationSda = SL_I2CSPM_SENSOR_SDA_LOC,
  .i2cRefFreq = 0,
  .i2cMaxFreq = SL_I2CSPM_SENSOR_MAX_FREQ,
  .i2cClhr = SL_I2CSPM_SENSOR_HLR
};

sl_status_t HallSensor::Init()
{
  sl_status_t status;
  //status = sl_board_enable_sensor(SL_BOARD_SENSOR_HALL);
  //if (status != SL_STATUS_OK)
    //return status;
  GPIO_PinModeSet(SL_BOARD_ENABLE_SENSOR_HALL_PORT, SL_BOARD_ENABLE_SENSOR_HALL_PIN, gpioModePushPull, 1);

  CMU_ClockEnable(cmuClock_GPIO, true);
  I2CSPM_Init(&init_sensor);
  
  status = sl_si7210_init(sl_i2cspm_sensor);
  return status;
}

sl_status_t HallSensor::Measure(float *value)
{
  return sl_si7210_measure(sl_i2cspm_sensor, 10000, value);
}
