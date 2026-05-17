#ifndef APPLICATION_H
#define APPLICATION_H

#include "main.h"
#include "cmsis_os.h"
#include <stdbool.h>

// Sensör kütüphaneleri
#include "INA228.h"
#include "NAU7802.h"
#include "TCA9548.h"

// main.c içinde çağrılacak RTOS görevleri
void App_Init(void);
void App_TaskSensorRead(void *argument);
void App_TaskUsbListen(void *argument);

// USB'den veri geldiğinde usbd_cdc_if.c içinden çağrılacak hook fonksiyonu
void App_ProcessUsbData(uint8_t* Buf, uint32_t Len);

#endif /* APPLICATION_H */
