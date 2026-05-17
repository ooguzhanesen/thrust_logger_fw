#include "application.h"
#include "usbd_cdc_if.h" // CDC_Transmit_FS fonksiyonu için
#include <stdio.h>
#include <string.h>

// main.c'deki donanım değişkenlerini buraya çekiyoruz
extern I2C_HandleTypeDef hi2c1; // TCA9548 ve NAU7802 I2C hattı
extern I2C_HandleTypeDef hi2c2; // INA228 I2C hattı
extern TIM_HandleTypeDef htim3; // PWM için CubeMX'in ürettiği Timer handle'ı

// Sensör Obje Tanımlamaları
static INA228_HandleTypeDef powerSensor;
static NAU7802_Sensor thrustSensor;

// USB Komut ve Durum Değişkenleri
static volatile bool is_reading = false;
static volatile bool new_usb_data = false;
static char usb_rx_buffer[64];
static char usb_tx_buffer[128];

static uint16_t current_pwm = 1000;

// --- FONKSİYON PROTOTİPLERİ ---
static void App_BuzzerBeep(uint32_t delay_ms) {
    // Buzzer LOW'da çalıştığı için pini RESET (0V) yapıyoruz
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_RESET);

    // İstediğimiz süre kadar bu task'ı uyutup buzzer'ın çalmasını sağlıyoruz
    osDelay(delay_ms);

    // Süre bitince pini HIGH (3.3V) yaparak susturuyoruz
    HAL_GPIO_WritePin(BUZZER_GPIO_Port, BUZZER_Pin, GPIO_PIN_SET);
}

static void App_ProcessCommand(const char* cmd);

/* RTOS Başlamadan Önce main() İçinde Çalışacak İlk Ayarlar */
void App_Init(void) {
    // 1. INA228 Başlatma
    INA228_Init(&powerSensor, &hi2c2, 0x40);
    HAL_Delay(1000); // USB'nin PC tarafından tanınması için bekleme

    if (INA228_Begin(&powerSensor)) {
        INA228_SetMaxCurrentShunt(&powerSensor, 20.0f, 0.001988f);
        INA228_SetMode(&powerSensor, INA228_MODE_CONT_TEMP_BUS_SHUNT);

        strcpy(usb_tx_buffer, "INA228 Baslatildi.\r\n");
        CDC_Transmit_FS((uint8_t*)usb_tx_buffer, strlen(usb_tx_buffer));
    } else {
        strcpy(usb_tx_buffer, "HATA: INA228 iletisim kurulamadi!\r\n");
        CDC_Transmit_FS((uint8_t*)usb_tx_buffer, strlen(usb_tx_buffer));
    }

    // 2. TCA9548 ve NAU7802 Başlatma
    TCA9548_Init(&hi2c1, 0x00);
    TCA9548_SelectChannel(&hi2c1, 0); // Sadece itki ölçen 1. sensörün kanalı (Kanal 0)

    thrustSensor.hi2c = &hi2c1;
    if (NAU7802_Init(&hi2c1)) {
        // İhtiyaç duyarsan NAU7802_Tare(&thrustSensor, 10); gibi dara işlemlerini buraya ekleyebilirsin
    }

    HAL_TIM_PWM_Start(&htim3, TIM_CHANNEL_3);
}

/* USB'den Gelen Veriyi Yakalama (Interrupt içinden tetiklenir) */
void App_ProcessUsbData(uint8_t* Buf, uint32_t Len) {
    // Taşmayı engellemek için sınır kontrolü
    if (Len < sizeof(usb_rx_buffer)) {
        memcpy(usb_rx_buffer, Buf, Len);
        usb_rx_buffer[Len] = '\0'; // String sonlandırıcı ekle
        new_usb_data = true;       // Task'e haber ver
    }
}

/* Yüksek Öncelikli Görev: USB Komut Dinleyicisi */
void App_TaskUsbListen(void *argument) {
    for(;;) {
        if (new_usb_data) {
            new_usb_data = false; // Bayrağı indir
            App_ProcessCommand(usb_rx_buffer); // Gelen veriyi komut merkezine gönder
        }
        osDelay(10); // Komut kontrolünü 10ms'de bir yap
    }
}

