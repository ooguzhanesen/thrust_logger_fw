#ifndef INA228_H
#define INA228_H

#ifdef __cplusplus
extern "C" {
#endif

#include "stm32f4xx_hal.h"
#include <stdint.h>
#include <stdbool.h>

/* --- ENUMS --- */
typedef enum {
  INA228_MODE_SHUTDOWN            = 0x00,
  INA228_MODE_TRIG_BUS            = 0x01,
  INA228_MODE_TRIG_SHUNT          = 0x02,
  INA228_MODE_TRIG_BUS_SHUNT      = 0x03,
  INA228_MODE_TRIG_TEMP           = 0x04,
  INA228_MODE_TRIG_TEMP_BUS       = 0x05,
  INA228_MODE_TRIG_TEMP_SHUNT     = 0x06,
  INA228_MODE_TRIG_TEMP_BUS_SHUNT = 0x07,
  INA228_MODE_SHUTDOWN2           = 0x08,
  INA228_MODE_CONT_BUS            = 0x09,
  INA228_MODE_CONT_SHUNT          = 0x0A,
  INA228_MODE_CONT_BUS_SHUNT      = 0x0B,
  INA228_MODE_CONT_TEMP           = 0x0C,
  INA228_MODE_CONT_TEMP_BUS       = 0x0D,
  INA228_MODE_CONT_TEMP_SHUNT     = 0x0E,
  INA228_MODE_CONT_TEMP_BUS_SHUNT = 0x0F
} INA228_Mode_t;

typedef enum {
    INA228_1_SAMPLE     = 0,
    INA228_4_SAMPLES    = 1,
    INA228_16_SAMPLES   = 2,
    INA228_64_SAMPLES   = 3,
    INA228_128_SAMPLES  = 4,
    INA228_256_SAMPLES  = 5,
    INA228_512_SAMPLES  = 6,
    INA228_1024_SAMPLES = 7
} INA228_Averaging_t;

typedef enum {
    INA228_50_us   = 0,
    INA228_84_us   = 1,
    INA228_150_us  = 2,
    INA228_280_us  = 3,
    INA228_540_us  = 4,
    INA228_1052_us = 5,
    INA228_2074_us = 6,
    INA228_4120_us = 7
} INA228_ConversionTime_t;

/* --- SENSOR HANDLE STRUCT --- */
typedef struct {
  I2C_HandleTypeDef *hi2c;
  uint8_t address;          // 8-bit shifted I2C address
  float current_LSB;
  float shunt;
  float maxCurrent;
  bool ADCRange;
  int error;
  uint32_t timeout;         // I2C timeout in milliseconds
} INA228_HandleTypeDef;

/* --- CORE FUNCTIONS --- */
void     INA228_Init(INA228_HandleTypeDef *dev, I2C_HandleTypeDef *hi2c, uint8_t address);
bool     INA228_Begin(INA228_HandleTypeDef *dev);
bool     INA228_IsConnected(INA228_HandleTypeDef *dev);
uint8_t  INA228_GetAddress(INA228_HandleTypeDef *dev);

float    INA228_GetBusVoltage(INA228_HandleTypeDef *dev);
float    INA228_GetShuntVoltage(INA228_HandleTypeDef *dev);
int32_t  INA228_GetShuntVoltageRAW(INA228_HandleTypeDef *dev);
float    INA228_GetCurrent(INA228_HandleTypeDef *dev);
float    INA228_GetPower(INA228_HandleTypeDef *dev);
float    INA228_GetTemperature(INA228_HandleTypeDef *dev);
double   INA228_GetEnergy(INA228_HandleTypeDef *dev);
double   INA228_GetCharge(INA228_HandleTypeDef *dev);

/* --- CONFIG REGISTER 0 --- */
void     INA228_Reset(INA228_HandleTypeDef *dev);
bool     INA228_SetAccumulation(INA228_HandleTypeDef *dev, uint8_t value);
bool     INA228_GetAccumulation(INA228_HandleTypeDef *dev);
void     INA228_SetConversionDelay(INA228_HandleTypeDef *dev, uint8_t steps);
uint8_t  INA228_GetConversionDelay(INA228_HandleTypeDef *dev);
void     INA228_SetTemperatureCompensation(INA228_HandleTypeDef *dev, bool on);
bool     INA228_GetTemperatureCompensation(INA228_HandleTypeDef *dev);
bool     INA228_SetADCRange(INA228_HandleTypeDef *dev, bool flag);
bool     INA228_GetADCRange(INA228_HandleTypeDef *dev);

/* --- CONFIG ADC REGISTER 1 --- */
bool     INA228_SetMode(INA228_HandleTypeDef *dev, INA228_Mode_t mode);
uint8_t  INA228_GetMode(INA228_HandleTypeDef *dev);
bool     INA228_SetBusVoltageConversionTime(INA228_HandleTypeDef *dev, INA228_ConversionTime_t bvct);
uint8_t  INA228_GetBusVoltageConversionTime(INA228_HandleTypeDef *dev);
bool     INA228_SetShuntVoltageConversionTime(INA228_HandleTypeDef *dev, INA228_ConversionTime_t svct);
uint8_t  INA228_GetShuntVoltageConversionTime(INA228_HandleTypeDef *dev);
bool     INA228_SetTemperatureConversionTime(INA228_HandleTypeDef *dev, INA228_ConversionTime_t tct);
uint8_t  INA228_GetTemperatureConversionTime(INA228_HandleTypeDef *dev);
bool     INA228_SetAverage(INA228_HandleTypeDef *dev, INA228_Averaging_t avg);
uint8_t  INA228_GetAverage(INA228_HandleTypeDef *dev);

/* --- SHUNT CALIBRATION REGISTER 2 --- */
int      INA228_SetMaxCurrentShunt(INA228_HandleTypeDef *dev, float maxCurrent, float shunt);
bool     INA228_IsCalibrated(INA228_HandleTypeDef *dev);

/* --- SHUNT TEMPERATURE COEFFICIENT REGISTER 3 --- */
bool     INA228_SetShuntTemperatureCoefficent(INA228_HandleTypeDef *dev, uint16_t ppm);
uint16_t INA228_GetShuntTemperatureCoefficent(INA228_HandleTypeDef *dev);

/* --- DIAGNOSE ALERT REGISTER 11 --- */
void     INA228_SetDiagnoseAlert(INA228_HandleTypeDef *dev, uint16_t flags);
uint16_t INA228_GetDiagnoseAlert(INA228_HandleTypeDef *dev);
void     INA228_SetDiagnoseAlertBit(INA228_HandleTypeDef *dev, uint8_t bit);
void     INA228_ClearDiagnoseAlertBit(INA228_HandleTypeDef *dev, uint8_t bit);
uint16_t INA228_GetDiagnoseAlertBit(INA228_HandleTypeDef *dev, uint8_t bit);

/* --- THRESHOLD AND LIMIT REGISTERS 12-17 --- */
void     INA228_SetShuntOvervoltageTH(INA228_HandleTypeDef *dev, uint16_t threshold);
uint16_t INA228_GetShuntOvervoltageTH(INA228_HandleTypeDef *dev);
void     INA228_SetShuntUndervoltageTH(INA228_HandleTypeDef *dev, uint16_t threshold);
uint16_t INA228_GetShuntUndervoltageTH(INA228_HandleTypeDef *dev);
void     INA228_SetBusOvervoltageTH(INA228_HandleTypeDef *dev, uint16_t threshold);
uint16_t INA228_GetBusOvervoltageTH(INA228_HandleTypeDef *dev);
void     INA228_SetBusUndervoltageTH(INA228_HandleTypeDef *dev, uint16_t threshold);
uint16_t INA228_GetBusUndervoltageTH(INA228_HandleTypeDef *dev);
void     INA228_SetTemperatureOverLimitTH(INA228_HandleTypeDef *dev, uint16_t threshold);
uint16_t INA228_GetTemperatureOverLimitTH(INA228_HandleTypeDef *dev);
void     INA228_SetPowerOverLimitTH(INA228_HandleTypeDef *dev, uint16_t threshold);
uint16_t INA228_GetPowerOverLimitTH(INA228_HandleTypeDef *dev);

/* --- MANUFACTURER and ID --- */
uint16_t INA228_GetManufacturer(INA228_HandleTypeDef *dev);
uint16_t INA228_GetDieID(INA228_HandleTypeDef *dev);
uint16_t INA228_GetRevision(INA228_HandleTypeDef *dev);
int      INA228_GetLastError(INA228_HandleTypeDef *dev);

#ifdef __cplusplus
}
#endif

#endif // INA228_H
