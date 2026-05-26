#include "application.h"
#include "usbd_cdc_if.h"
#include "prdc_tmaesc.h"
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

// DMA Tamponu
static uint8_t dma_rx_buffer[TMAESC_PACKET_SIZE];

// USB Komut ve Durum Değişkenleri
static volatile bool is_reading = false;
static volatile bool new_usb_data = false;
static char usb_rx_buffer[64];

static uint16_t current_pwm = 1000;
static uint16_t esc_rpm = 0;
static volatile float live_bus_voltage = 0.0f; // Arka planda sürekli güncellenen güvenli voltaj

// Güvenlik Limitleri
static float max_current_limit = 999.0f;
static float min_voltage_limit = 0.0f;

// --- LOADCELL DEĞİŞKENLERİ ---
static int32_t loadcell_offset = 0;       // Dara değeri (Sıfır noktası)
static float loadcell_divider = 1.0f;     // Kalibrasyon çarpanı (Gram başına düşen ham değer)

static bool auto_test_active = false;
static uint32_t auto_test_interval_ms = 0;
static int auto_test_pwm_step = 0;
static uint32_t auto_test_last_time = 0;

// --- FLASH HAFIZA (EEPROM) AYARLARI ---
#define FLASH_STORAGE_ADDR 0x08060000
#define FLASH_MAGIC_NUMBER 0x14532027

typedef struct {
    uint32_t magic;
    float max_current;
    float min_voltage;
    uint32_t poles;
    int32_t loadcell_offset;
    float loadcell_divider;
} AppSettings;

// --- FONKSİYON PROTOTİPLERİ ---
static void App_ProcessCommand(const char* cmd);
static void App_BuzzerBeep(uint32_t delay_ms);
static void App_SendAck(const char* msg);
static void Load_Settings(void);
static void Save_Settings(void);

/* Hafızadan Ayarları Yükle */
static void Load_Settings(void) {
    AppSettings* flash_ptr = (AppSettings*)FLASH_STORAGE_ADDR;

    if (flash_ptr->magic == FLASH_MAGIC_NUMBER) {
        max_current_limit = flash_ptr->max_current;
        min_voltage_limit = flash_ptr->min_voltage;
        esc_handler.poles = (uint8_t)flash_ptr->poles;
        loadcell_offset = flash_ptr->loadcell_offset;
        loadcell_divider = flash_ptr->loadcell_divider;
    }
}

/* Ayarları Hafızaya Kazı */
static void Save_Settings(void) {
    AppSettings new_settings;
    new_settings.magic = FLASH_MAGIC_NUMBER;
    new_settings.max_current = max_current_limit;
    new_settings.min_voltage = min_voltage_limit;
    new_settings.poles = esc_handler.poles;
    new_settings.loadcell_offset = loadcell_offset;
    new_settings.loadcell_divider = loadcell_divider;

    HAL_FLASH_Unlock();

    FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SectorError = 0;
    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.VoltageRange = FLASH_VOLTAGE_RANGE_3;
    EraseInitStruct.Sector = FLASH_SECTOR_7;
    EraseInitStruct.NbSectors = 1;
    HAL_FLASHEx_Erase(&EraseInitStruct, &SectorError);

    uint32_t* data_ptr = (uint32_t*)&new_settings;
    for (int i = 0; i < (sizeof(AppSettings) / 4); i++) {
        HAL_FLASH_Program(FLASH_TYPEPROGRAM_WORD, FLASH_STORAGE_ADDR + (i * 4), data_ptr[i]);
    }

    HAL_FLASH_Lock();
}

/* USB'den Onay Mesajı Gönderir */
static void App_SendAck(const char* msg) {
    uint32_t timeout = 0;
    while (CDC_Transmit_FS((uint8_t*)msg, strlen(msg)) == USBD_BUSY) {
        osDelay(1);
        if (++timeout >= 5) break;
    }
}

