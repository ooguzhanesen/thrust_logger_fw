#include "ina228.h"
#include <math.h>

// REGISTERS
#define INA228_CONFIG               0x00
#define INA228_ADC_CONFIG           0x01
#define INA228_SHUNT_CAL            0x02
#define INA228_SHUNT_TEMP_CO        0x03
#define INA228_SHUNT_VOLTAGE        0x04
#define INA228_BUS_VOLTAGE          0x05
#define INA228_TEMPERATURE          0x06
#define INA228_CURRENT              0x07
#define INA228_POWER                0x08
#define INA228_ENERGY               0x09
#define INA228_CHARGE               0x0A
#define INA228_DIAG_ALERT           0x0B
#define INA228_SOVL                 0x0C
#define INA228_SUVL                 0x0D
#define INA228_BOVL                 0x0E
#define INA228_BUVL                 0x0F
#define INA228_TEMP_LIMIT           0x10
#define INA228_POWER_LIMIT          0x11
#define INA228_MANUFACTURER         0x3E
#define INA228_DEVICE_ID            0x3F

// MASKS
#define INA228_CFG_RST              0x8000
#define INA228_CFG_RSTACC           0x4000
#define INA228_CFG_CONVDLY          0x3FC0
#define INA228_CFG_TEMPCOMP         0x0020
#define INA228_CFG_ADCRANGE         0x0010
#define INA228_ADC_MODE             0xF000
#define INA228_ADC_VBUSCT           0x0E00
#define INA228_ADC_VSHCT            0x01C0
#define INA228_ADC_VTCT             0x0038
#define INA228_ADC_AVG              0x0007

/* --- PRIVATE HARDWARE ABSTRACTION FUNCTIONS --- */
static uint32_t INA228_ReadRegister(INA228_HandleTypeDef *dev, uint8_t reg, uint8_t bytes) {
  uint8_t buffer[4] = {0};
  dev->error = 0;

  if (HAL_I2C_Mem_Read(dev->hi2c, dev->address, reg, I2C_MEMADD_SIZE_8BIT, buffer, bytes, dev->timeout) != HAL_OK) {
    dev->error = -1;
    return 0;
  }

  uint32_t value = 0;
  for (int i = 0; i < bytes; i++) {
    value <<= 8;
    value |= buffer[i];
  }
  return value;
}

static double INA228_ReadRegisterF(INA228_HandleTypeDef *dev, uint8_t reg, char mode) {
  uint8_t buffer[5] = {0};
  dev->error = 0;

  if (HAL_I2C_Mem_Read(dev->hi2c, dev->address, reg, I2C_MEMADD_SIZE_8BIT, buffer, 5, dev->timeout) != HAL_OK) {
    dev->error = -1;
    return 0;
  }

  double value = 0;
  uint32_t val = 0;

  for (int i = 0; i < 4; i++) {
    val <<= 8;
    val |= buffer[i];
  }

  if (mode == 'U') value = val;
  else             value = (int32_t) val;

  value *= 256;
  value += buffer[4];

  return value;
}

static uint16_t INA228_WriteRegister(INA228_HandleTypeDef *dev, uint8_t reg, uint16_t value) {
  uint8_t buffer[2];
  buffer[0] = (value >> 8) & 0xFF;
  buffer[1] = value & 0xFF;

  if (HAL_I2C_Mem_Write(dev->hi2c, dev->address, reg, I2C_MEMADD_SIZE_8BIT, buffer, 2, dev->timeout) != HAL_OK) {
    dev->error = -1;
    return 1; // Error
  }
  return 0; // Success
}

/* --- PUBLIC FUNCTIONS --- */

void INA228_Init(INA228_HandleTypeDef *dev, I2C_HandleTypeDef *hi2c, uint8_t address) {
  dev->hi2c = hi2c;
  dev->address = (address << 1); // STM32 requires 8-bit shifted address
  dev->shunt = 0.015f;
  dev->maxCurrent = 10.0f;
  dev->current_LSB = dev->maxCurrent * powf(2.0f, -19.0f);
  dev->error = 0;
  dev->timeout = 100; // 100ms default timeout
  dev->ADCRange = false;
}

bool INA228_Begin(INA228_HandleTypeDef *dev) {
  if (!INA228_IsConnected(dev)) return false;
  INA228_GetADCRange(dev);
  return true;
}

bool INA228_IsConnected(INA228_HandleTypeDef *dev) {
  return (HAL_I2C_IsDeviceReady(dev->hi2c, dev->address, 1, dev->timeout) == HAL_OK);
}

uint8_t INA228_GetAddress(INA228_HandleTypeDef *dev) {
  return (dev->address >> 1);
}

float INA228_GetBusVoltage(INA228_HandleTypeDef *dev) {
  int32_t value = INA228_ReadRegister(dev, INA228_BUS_VOLTAGE, 3) >> 4;
  return value * 195.3125e-6f;
}

