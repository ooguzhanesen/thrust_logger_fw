#include "application.h"
#include "usbd_cdc_if.h" // CDC_Transmit_FS fonksiyonu için
#include "prdc_tmaesc.h" // T-Motor UART Kütüphanesi
#include <stdio.h>
#include <string.h>

extern I2C_HandleTypeDef hi2c1;
extern I2C_HandleTypeDef hi2c2;
extern TIM_HandleTypeDef htim3;
extern UART_HandleTypeDef huart3;

// Sensör ve ESC Obje Tanımlamaları
static INA228_HandleTypeDef powerSensor;
static NAU7802_Sensor thrustSensor;
static TMAESC_Handle esc_handler;

// DMA'in işlemciden bağımsız yığacağı tampon
static uint8_t dma_rx_buffer[TMAESC_PACKET_SIZE];

// USB Komut ve Durum Değişkenleri
static volatile bool is_reading = false;
static volatile bool new_usb_data = false;
static char usb_rx_buffer[64];

static uint16_t current_pwm = 1000;

// Kullanıcının görmek istediği değişkenler
static uint16_t esc_rpm = 0;

// --- YENİ EKLENEN GÜVENLİK VE TEST DEĞİŞKENLERİ ---
static float max_current_limit = 999.0f; // Başlangıçta limitsiz
static float min_voltage_limit = 0.0f;   // Başlangıçta limitsiz

static bool auto_test_active = false;
static uint32_t auto_test_interval_ms = 0;
static int auto_test_pwm_step = 0;
static uint32_t auto_test_last_time = 0;

// --- FONKSİYON PROTOTİPLERİ ---
static void App_ProcessCommand(const char* cmd);
static void App_BuzzerBeep(uint32_t delay_ms);
static void App_SendAck(const char* msg);

/* Yardımcı Fonksiyon: USB'den Onay Mesajı Gönderir */
static void App_SendAck(const char* msg) {
    uint32_t timeout = 0;
    while (CDC_Transmit_FS((uint8_t*)msg, strlen(msg)) == USBD_BUSY) {
        osDelay(1);
        if (++timeout >= 5) break;
    }
}

/* Yardımcı Fonksiyon: Buzzer Ses Çıkarır */
static void App_BuzzerBeep(uint32_t delay_ms) {
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);
    osDelay(delay_ms);
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
}

/* DMA Paketi Dolduğunda Tetiklenir */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART3) {
        for(int i=0; i<TMAESC_PACKET_SIZE; i++) {
            TMAESC_ParseByte(&esc_handler, dma_rx_buffer[i]);
        }
    }
}

/* UART Hata Yakalayıcı */
void HAL_UART_ErrorCallback(UART_HandleTypeDef *huart) {
    if (huart->Instance == USART3) {
        __HAL_UART_CLEAR_OREFLAG(huart);
        __HAL_UART_CLEAR_NEFLAG(huart);
        __HAL_UART_CLEAR_FEFLAG(huart);
        HAL_UART_Receive_DMA(&huart3, dma_rx_buffer, TMAESC_PACKET_SIZE);
    }
}

/* RTOS Başlamadan Önceki İlk Ayarlar */
void App_Init(void) {
    INA228_Init(&powerSensor, &hi2c2, 0x40);
    HAL_Delay(1000);

    if (INA228_Begin(&powerSensor)) {
        INA228_SetMaxCurrentShunt(&powerSensor, 20.0f, 0.001988f);
        INA228_SetMode(&powerSensor, INA228_MODE_CONT_TEMP_BUS_SHUNT);
    }

    TCA9548_Init(&hi2c1, 0x00);
    TCA9548_SelectChannel(&hi2c1, 0);
    thrustSensor.hi2c = &hi2c1;
    if (NAU7802_Init(&hi2c1)) { }

    TMAESC_Init(&esc_handler, &huart3, 28); // Varsayılan 28 kutup
    HAL_UART_Receive_DMA(&huart3, dma_rx_buffer, TMAESC_PACKET_SIZE);

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
}

/* USB Kesmesi */
void App_ProcessUsbData(uint8_t* Buf, uint32_t Len) {
    if (Len < sizeof(usb_rx_buffer)) {
        memcpy(usb_rx_buffer, Buf, Len);
        usb_rx_buffer[Len] = '\0';
        new_usb_data = true;
    }
}