/* Buzzer Ses Çıkarır */
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

    TMAESC_Init(&esc_handler, &huart3, 28);
    Load_Settings();

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
    char ack_msg[256];

    if (strstr(cmd, "start_reading") != NULL) {
        is_reading = true;
        App_SendAck("ok start_reading\r\n");
        App_BuzzerBeep(50); osDelay(50); App_BuzzerBeep(50);
    }
    // GÜNCELLEME: Stop edildiğinde PWM'i 1000'e (Sıfır Gaz) çek.
    else if (strstr(cmd, "stop_reading") != NULL) {
        is_reading = false;
        auto_test_active = false;

        current_pwm = 1000;
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 1000);

        App_SendAck("ok stop_reading\r\n");
        App_BuzzerBeep(120);
    }
    else if (strstr(cmd, "emergency_stop") != NULL) {
        is_reading = false;
        auto_test_active = false;
        current_pwm = 1000;
        __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 1000);
        App_SendAck("ok emergency_stop\r\n");
        App_BuzzerBeep(500);
    }

    // ==========================================
    // ESC KALİBRASYON KOMUTU (10 SN BEKLEMELİ)
    // ==========================================
    else if (strstr(cmd, "calibrate_esc") != NULL) {
        // Doğrudan arka planda okunan güvenli voltajı kontrol et
        if (live_bus_voltage > 5.0f) {
            // Batarya takılıysa işlemi kesinlikle reddet!
            App_SendAck("\r\n!!! ERROR: CALIBRATION FAILED !!!\r\n"
                        "!!! PLEASE DISCONNECT MAIN BATTERY FIRST !!!\r\n");
            App_BuzzerBeep(800); // Hata sesi
        }
        else {
            // Batarya takılı değil, işlem güvenli.
            is_reading = false;
            auto_test_active = false;

            // 1. ADIM: PWM'i Maksimuma (2000) Çek
            current_pwm = 2000;
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 2000);

            App_SendAck("\r\n--- ESC CALIBRATION MODE ---\r\n"
                        "-> PWM set to 2000 (MAX).\r\n"
                        "-> ACTION: PLUG IN ESC BATTERY NOW!\r\n"
                        "-> Waiting 10 seconds...\r\n");

            App_BuzzerBeep(1000); // Bataryayı tak uyarısı

            // Bataryanın takılması için 10 saniye (10000 ms) bekle
            osDelay(10000);

            // 2. ADIM: PWM'i Minimuma (1000) Çek
            current_pwm = 1000;
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 1000);

            App_SendAck("\r\n-> PWM set to 1000 (MIN).\r\n"
                        "-> ACTION: Wait for ESC confirmation beeps.\r\n"
                        "--- CALIBRATION DONE ---\r\n");

            App_BuzzerBeep(150); osDelay(100); App_BuzzerBeep(150);
        }
    }
    else if (strstr(cmd, "status") != NULL) {
        snprintf(ack_msg, sizeof(ack_msg),
                 "\r\n--- ROTARY AEROSPACE TEST STAND STATUS ---\r\n"
                 "Max Current Limit : %.1f A\r\n"
                 "Min Voltage Limit : %.1f V\r\n"
                 "Motor Pole Count  : %d\r\n"
                 "Loadcell Offset   : %ld\r\n"
                 "Loadcell Divider  : %.2f\r\n"
                 "----------------------------------------\r\n",
                 max_current_limit, min_voltage_limit, esc_handler.poles, loadcell_offset, loadcell_divider);
        App_SendAck(ack_msg);
        App_BuzzerBeep(50);
    }
    else if (strncmp(cmd, "set_pol_", 8) == 0) {
        int target_pol;
        if (sscanf(cmd, "set_pol_%d", &target_pol) == 1 && target_pol > 0) {
            esc_handler.poles = target_pol;
            Save_Settings();
            snprintf(ack_msg, sizeof(ack_msg), "ok pol %d (SAVED)\r\n", target_pol);
            App_SendAck(ack_msg);
            App_BuzzerBeep(50);
        }
    }
    else if (strncmp(cmd, "set_max_current_", 16) == 0) {
        float target_cur;
        if (sscanf(cmd, "set_max_current_%f", &target_cur) == 1) {
            max_current_limit = target_cur;
            Save_Settings();
            snprintf(ack_msg, sizeof(ack_msg), "ok max_current %.1f (SAVED)\r\n", max_current_limit);
            App_SendAck(ack_msg);
            App_BuzzerBeep(200); osDelay(100); App_BuzzerBeep(200);
        }
    }
    else if (strncmp(cmd, "set_min_voltage_", 16) == 0) {
        float target_vol;
        if (sscanf(cmd, "set_min_voltage_%f", &target_vol) == 1) {
            min_voltage_limit = target_vol;
            Save_Settings();
            snprintf(ack_msg, sizeof(ack_msg), "ok min_voltage %.1f (SAVED)\r\n", min_voltage_limit);
            App_SendAck(ack_msg);
            App_BuzzerBeep(200); osDelay(100); App_BuzzerBeep(200);
        }
    }
    else if (strncmp(cmd, "test_", 5) == 0) {
        int delay_sec, pwm_step;
        if (sscanf(cmd, "test_%d_%d", &delay_sec, &pwm_step) == 2) {
            auto_test_interval_ms = delay_sec * 1000;
            auto_test_pwm_step = pwm_step;
            auto_test_active = true;
            auto_test_last_time = HAL_GetTick();

            snprintf(ack_msg, sizeof(ack_msg), "ok test delay:%d step:%d\r\n", delay_sec, pwm_step);
            App_SendAck(ack_msg);
            App_BuzzerBeep(50); osDelay(50); App_BuzzerBeep(50);
        }
    }
    else if (strncmp(cmd, "set_pwm_", 8) == 0) {
        int target_pwm;
        if (sscanf(cmd, "set_pwm_%d", &target_pwm) == 1) {
            if (target_pwm < 1000) target_pwm = 1000;
            if (target_pwm > 2000) target_pwm = 2000;

            current_pwm = target_pwm;
            auto_test_active = false;
            __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, current_pwm);

            snprintf(ack_msg, sizeof(ack_msg), "ok pwm %d\r\n", current_pwm);
            App_SendAck(ack_msg);
            App_BuzzerBeep(50);
        }
    }

    // A. DARA ALMA
    else if (strstr(cmd, "tare") != NULL) {
        int32_t sum = 0;
        int num_samples = 10;
        for(int i = 0; i < num_samples; i++) {
            sum += NAU7802_Read(&hi2c1);
            osDelay(20);
        }
        loadcell_offset = sum / num_samples;
        Save_Settings();
        snprintf(ack_msg, sizeof(ack_msg), "ok tare (Offset: %ld) (SAVED)\r\n", loadcell_offset);
        App_SendAck(ack_msg);
        App_BuzzerBeep(100); osDelay(100); App_BuzzerBeep(100);
    }

    // B. KALİBRASYON
    else if (strncmp(cmd, "calibrate_", 10) == 0) {
        float known_weight_grams;
        if (sscanf(cmd, "calibrate_%f", &known_weight_grams) == 1 && known_weight_grams > 0) {
            int32_t sum = 0;
            int num_samples = 10;
            for(int i = 0; i < num_samples; i++) {
                sum += NAU7802_Read(&hi2c1);
                osDelay(20);
            }
            int32_t raw_average = sum / num_samples;
            loadcell_divider = (float)(raw_average - loadcell_offset) / known_weight_grams;
            if(loadcell_divider == 0) loadcell_divider = 1.0f;
            Save_Settings();
            snprintf(ack_msg, sizeof(ack_msg), "ok calibrate (Factor: %.2f) (SAVED)\r\n", loadcell_divider);
            App_SendAck(ack_msg);
            App_BuzzerBeep(100); osDelay(100); App_BuzzerBeep(100);
        }
    }
}