float INA228_GetShuntVoltage(INA228_HandleTypeDef *dev) {
  float shunt_LSB = dev->ADCRange ? 78.125e-9f : 312.5e-9f;
  int32_t value = INA228_ReadRegister(dev, INA228_SHUNT_VOLTAGE, 3) >> 4;
  if (value & 0x00080000) value |= 0xFFF00000;
  return value * shunt_LSB;
}

int32_t INA228_GetShuntVoltageRAW(INA228_HandleTypeDef *dev) {
  uint32_t value = INA228_ReadRegister(dev, INA228_SHUNT_VOLTAGE, 3) >> 4;
  if (value & 0x00080000) value |= 0xFFF00000;
  return (int32_t)value;
}

float INA228_GetCurrent(INA228_HandleTypeDef *dev) {
  int32_t value = INA228_ReadRegister(dev, INA228_CURRENT, 3) >> 4;
  if (value & 0x00080000) value |= 0xFFF00000;
  return value * dev->current_LSB;
}

float INA228_GetPower(INA228_HandleTypeDef *dev) {
  uint32_t value = INA228_ReadRegister(dev, INA228_POWER, 3);
  return value * 3.2f * dev->current_LSB;
}

float INA228_GetTemperature(INA228_HandleTypeDef *dev) {
  uint32_t value = INA228_ReadRegister(dev, INA228_TEMPERATURE, 2);
  return value * 7.8125e-3f;
}

double INA228_GetEnergy(INA228_HandleTypeDef *dev) {
  double value = INA228_ReadRegisterF(dev, INA228_ENERGY, 'U');
  return value * (16.0 * 3.2) * (double)dev->current_LSB;
}

double INA228_GetCharge(INA228_HandleTypeDef *dev) {
  double value = INA228_ReadRegisterF(dev, INA228_CHARGE, 'S');
  return value * (double)dev->current_LSB;
}

void INA228_Reset(INA228_HandleTypeDef *dev) {
  uint16_t value = INA228_ReadRegister(dev, INA228_CONFIG, 2);
  value |= INA228_CFG_RST;
  INA228_WriteRegister(dev, INA228_CONFIG, value);
}

bool INA228_SetAccumulation(INA228_HandleTypeDef *dev, uint8_t value) {
  if (value > 1) return false;
  uint16_t reg = INA228_ReadRegister(dev, INA228_CONFIG, 2);
  if (value == 1) reg |= INA228_CFG_RSTACC;
  else            reg &= ~INA228_CFG_RSTACC;
  INA228_WriteRegister(dev, INA228_CONFIG, reg);
  return true;
}

bool INA228_GetAccumulation(INA228_HandleTypeDef *dev) {
  uint16_t value = INA228_ReadRegister(dev, INA228_CONFIG, 2);
  return (value & INA228_CFG_RSTACC) > 0;
}

void INA228_SetConversionDelay(INA228_HandleTypeDef *dev, uint8_t steps) {
  uint16_t value = INA228_ReadRegister(dev, INA228_CONFIG, 2);
  value &= ~INA228_CFG_CONVDLY;
  value |= (steps << 6);
  INA228_WriteRegister(dev, INA228_CONFIG, value);
}

uint8_t INA228_GetConversionDelay(INA228_HandleTypeDef *dev) {
  uint16_t value = INA228_ReadRegister(dev, INA228_CONFIG, 2);
  return (value >> 6) & 0xFF;
}

void INA228_SetTemperatureCompensation(INA228_HandleTypeDef *dev, bool on) {
  uint16_t value = INA228_ReadRegister(dev, INA228_CONFIG, 2);
  if (on) value |= INA228_CFG_TEMPCOMP;
  else    value &= ~INA228_CFG_TEMPCOMP;
  INA228_WriteRegister(dev, INA228_CONFIG, value);
}

bool INA228_GetTemperatureCompensation(INA228_HandleTypeDef *dev) {
  uint16_t value = INA228_ReadRegister(dev, INA228_CONFIG, 2);
  return (value & INA228_CFG_TEMPCOMP) > 0;
}

bool INA228_SetADCRange(INA228_HandleTypeDef *dev, bool flag) {
  uint16_t value = INA228_ReadRegister(dev, INA228_CONFIG, 2);
  dev->ADCRange = (value & INA228_CFG_ADCRANGE) > 0;
  if (flag == dev->ADCRange) return true;

  dev->ADCRange = flag;
  if (flag) value |= INA228_CFG_ADCRANGE;
  else      value &= ~INA228_CFG_ADCRANGE;
  INA228_WriteRegister(dev, INA228_CONFIG, value);
  return (INA228_SetMaxCurrentShunt(dev, dev->maxCurrent, dev->shunt) == 0);
}

