#include "NAU7802.h"

// --- Yardımcı Fonksiyonlar (Sadece bu dosyada kullanılır) ---
static bool NAU7802_WriteReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t value) {
    return (HAL_I2C_Mem_Write(hi2c, NAU7802_I2CADDR, reg, 1, &value, 1, 100) == HAL_OK);
}

static bool NAU7802_ReadReg(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t *value) {
    return (HAL_I2C_Mem_Read(hi2c, NAU7802_I2CADDR, reg, 1, value, 1, 100) == HAL_OK);
}

static bool NAU7802_SetBit(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t bit) {
    uint8_t val;
    if (!NAU7802_ReadReg(hi2c, reg, &val)) return false;
    val |= (1 << bit);
    return NAU7802_WriteReg(hi2c, reg, val);
}

static bool NAU7802_ClearBit(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t bit) {
    uint8_t val;
    if (!NAU7802_ReadReg(hi2c, reg, &val)) return false;
    val &= ~(1 << bit);
    return NAU7802_WriteReg(hi2c, reg, val);
}

static bool NAU7802_ModifyBits(I2C_HandleTypeDef *hi2c, uint8_t reg, uint8_t mask, uint8_t shift, uint8_t data) {
    uint8_t val;
    if (!NAU7802_ReadReg(hi2c, reg, &val)) return false;
    val &= ~(mask << shift); // İlgili bitleri temizle
    val |= (data << shift);  // Yeni veriyi yaz
    return NAU7802_WriteReg(hi2c, reg, val);
}

// --- Ana Fonksiyonlar ---

bool NAU7802_Reset(I2C_HandleTypeDef *hi2c) {
    NAU7802_SetBit(hi2c, NAU7802_PU_CTRL, 0); // RR bit = 1
    HAL_Delay(10);
    NAU7802_ClearBit(hi2c, NAU7802_PU_CTRL, 0); // RR bit = 0
    NAU7802_SetBit(hi2c, NAU7802_PU_CTRL, 1);   // PUD bit = 1
    HAL_Delay(1);

    uint8_t val;
    NAU7802_ReadReg(hi2c, NAU7802_PU_CTRL, &val);
    return (val & (1 << 3)); // PUR bitini kontrol et
}

bool NAU7802_Enable(I2C_HandleTypeDef *hi2c, bool flag) {
    if (!flag) {
        NAU7802_ClearBit(hi2c, NAU7802_PU_CTRL, 2); // Analog kapa
        NAU7802_ClearBit(hi2c, NAU7802_PU_CTRL, 1); // Dijital kapa
        return true;
    }
    NAU7802_SetBit(hi2c, NAU7802_PU_CTRL, 1); // Dijital aç
    NAU7802_SetBit(hi2c, NAU7802_PU_CTRL, 2); // Analog aç
    HAL_Delay(600); // LDO ve analog kısımların uyanması için süre
    NAU7802_SetBit(hi2c, NAU7802_PU_CTRL, 4); // CS (Cycle Start)

    uint8_t val;
    NAU7802_ReadReg(hi2c, NAU7802_PU_CTRL, &val);
    return (val & (1 << 3)); // Hazır mı kontrol et
}

bool NAU7802_Init(I2C_HandleTypeDef *hi2c) {
    if (!NAU7802_Reset(hi2c)) return false;
    if (!NAU7802_Enable(hi2c, true)) return false;

    // Revizyon kontrolü
    uint8_t rev;
    if (!NAU7802_ReadReg(hi2c, NAU7802_REVISION_ID, &rev)) return false;
    if ((rev & 0x0F) != 0x0F) return false;

    // Adafruit standart ayarları
    if (!NAU7802_SetLDO(hi2c, NAU7802_3V0)) return false;
    if (!NAU7802_SetGain(hi2c, NAU7802_GAIN_128)) return false;
    if (!NAU7802_SetRate(hi2c, NAU7802_RATE_10SPS)) return false; // Thrust Logger için hızı 80SPS yaptım!

    // Chopper clock kapat (Adafruit default)
    NAU7802_ModifyBits(hi2c, NAU7802_ADC, 0x03, 4, 0x03);
    // Low ESR Caps
    NAU7802_ClearBit(hi2c, NAU7802_PGA, 6);

    return true;
}

