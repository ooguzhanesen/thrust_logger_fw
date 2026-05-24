#ifndef PRDC_TMAESC_H
#define PRDC_TMAESC_H

#include <stdint.h>
#include <stdbool.h>
#include "stm32f4xx_hal.h" // STM32F446 İşlemcin için düzeltildi

/* T-Motor Alpha Orijinal Protokol Sabitleri */
#define TMAESC_HDR0           0x9B
#define TMAESC_PACKET_SIZE    12u     /* Sabit 12 Byte Protokol */
#define TMAESC_DATA_SIZE      11u

/* Çözümlenmiş Telemetri Veri Yapısı */
typedef struct {
  uint32_t time_ms;           /* HAL_GetTick() zaman damgası */
  uint16_t rpm;               /* Gerçek Motor Devri (RPM) */
  float    voltage_bus;       /* Batarya Voltajı (V) */
  float    current_bus;       /* Çekilen Akım (A) */
  uint8_t  temperature_mos;   /* Mosfet Sıcaklığı (°C) */
  uint16_t status;            /* ESC Durum / Hata Kodu */
} TMAESC_Data;

/* Durum Makinesi Adımları */
typedef enum {
  TMAESC_S_HDR0 = 0,
  TMAESC_S_COLLECT
} TMAESC_State;

/* ESC Yönetim Nesnesi (Handle) */
typedef struct {
  UART_HandleTypeDef *huart;    /* Bağlı olan UART Donanımı (USART3) */
  uint8_t  poles;               /* Motor Kutup Sayısı (Örn: 28) */
  TMAESC_State state;
  uint8_t  buf[TMAESC_PACKET_SIZE];
  uint8_t  idx;
  uint8_t  rx_byte;             /* 1-Baytlık Kesme Tamponu */
  volatile bool new_frame;      /* Yeni Paket Geldi Bayrağı */
  TMAESC_Data last;             /* En Son Çözülen Temiz Veri */
} TMAESC_Handle;

/* API Fonksiyon Prototipleri */
void TMAESC_Init(TMAESC_Handle *h, UART_HandleTypeDef *huart, uint8_t poles);
void TMAESC_StartIT(TMAESC_Handle *h);
void TMAESC_OnRxCplt(TMAESC_Handle *h);
bool TMAESC_ParseByte(TMAESC_Handle *h, uint8_t b);
bool TMAESC_TryGet(TMAESC_Handle *h, TMAESC_Data *out);

#endif /* PRDC_TMAESC_H */
