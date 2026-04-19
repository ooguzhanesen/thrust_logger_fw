#ifndef NAU7802_H
#define NAU7802_H

#include "main.h"
#include <stdbool.h>

// I2C Adresi (7-bit adres sola kaydırılmış hali)
#define NAU7802_I2CADDR (0x2A << 1)

// Register Adresleri
#define NAU7802_PU_CTRL     0x00
#define NAU7802_CTRL1       0x01
#define NAU7802_CTRL2       0x02
#define NAU7802_ADCO_B2     0x12
#define NAU7802_ADC         0x15
#define NAU7802_PGA         0x1B
#define NAU7802_POWER       0x1C
#define NAU7802_REVISION_ID 0x1F

// LDO Voltaj Seçenekleri
typedef enum {
    NAU7802_4V5 = 0,
    NAU7802_4V2 = 1,
    NAU7802_3V9 = 2,
    NAU7802_3V6 = 3,
    NAU7802_3V3 = 4,
    NAU7802_3V0 = 5,
    NAU7802_2V7 = 6,
    NAU7802_2V4 = 7,
    NAU7802_EXTERNAL = 8
} NAU7802_LDOVoltage;

// Kazanç (Gain) Seçenekleri
typedef enum {
    NAU7802_GAIN_1 = 0,
    NAU7802_GAIN_2 = 1,
    NAU7802_GAIN_4 = 2,
    NAU7802_GAIN_8 = 3,
    NAU7802_GAIN_16 = 4,
    NAU7802_GAIN_32 = 5,
    NAU7802_GAIN_64 = 6,
    NAU7802_GAIN_128 = 7
} NAU7802_Gain;

// Örnekleme Hızı (Sample Rate) Seçenekleri
typedef enum {
    NAU7802_RATE_10SPS = 0,
    NAU7802_RATE_20SPS = 1,
    NAU7802_RATE_40SPS = 2,
    NAU7802_RATE_80SPS = 3,
    NAU7802_RATE_320SPS = 7
} NAU7802_SampleRate;

// Kalibrasyon Modları
typedef enum {
    NAU7802_CALMOD_INTERNAL = 0,
    NAU7802_CALMOD_OFFSET = 2,
    NAU7802_CALMOD_GAIN = 3
} NAU7802_Calibration;

typedef struct {
    I2C_HandleTypeDef *hi2c;  // Hangi I2C hattına bağlı
    int32_t zero_offset;      // Bu sensörün özel sıfır noktası
    float calibration_factor; // Bu sensörün özel kalibrasyon çarpanı
} NAU7802_Sensor;


// --- Fonksiyon Prototipleri ---
bool NAU7802_Init(I2C_HandleTypeDef *hi2c);
bool NAU7802_Reset(I2C_HandleTypeDef *hi2c);
bool NAU7802_Enable(I2C_HandleTypeDef *hi2c, bool flag);
bool NAU7802_Available(I2C_HandleTypeDef *hi2c);
int32_t NAU7802_Read(I2C_HandleTypeDef *hi2c);

bool NAU7802_SetLDO(I2C_HandleTypeDef *hi2c, NAU7802_LDOVoltage voltage);
bool NAU7802_SetGain(I2C_HandleTypeDef *hi2c, NAU7802_Gain gain);
bool NAU7802_SetRate(I2C_HandleTypeDef *hi2c, NAU7802_SampleRate rate);
bool NAU7802_Calibrate(I2C_HandleTypeDef *hi2c, NAU7802_Calibration mode);

// Belirtilen sensör nesnesi için dara alır ve zero_offset değerini nesnenin içine kaydeder
void NAU7802_Tare(NAU7802_Sensor *sensor, uint8_t samples);

// Ham veriyi alır, sensörün kendi offset ve çarpanını kullanarak gerçek ağırlığı (gram) döndürür
float NAU7802_CalculateWeight(NAU7802_Sensor *sensor, int32_t raw_value);

// Bilinen bir ağırlık ile kalibrasyon çarpanını otomatik hesaplar
void NAU7802_CalibrateFactor(NAU7802_Sensor *sensor, float known_weight, uint8_t samples);

#endif // NAU7802_H
