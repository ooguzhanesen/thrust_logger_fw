// TCA9548.c

#include "TCA9548.h"

// Yerel bir değişken olarak aktif maskeyi saklayalım (istek sayısını azaltmak için)
static uint8_t current_mask = 0x00;

// TCA9548'i başlatır.
HAL_StatusTypeDef TCA9548_Init(I2C_HandleTypeDef *hi2c, uint8_t initial_mask) {
    if (TCA9548_IsConnected(hi2c)) {
        return TCA9548_SetChannelMask(hi2c, initial_mask);
    }
    return HAL_ERROR;
}

// I2C hattında TCA9548'in mevcut olup olmadığını kontrol eder.
bool TCA9548_IsConnected(I2C_HandleTypeDef *hi2c) {
    // Adresi sorgular, cevap gelirse cihaz oradadır.
    HAL_StatusTypeDef status = HAL_I2C_IsDeviceReady(hi2c, TCA9548_ADDRESS, 3, 100);
    return (status == HAL_OK);
}

// Sadece belirtilen kanalı aktif eder (diğerlerini kapatır).
HAL_StatusTypeDef TCA9548_SelectChannel(I2C_HandleTypeDef *hi2c, uint8_t channel) {
    if (channel >= TCA9548_CHANNELS) return HAL_ERROR;

    uint8_t new_mask = (1 << channel);
    return TCA9548_SetChannelMask(hi2c, new_mask);
}

// Belirtilen kanalı devre dışı bırakır.
HAL_StatusTypeDef TCA9548_DisableChannel(I2C_HandleTypeDef *hi2c, uint8_t channel) {
    if (channel >= TCA9548_CHANNELS) return HAL_ERROR;

    uint8_t new_mask = current_mask & ~(1 << channel);
    return TCA9548_SetChannelMask(hi2c, new_mask);
}

// Tüm kanalları devre dışı bırakır.
HAL_StatusTypeDef TCA9548_DisableAllChannels(I2C_HandleTypeDef *hi2c) {
    return TCA9548_SetChannelMask(hi2c, 0x00);
}

// Çoklu kanalları aynı anda ayarlamak için bir bit maskesi gönderir.
HAL_StatusTypeDef TCA9548_SetChannelMask(I2C_HandleTypeDef *hi2c, uint8_t mask) {
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(hi2c, TCA9548_ADDRESS, &mask, 1, 100);
    if (status == HAL_OK) {
        current_mask = mask; // Başarılıysa durumu güncelle
    }
    return status;
}

// Şu anda aktif olan kanalların maskesini okur.
uint8_t TCA9548_GetChannelMask(I2C_HandleTypeDef *hi2c) {
    uint8_t read_mask = 0;
    // Cihazdan aktif maskeyi oku
    if (HAL_I2C_Master_Receive(hi2c, TCA9548_ADDRESS, &read_mask, 1, 100) == HAL_OK) {
         current_mask = read_mask; // Yerel değişkeni de senkronize et
         return read_mask;
    }
    return current_mask; // Hata olursa son bilinen durumu döndür
}