/* YÜKSEK ÖNCELİKLİ GÖREV: USB Komut Dinleyicisi */
void App_TaskUsbListen(void *argument) {
    char local_cmd[64];
    for(;;) {
        if (new_usb_data) {
            strncpy(local_cmd, (char*)usb_rx_buffer, sizeof(local_cmd) - 1);
            local_cmd[sizeof(local_cmd) - 1] = '\0';
            new_usb_data = false;
            App_ProcessCommand(local_cmd);
        }
        osDelay(10);
    }
}

/* KOMUT AYRIŞTIRICI MERKEZ */
static void App_ProcessCommand(const char* cmd) {
    char ack_msg[64];

    // 1. Okuma Başlat
    if (strstr(cmd, "start_reading") != NULL) {
        is_reading = true;
        App_SendAck("ok start_reading\r\n");
        App_BuzzerBeep(50); osDelay(50); App_BuzzerBeep(50);
    }
    // 2. Okuma Durdur
    else if (strstr(cmd, "stop_reading") != NULL) {
        is_reading = false;
        auto_test_active = false; // Testi iptal et
        App_SendAck("ok stop_reading\r\n");
        App_BuzzerBeep(120);
    }
    // 3. Acil Durum Stop
    else if (strstr(cmd, "emergency_stop") != NULL) {
        is_reading = false;
        auto_test_active = false; // Testi iptal et
        current_pwm = 1000;
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 1000);
        App_SendAck("ok emergency_stop\r\n");
        App_BuzzerBeep(500); // Acil stop uzun bip
    }
    // 4. Pole (Kutup) Sayısı Ayarlama
    else if (strncmp(cmd, "set_pol_", 8) == 0) {
        int target_pol;
        if (sscanf(cmd, "set_pol_%d", &target_pol) == 1 && target_pol > 0) {
            esc_handler.poles = target_pol;
            snprintf(ack_msg, sizeof(ack_msg), "ok pol %d\r\n", target_pol);
            App_SendAck(ack_msg);
            App_BuzzerBeep(50);
        }
    }
    // 5. Max Akım Sınırı
    else if (strncmp(cmd, "set_max_current_", 16) == 0) {
        float target_cur;
        if (sscanf(cmd, "set_max_current_%f", &target_cur) == 1) {
            max_current_limit = target_cur;
            snprintf(ack_msg, sizeof(ack_msg), "ok max_current %.1f\r\n", max_current_limit);
            App_SendAck(ack_msg);
            App_BuzzerBeep(200); osDelay(100); App_BuzzerBeep(200);
        }
    }
    // 6. Min Voltaj Sınırı
    else if (strncmp(cmd, "set_min_voltage_", 16) == 0) {
        float target_vol;
        if (sscanf(cmd, "set_min_voltage_%f", &target_vol) == 1) {
            min_voltage_limit = target_vol;
            snprintf(ack_msg, sizeof(ack_msg), "ok min_voltage %.1f\r\n", min_voltage_limit);
            App_SendAck(ack_msg);
            App_BuzzerBeep(200); osDelay(100); App_BuzzerBeep(200);
        }
    }
    // 7. Otomatik Test Modu (Örn: test_10_100)
    else if (strncmp(cmd, "test_", 5) == 0) {
        int delay_sec, pwm_step;
        if (sscanf(cmd, "test_%d_%d", &delay_sec, &pwm_step) == 2) {
            auto_test_interval_ms = delay_sec * 1000; // Saniyeyi ms'ye çevir
            auto_test_pwm_step = pwm_step;
            auto_test_active = true;
            auto_test_last_time = HAL_GetTick(); // Testi şu an başlat

            snprintf(ack_msg, sizeof(ack_msg), "ok test delay:%d step:%d\r\n", delay_sec, pwm_step);
            App_SendAck(ack_msg);
            App_BuzzerBeep(50); osDelay(50); App_BuzzerBeep(50); // İki kısa bip = Test Başladı
        }
    }
    // 8. Manuel PWM Ayarlama (Manuel müdahale testi iptal eder)
    else if (strncmp(cmd, "set_pwm_", 8) == 0) {
        int target_pwm;
        if (sscanf(cmd, "set_pwm_%d", &target_pwm) == 1) {
            if (target_pwm < 1000) target_pwm = 1000;
            if (target_pwm > 2000) target_pwm = 2000;

            current_pwm = target_pwm;
            auto_test_active = false; // Kullanıcı gaza kendi basarsa otomatik test durur
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, current_pwm);

            snprintf(ack_msg, sizeof(ack_msg), "ok pwm %d\r\n", current_pwm);
            App_SendAck(ack_msg);
            App_BuzzerBeep(50);
        }
    }
}