/* GÜNCELLEME: Sensörler her zaman okunur, güvenlik her an tetiktedir! */
void App_TaskSensorRead(void *argument) {
    char local_tx_buffer[256];
    TMAESC_Data esc_data;

    for(;;) {
        // --- 1. SENSÖRLERİ HER ZAMAN OKU (is_reading'den BAĞIMSIZ) ---
        float busVoltage = 0.0f, currentAmp = 0.0f, powerWatt = 0.0f;
        float thrust_grams = 0.0f;

        if (INA228_IsConnected(&powerSensor)) {
            busVoltage = INA228_GetBusVoltage(&powerSensor);
            currentAmp = INA228_GetCurrent(&powerSensor);
            powerWatt  = INA228_GetPower(&powerSensor);

            // Kalibrasyon kontrolü için global voltajı tazele
            live_bus_voltage = busVoltage;

            // GÜVENLİK LİMİTLERİ: Okuma kapalı olsa bile aşılırsa sistemi durdur!
            if (currentAmp > max_current_limit) {
                current_pwm = 1000;
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 1000);
                is_reading = false;
                auto_test_active = false;
                App_SendAck("\r\n!!! ALARM: MAX CURRENT EXCEEDED! STOPPED !!!\r\n");
                App_BuzzerBeep(800);
            }
            else if (busVoltage < min_voltage_limit && busVoltage > 5.0f) {
                current_pwm = 1000;
                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, 1000);
                is_reading = false;
                auto_test_active = false;
                App_SendAck("\r\n!!! ALARM: LOW VOLTAGE! STOPPED !!!\r\n");
                App_BuzzerBeep(200); osDelay(100); App_BuzzerBeep(200);
            }
        }
        else {
            live_bus_voltage = 0.0f;
        }

        // LOADCELL SÜREKLİ OKUNUR
        if (NAU7802_Available(&hi2c1)) {
            int32_t raw_val = NAU7802_Read(&hi2c1);
            thrust_grams = (float)(raw_val - loadcell_offset) / loadcell_divider;
        }

        // ESC TELEMETRİSİ SÜREKLİ OKUNUR
        if (TMAESC_TryGet(&esc_handler, &esc_data)) {
            esc_rpm = esc_data.rpm;
        }

        // --- 2. OTOMATİK TEST İŞLEYİCİSİ ---
        // Okuma aktifse ve test modu açıksa kademeli PWM artışını yap
        if (auto_test_active && is_reading) {
            if (HAL_GetTick() - auto_test_last_time >= auto_test_interval_ms) {
                current_pwm += auto_test_pwm_step;

                if (current_pwm > 2000) {
                    current_pwm = 1000;
                    auto_test_active = false;
                    App_SendAck("\r\n--- AUTO TEST COMPLETED (MAX PWM) ---\r\n");
                }

                __HAL_TIM_SET_COMPARE(&htim3, TIM_CHANNEL_3, current_pwm);
                App_BuzzerBeep(50);
                auto_test_last_time = HAL_GetTick();
            }
        }

        // --- 3. SADECE OKUMA AÇIKSA VERİYİ USB'DEN GÖNDER ---
        if (is_reading) {
            snprintf(local_tx_buffer, sizeof(local_tx_buffer),
            		"PWM:%d V:%.2f I:%.2f P:%.2f T:%.1f eRPM:%u\r\n",
					current_pwm, busVoltage, currentAmp, powerWatt, thrust_grams, esc_rpm);

            uint32_t timeout = 0;
            while (CDC_Transmit_FS((uint8_t*)local_tx_buffer, strlen(local_tx_buffer)) == USBD_BUSY) {
                osDelay(1);
                if (++timeout >= 5) break;
            }
        }

        // DMA Tamponunu tazele
        if (huart3.RxState == HAL_UART_STATE_READY) {
             HAL_UART_Receive_DMA(&huart3, dma_rx_buffer, TMAESC_PACKET_SIZE);
        }

        osDelay(100); // 10Hz Döngü (100ms)
    }
}