bool INA228_GetADCRange(INA228_HandleTypeDef *dev) {
  uint16_t value = INA228_ReadRegister(dev, INA228_CONFIG, 2);
  dev->ADCRange = (value & INA228_CFG_ADCRANGE) > 0;
  return dev->ADCRange;
}

bool INA228_SetMode(INA228_HandleTypeDef *dev, INA228_Mode_t mode) {
  if (mode > 0x0F) return false;
  uint16_t value = INA228_ReadRegister(dev, INA228_ADC_CONFIG, 2);
  value &= ~INA228_ADC_MODE;
  value |= (mode << 12);
  INA228_WriteRegister(dev, INA228_ADC_CONFIG, value);
  return true;
}

uint8_t INA228_GetMode(INA228_HandleTypeDef *dev) {
  uint16_t value = INA228_ReadRegister(dev, INA228_ADC_CONFIG, 2);
  return (value & INA228_ADC_MODE) >> 12;
}

bool INA228_SetBusVoltageConversionTime(INA228_HandleTypeDef *dev, INA228_ConversionTime_t bvct) {
  if (bvct > 7) return false;
  uint16_t value = INA228_ReadRegister(dev, INA228_ADC_CONFIG, 2);
  value &= ~INA228_ADC_VBUSCT;
  value |= (bvct << 9);
  INA228_WriteRegister(dev, INA228_ADC_CONFIG, value);
  return true;
}

uint8_t INA228_GetBusVoltageConversionTime(INA228_HandleTypeDef *dev) {
  uint16_t value = INA228_ReadRegister(dev, INA228_ADC_CONFIG, 2);
  return (value & INA228_ADC_VBUSCT) >> 9;
}

bool INA228_SetShuntVoltageConversionTime(INA228_HandleTypeDef *dev, INA228_ConversionTime_t svct) {
  if (svct > 7) return false;
  uint16_t value = INA228_ReadRegister(dev, INA228_ADC_CONFIG, 2);
  value &= ~INA228_ADC_VSHCT;
  value |= (svct << 6);
  INA228_WriteRegister(dev, INA228_ADC_CONFIG, value);
  return true;
}

uint8_t INA228_GetShuntVoltageConversionTime(INA228_HandleTypeDef *dev) {
  uint16_t value = INA228_ReadRegister(dev, INA228_ADC_CONFIG, 2);
  return (value & INA228_ADC_VSHCT) >> 6;
}

bool INA228_SetTemperatureConversionTime(INA228_HandleTypeDef *dev, INA228_ConversionTime_t tct) {
  if (tct > 7) return false;
  uint16_t value = INA228_ReadRegister(dev, INA228_ADC_CONFIG, 2);
  value &= ~INA228_ADC_VTCT;
  value |= (tct << 3);
  INA228_WriteRegister(dev, INA228_ADC_CONFIG, value);
  return true;
}

uint8_t INA228_GetTemperatureConversionTime(INA228_HandleTypeDef *dev) {
  uint16_t value = INA228_ReadRegister(dev, INA228_ADC_CONFIG, 2);
  return (value & INA228_ADC_VTCT) >> 3;
}

bool INA228_SetAverage(INA228_HandleTypeDef *dev, INA228_Averaging_t avg) {
  if (avg > 7) return false;
  uint16_t value = INA228_ReadRegister(dev, INA228_ADC_CONFIG, 2);
  value &= ~INA228_ADC_AVG;
  value |= avg;
  INA228_WriteRegister(dev, INA228_ADC_CONFIG, value);
  return true;
}

uint8_t INA228_GetAverage(INA228_HandleTypeDef *dev) {
  uint16_t value = INA228_ReadRegister(dev, INA228_ADC_CONFIG, 2);
  return (value & INA228_ADC_AVG);
}

int INA228_SetMaxCurrentShunt(INA228_HandleTypeDef *dev, float maxCurrent, float shunt) {
  if (shunt < 0.0001f) return -2;
  if (maxCurrent < 0.0f) return -3;
  dev->maxCurrent = maxCurrent;
  dev->shunt = shunt;
  dev->current_LSB = dev->maxCurrent * 1.9073486328125e-6f;

  float shunt_cal = 13107.2e6f * dev->current_LSB * dev->shunt;
  if (dev->ADCRange == true) {
    shunt_cal *= 4.0f;
  }
  INA228_WriteRegister(dev, INA228_SHUNT_CAL, (uint16_t)shunt_cal);
  return 0;
}

bool INA228_IsCalibrated(INA228_HandleTypeDef *dev) {
    return dev->current_LSB > 0.0f;
}

bool INA228_SetShuntTemperatureCoefficent(INA228_HandleTypeDef *dev, uint16_t ppm) {
  if (ppm > 16383) return false;
  INA228_WriteRegister(dev, INA228_SHUNT_TEMP_CO, ppm);
  return true;
}