/* Normal Öncelikli Görev: Sensör Okuma ve Gönderme (10 Hz) */
void App_TaskSensorRead(void *argument) {
    char local_tx_buffer[256];
    TMAESC_Data esc_data;

    for(;;) {
        if (is_reading) {
            float busVoltage = 0.0f, currentAmp = 0.0f, powerWatt = 0.0f;
            int32_t thrustVal = 0;

            if (INA228_IsConnected(&powerSensor)) {
                busVoltage = INA228_GetBusVoltage(&powerSensor);
                currentAmp = INA228_GetCurrent(&powerSensor);
                powerWatt  = INA228_GetPower(&powerSensor);

                // --- 1. GÜVENLİK: MAKSİMUM AKIM KORUMASI ---
                if (currentAmp > max_current_limit) {
                    current_pwm = 1000;
                    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 1000);
                    is_reading = false;
                    auto_test_active = false;
                    App_SendAck("\r\n!!! ALARM: MAX CURRENT EXCEEDED! STOPPED !!!\r\n");
                    App_BuzzerBeep(800); // Uzun ikaz
                }

                // --- 2. GÜVENLİK: DÜŞÜK VOLTAJ KORUMASI ---
                // (busVoltage > 5.0f şartı, batarya hiç takılı değilken cihazı kilitlenmekten korur)
                else if (busVoltage < min_voltage_limit && busVoltage > 5.0f) {
                    current_pwm = 1000;
                    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 1000);
                    is_reading = false;
                    auto_test_active = false;
                    App_SendAck("\r\n!!! ALARM: LOW VOLTAGE! STOPPED !!!\r\n");

                    // Düüt Düüt (Düşük voltaj melodisi)
                    App_BuzzerBeep(200);
                    osDelay(100);
                    App_BuzzerBeep(200);
                }
            }

            if (NAU7802_Available(&hi2c1)) {
                thrustVal = NAU7802_Read(&hi2c1);
            }

            if (TMAESC_TryGet(&esc_handler, &esc_data)) {
                esc_rpm = esc_data.rpm;
            }

            // --- 3. OTOMATİK TEST MODU İŞLEYİCİSİ ---
            if (auto_test_active) {
                if (HAL_GetTick() - auto_test_last_time >= auto_test_interval_ms) {
                    current_pwm += auto_test_pwm_step;

                    if (current_pwm >= 2000) {
                        current_pwm = 2000;
                        auto_test_active = false; // Max değere ulaştı, testi bitir
                        App_SendAck("\r\n--- AUTO TEST COMPLETED (MAX PWM) ---\r\n");
                    }

                    __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, current_pwm);
                    App_BuzzerBeep(50); // Her artışta kısa bir tık sesi
                    auto_test_last_time = HAL_GetTick();
                }
            }

            // Ekrana Veri Basma
            snprintf(local_tx_buffer, sizeof(local_tx_buffer),
            		"PWM:%d V:%.2f I:%.2f P:%.2f T:%ld eRPM:%u\r\n",
					current_pwm, busVoltage, currentAmp, powerWatt, thrustVal, esc_rpm);

            uint32_t timeout = 0;
            while (CDC_Transmit_FS((uint8_t*)local_tx_buffer, strlen(local_tx_buffer)) == USBD_BUSY) {
                osDelay(1);
                if (++timeout >= 5) break;
            }
        }

        // GÜVENLİK TETİĞİ: DMA her ihtimale karşı kapanmışsa yeniden başlat
        if (huart3.RxState == HAL_UART_STATE_READY) {
             HAL_UART_Receive_DMA(&huart3, dma_rx_buffer, TMAESC_PACKET_SIZE);
        }

        osDelay(100); // 10Hz Döngü
    }
}
