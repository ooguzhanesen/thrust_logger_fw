#include "prdc_tmaesc.h"
#include <string.h>

// T-Motor Alpha Orijinal Big Endian Birleştirme Fonksiyonu
static inline uint16_t be16(const uint8_t *p) { return ((uint16_t)p[0] << 8) | p[1]; }

/* T-Motor NTC Sensör Sıcaklık Dönüşüm Tablosu */
typedef struct { uint8_t r, t; } temp_item_t;
static const temp_item_t temp_table[] = {
  {241,0},{240,1},{239,2},{238,3},{237,4},{236,5},{235,6},{234,7},{233,8},{232,9},
  {231,10},{230,11},{229,12},{228,13},{227,14},{226,15},{224,16},{223,17},{222,18},{220,19},
  {219,20},{217,21},{216,22},{214,23},{212,24},{210,25},{208,26},{207,27},{205,28},{203,29},
  {201,30},{199,31},{197,32},{195,33},{193,34},{190,35},{188,36},{186,37},{184,38},{181,39},
  {179,40},{177,41},{174,42},{172,43},{169,44},{167,45},{164,46},{162,47},{159,48},{157,49},
  {154,50},{152,51},{149,52},{146,53},{144,54},{141,55},{138,56},{136,57},{133,58},{131,59},
  {128,60},{125,61},{123,62},{120,63},{118,64},{115,65},{113,66},{110,67},{108,68},{105,69},
  {103,70},{100,71},{98,72},{95,73},{93,74},{91,75},{88,76},{86,77},{84,78},{82,79},
  {79,80},{77,81},{75,82},{73,83},{71,84},{69,85},{67,86},{65,87},{63,88},{61,89},
  {60,90},{58,91},{56,92},{54,93},{53,94},{51,95},{50,96},{48,97},{47,98},{45,99},
  {44,100},{42,101},{41,102},{40,103},{38,104},{37,105},{36,106},{35,107},{33,108},{32,109},
  {31,110},{30,111},{29,112},{28,113},{27,114},{26,115},{25,116},{24,117},{23,118},{23,119},
  {22,120},{21,121},{20,122},{20,123},{19,124},{18,125}
};

static uint8_t temp_decode(uint8_t r)
{
  if (r >= temp_table[0].r) return temp_table[0].t;
  size_t n = sizeof(temp_table)/sizeof(temp_table[0]);
  if (r <= temp_table[n-1].r) return temp_table[n-1].t;

  for (size_t i=0; i<n-1; i++) {
    if (r <= temp_table[i].r && r >= temp_table[i+1].r) {
      if ((temp_table[i].r - r) < (r - temp_table[i+1].r)) return temp_table[i].t;
      else return temp_table[i+1].t;
    }
  }
  return 25u;
}

/* Orijinal T-Motor Alpha (12 Byte) Paket Çözücü - Big Endian */
bool TMAESC_ParseByte(TMAESC_Handle *h, uint8_t b)
{
  switch (h->state)
  {
    case TMAESC_S_HDR0:
      if (b == TMAESC_HDR0) {
        h->buf[0] = b;
        h->idx   = 1;
        h->state = TMAESC_S_COLLECT;
      }
      break;

    case TMAESC_S_COLLECT:
      h->buf[h->idx++] = b;

      // 12 Byte tamamlandığında çözüme başla
      if (h->idx >= TMAESC_PACKET_SIZE) {

        // Byte 2-3: Voltaj, Byte 4-5: Akım, Byte 6-7: Hız (Big Endian)
        uint16_t vbus_raw = be16(&h->buf[2]);
        uint16_t ibus_raw = be16(&h->buf[4]);
        uint16_t rpm_raw  = be16(&h->buf[6]);

        h->last.time_ms         = HAL_GetTick();
        h->last.voltage_bus     = (float)vbus_raw / 10.0f; // Örn: 500 -> 50.0V
        h->last.current_bus     = (float)ibus_raw / 10.0f; // Örn: 125 -> 12.5A

        // Sinyal gürültüsünü engellemek için güvenlik kelepçesi
        if (rpm_raw == 0xFFFF || rpm_raw == 0) {
            h->last.rpm = 0;
        } else {
            // Standart E-RPM / (Kutup Sayısı / 2) formülü
            h->last.rpm = (uint16_t)((rpm_raw * 10.0f) / ((float)h->poles / 2.0f));
        }

        h->last.temperature_mos = temp_decode(h->buf[8]);
        h->last.status          = h->buf[10];

        h->new_frame = true;

        // Durum makinesini sıfırla
        h->state = TMAESC_S_HDR0;
        h->idx   = 0;
      }
      break;
  }
  return h->new_frame;
}

void TMAESC_Init(TMAESC_Handle *h, UART_HandleTypeDef *huart, uint8_t poles)
{
  memset(h, 0, sizeof(*h));
  h->huart  = huart;
  h->poles  = poles;
  h->state  = TMAESC_S_HDR0;
  h->idx    = 0;
  h->new_frame = false;
}

void TMAESC_StartIT(TMAESC_Handle *h)
{
  if(h && h->huart) {
    HAL_UART_Receive_IT(h->huart, &h->rx_byte, 1);
  }
}

void TMAESC_OnRxCplt(TMAESC_Handle *h)
{
  if(h && h->huart) {
    TMAESC_ParseByte(h, h->rx_byte);
    HAL_UART_Receive_IT(h->huart, &h->rx_byte, 1);
  }
}

bool TMAESC_TryGet(TMAESC_Handle *h, TMAESC_Data *out)
{
  bool res = false;
  __disable_irq();
  if (h->new_frame) {
    if (out) {
      *out = h->last;
    }
    h->new_frame = false;
    res = true;
  }
  __enable_irq();
  return res;
}