/* KOMUT AYRIŞTIRICI MERKEZ (Command Parser) */
static void App_ProcessCommand(const char* cmd) {

    // 1. Okuma Başlat Komutu
    if (strstr(cmd, "start_reading") != NULL) {
        is_reading = true;
        App_BuzzerBeep(50);
        osDelay(50);
        App_BuzzerBeep(60);
    }

    // 2. Okuma Durdur Komutu
    else if (strstr(cmd, "stop_reading") != NULL) {
        is_reading = false;
        App_BuzzerBeep(200);
    }

    // 3. PWM Ayarlama Komutu
    else if (strncmp(cmd, "set_pwm_", 8) == 0) {
        int target_pwm;
        if (sscanf(cmd, "set_pwm_%d", &target_pwm) == 1) {
            // Güvenlik sınırları
            if (target_pwm < 1000) target_pwm = 1000;
            if (target_pwm > 2000) target_pwm = 2000;

            current_pwm = target_pwm;
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, current_pwm);

            // BİLGİSAYARA ONAY (ACK) MESAJI GÖNDERME KISMI
            char ack_msg[32];
            snprintf(ack_msg, sizeof(ack_msg), "ok pwm %d\r\n", current_pwm);

            // USB meşgulse bekle, hattı kilitlememek için max 5ms sınır koy
            uint32_t timeout = 0;
            while (CDC_Transmit_FS((uint8_t*)ack_msg, strlen(ack_msg)) == USBD_BUSY) {
            	osDelay(1);
            	if (++timeout >= 5) break;
            }

            App_BuzzerBeep(50);

        }
    }

    // 4. İleride Ekleyeceğin Dara Alma Komutu (Örnek)
    else if (strstr(cmd, "tare_thrust") != NULL) {
        // NAU7802_Tare(&thrustSensor, 10);
    }

    // 5. Acil Durum Durdurması (Örnek)
    else if (strstr(cmd, "emergency_stop") != NULL) {
        is_reading = false;
        current_pwm = 1000; // Motoru anında durdur
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_1, 1000);
    }
}

/* Normal Öncelikli Görev: Sensör Okuma ve Gönderme (Saniyede 10 Veri - 10 Hz) */
void App_TaskSensorRead(void *argument) {

    // Görevin kendi iç tampon belleği
    char local_tx_buffer[128];

    for(;;) {
        if (is_reading) {
            float busVoltage = 0.0f, currentAmp = 0.0f, powerWatt = 0.0f;
            int32_t thrustVal = 0;

            // 1. Sensörleri Oku
            if (INA228_IsConnected(&powerSensor)) {
                busVoltage = INA228_GetBusVoltage(&powerSensor);
                currentAmp = INA228_GetCurrent(&powerSensor);
                powerWatt  = INA228_GetPower(&powerSensor);
            }

            if (NAU7802_Available(&hi2c1)) {
                thrustVal = NAU7802_Read(&hi2c1);
            }

            // 2. Veriyi tek satır formatına getir (current_pwm değerini de ekledik)
            snprintf(local_tx_buffer, sizeof(local_tx_buffer),
                     "PWM:%d V:%.2f I:%.2f P:%.1f T:%ld\r\n",
                     current_pwm, busVoltage, currentAmp, powerWatt, thrustVal);

            // 3. USB'ye Gönder (Kilitlenme Korumalı)
            uint32_t timeout = 0;
            while (CDC_Transmit_FS((uint8_t*)local_tx_buffer, strlen(local_tx_buffer)) == USBD_BUSY) {
                osDelay(1);
                if (++timeout >= 5) {
                    break; // Hat 5ms boyunca boşalmadıysa sistemi kilitleme, devam et
                }
            }
        }

        // 100ms periyodu milimetrik olarak tuttur (İşlemciyi diğer görevlere bırakır)
        osDelay(100); // Tertemiz, şaşmaz ve güvenli 10 Hz bekleme
    }
}
