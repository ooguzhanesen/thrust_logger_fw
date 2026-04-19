// TCA9548.h

#ifndef TCA9548_H
#define TCA9548_H

#include "main.h" // STM32 projesi için gerekli HAL tanımlarını içerir
#include <stdbool.h>

// --- Kütüphane Ayarları ---

// I2C Adresi (0x70 ile 0x77 arası) - 7-bit adresi 1 sola kaydırarak (8-bit) kullanacağız
// Örneğin, tüm pinler GND'ye bağlıysa adres 0x70'tir. Sola kaydırılmış hali 0xE0'dır.
#define TCA9548_ADDRESS  (0x70 << 1)

// Kanal Sayısı
#define TCA9548_CHANNELS 8

// --- Fonksiyon Prototipleri ---

// TCA9548'i başlatır. (Varsayılan olarak maske 0 - tüm kanallar kapalı)
// İletişimi kontrol eder ve isteğe bağlı bir başlangıç durumu ayarlar.
HAL_StatusTypeDef TCA9548_Init(I2C_HandleTypeDef *hi2c, uint8_t initial_mask);

// I2C hattında TCA9548'in mevcut olup olmadığını kontrol eder.
bool TCA9548_IsConnected(I2C_HandleTypeDef *hi2c);

// Sadece belirtilen kanalı aktif eder (diğer tüm kanalları kapatır).
HAL_StatusTypeDef TCA9548_SelectChannel(I2C_HandleTypeDef *hi2c, uint8_t channel);

// Belirtilen kanalı devre dışı bırakır.
HAL_StatusTypeDef TCA9548_DisableChannel(I2C_HandleTypeDef *hi2c, uint8_t channel);

// Tüm kanalları devre dışı bırakır.
HAL_StatusTypeDef TCA9548_DisableAllChannels(I2C_HandleTypeDef *hi2c);

// Çoklu kanalları aynı anda ayarlamak için bir bit maskesi gönderir (Örn: 0x03 -> Kanal 0 ve 1 açık).
HAL_StatusTypeDef TCA9548_SetChannelMask(I2C_HandleTypeDef *hi2c, uint8_t mask);

// Şu anda aktif olan kanalların maskesini okur.
uint8_t TCA9548_GetChannelMask(I2C_HandleTypeDef *hi2c);

#endif // TCA9548_H