bool NAU7802_Available(I2C_HandleTypeDef *hi2c) {
    uint8_t val;
    NAU7802_ReadReg(hi2c, NAU7802_PU_CTRL, &val);
    return (val & (1 << 5)); // CR (Conversion Ready) biti
}

int32_t NAU7802_Read(I2C_HandleTypeDef *hi2c) {
    uint8_t data[3];
    int32_t value = 0;

    if (HAL_I2C_Mem_Read(hi2c, NAU7802_I2CADDR, NAU7802_ADCO_B2, 1, data, 3, 100) == HAL_OK) {
        value = (int32_t)((data[0] << 16) | (data[1] << 8) | data[2]);
        if (value & 0x800000) { // Sign extension
            value |= 0xFF000000;
        }
    }
    return value;
}

bool NAU7802_SetLDO(I2C_HandleTypeDef *hi2c, NAU7802_LDOVoltage voltage) {
    if (voltage == NAU7802_EXTERNAL) {
        return NAU7802_ClearBit(hi2c, NAU7802_PU_CTRL, 7); // AVDDS biti 0
    }
    NAU7802_SetBit(hi2c, NAU7802_PU_CTRL, 7); // Dahili LDO aktif
    return NAU7802_ModifyBits(hi2c, NAU7802_CTRL1, 0x07, 3, voltage);
}

bool NAU7802_SetGain(I2C_HandleTypeDef *hi2c, NAU7802_Gain gain) {
    return NAU7802_ModifyBits(hi2c, NAU7802_CTRL1, 0x07, 0, gain);
}

bool NAU7802_SetRate(I2C_HandleTypeDef *hi2c, NAU7802_SampleRate rate) {
    return NAU7802_ModifyBits(hi2c, NAU7802_CTRL2, 0x07, 4, rate);
}

bool NAU7802_Calibrate(I2C_HandleTypeDef *hi2c, NAU7802_Calibration mode) {
    NAU7802_ModifyBits(hi2c, NAU7802_CTRL2, 0x03, 0, mode); // Modu seç
    NAU7802_SetBit(hi2c, NAU7802_CTRL2, 2); // CALS (Calibration Start) bitini 1 yap

    uint8_t val;
    // Kalibrasyonun bitmesini bekle
    do {
        HAL_Delay(10);
        NAU7802_ReadReg(hi2c, NAU7802_CTRL2, &val);
    } while (val & (1 << 2));

    // CAL_ERR bitini kontrol et (Hata varsa 1 döner, biz false döneceğiz)
    return !(val & (1 << 3));
}

// Belirli sayıda okuma yapıp ortalamasını alarak nesnenin "zero_offset" değerini günceller
void NAU7802_Tare(NAU7802_Sensor *sensor, uint8_t samples) {
    int64_t sum = 0;
    uint8_t count = 0;

    while(count < samples) {
        if (NAU7802_Available(sensor->hi2c)) {
            sum += NAU7802_Read(sensor->hi2c);
            count++;
        }
        HAL_Delay(10); // Sensörü boğmamak için kısa bir bekleme
    }

    sensor->zero_offset = sum / samples;
}

// Sensör nesnesindeki ayarları kullanarak matematiği çözer
float NAU7802_CalculateWeight(NAU7802_Sensor *sensor, int32_t raw_value) {
    return (float)(raw_value - sensor->zero_offset) / sensor->calibration_factor;
}

void NAU7802_CalibrateFactor(NAU7802_Sensor *sensor, float known_weight, uint8_t samples) {
    int64_t sum = 0;
    uint8_t count = 0;

    // Yüklü durumdayken belirtilen sayıda okuma yap ve ortalamasını al
    while(count < samples) {
        if (NAU7802_Available(sensor->hi2c)) {
            sum += NAU7802_Read(sensor->hi2c);
            count++;
        }
        HAL_Delay(10);
    }

    int32_t loaded_avg = sum / samples;

    // Çarpan formülü: (Yüklü Değer - Boş Değer) / Gerçek Ağırlık
    sensor->calibration_factor = (float)(loaded_avg - sensor->zero_offset) / known_weight;
}