uint16_t INA228_GetShuntTemperatureCoefficent(INA228_HandleTypeDef *dev) {
  return INA228_ReadRegister(dev, INA228_SHUNT_TEMP_CO, 2);
}

void INA228_SetDiagnoseAlert(INA228_HandleTypeDef *dev, uint16_t flags) { INA228_WriteRegister(dev, INA228_DIAG_ALERT, flags); }
uint16_t INA228_GetDiagnoseAlert(INA228_HandleTypeDef *dev) { return INA228_ReadRegister(dev, INA228_DIAG_ALERT, 2); }

void INA228_SetDiagnoseAlertBit(INA228_HandleTypeDef *dev, uint8_t bit) {
  uint16_t value = INA228_ReadRegister(dev, INA228_DIAG_ALERT, 2);
  uint16_t mask = (1 << bit);
  if ((value & mask) == 0) {
    value |= mask;
    INA228_WriteRegister(dev, INA228_DIAG_ALERT, value);
  }
}

void INA228_ClearDiagnoseAlertBit(INA228_HandleTypeDef *dev, uint8_t bit) {
  uint16_t value = INA228_ReadRegister(dev, INA228_DIAG_ALERT, 2);
  uint16_t mask = (1 << bit);
  if ((value & mask ) != 0) {
    value &= ~mask;
    INA228_WriteRegister(dev, INA228_DIAG_ALERT, value);
  }
}

uint16_t INA228_GetDiagnoseAlertBit(INA228_HandleTypeDef *dev, uint8_t bit) {
  uint16_t value = INA228_ReadRegister(dev, INA228_DIAG_ALERT, 2);
  return (value >> bit) & 0x01;
}

void INA228_SetShuntOvervoltageTH(INA228_HandleTypeDef *dev, uint16_t threshold) { INA228_WriteRegister(dev, INA228_SOVL, threshold); }
uint16_t INA228_GetShuntOvervoltageTH(INA228_HandleTypeDef *dev) { return INA228_ReadRegister(dev, INA228_SOVL, 2); }
void INA228_SetShuntUndervoltageTH(INA228_HandleTypeDef *dev, uint16_t threshold) { INA228_WriteRegister(dev, INA228_SUVL, threshold); }
uint16_t INA228_GetShuntUndervoltageTH(INA228_HandleTypeDef *dev) { return INA228_ReadRegister(dev, INA228_SUVL, 2); }

void INA228_SetBusOvervoltageTH(INA228_HandleTypeDef *dev, uint16_t threshold) {
  if (threshold > 0x7FFF) return;
  INA228_WriteRegister(dev, INA228_BOVL, threshold);
}

uint16_t INA228_GetBusOvervoltageTH(INA228_HandleTypeDef *dev) { return INA228_ReadRegister(dev, INA228_BOVL, 2); }

void INA228_SetBusUndervoltageTH(INA228_HandleTypeDef *dev, uint16_t threshold) {
  if (threshold > 0x7FFF) return;
  INA228_WriteRegister(dev, INA228_BUVL, threshold);
}

uint16_t INA228_GetBusUndervoltageTH(INA228_HandleTypeDef *dev) { return INA228_ReadRegister(dev, INA228_BUVL, 2); }
void INA228_SetTemperatureOverLimitTH(INA228_HandleTypeDef *dev, uint16_t threshold) { INA228_WriteRegister(dev, INA228_TEMP_LIMIT, threshold); }
uint16_t INA228_GetTemperatureOverLimitTH(INA228_HandleTypeDef *dev) { return INA228_ReadRegister(dev, INA228_TEMP_LIMIT, 2); }
void INA228_SetPowerOverLimitTH(INA228_HandleTypeDef *dev, uint16_t threshold) { INA228_WriteRegister(dev, INA228_POWER_LIMIT, threshold); }
uint16_t INA228_GetPowerOverLimitTH(INA228_HandleTypeDef *dev) { return INA228_ReadRegister(dev, INA228_POWER_LIMIT, 2); }

uint16_t INA228_GetManufacturer(INA228_HandleTypeDef *dev) { return INA228_ReadRegister(dev, INA228_MANUFACTURER, 2); }
uint16_t INA228_GetDieID(INA228_HandleTypeDef *dev) { return (INA228_ReadRegister(dev, INA228_DEVICE_ID, 2) >> 4) & 0x0FFF; }
uint16_t INA228_GetRevision(INA228_HandleTypeDef *dev) { return INA228_ReadRegister(dev, INA228_DEVICE_ID, 2) & 0x000F; }

int INA228_GetLastError(INA228_HandleTypeDef *dev) {
  int e = dev->error;
  dev->error = 0;
  return e;
}
