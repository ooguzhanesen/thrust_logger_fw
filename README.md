# Rotary Aerospace | Thrust Logger Control Center

Endüstriyel BLDC motor ve ESC performans testleri için geliştirilmiş, yüksek hassasiyetli bir veri toplama (Data Acquisition - DAQ) ve canlı analiz masaüstü arayüzüdür. STM32 tabanlı gömülü sistem donanımıyla USB-CDC (Sanal COM Port) protokolü üzerinden 115200 baud hızında haberleşerek; anlık voltaj, akım, güç, itki (loadcell) ve motor devri (eRPM) verilerini görselleştirir, güvenliği denetler ve test verilerini kayıt altına alır.

## 🚀 Öne Çıkan Özellikler

* **Canlı Grafik Motoru:** `pyqtgraph` altyapısı kullanılarak 10 Hz hızında parazitsiz, pürüzsüz ve gerçek zamanlı 5 farklı sensör grafiği (eRPM, İtki, Voltaj, Akım, Güç).
* **Akıllı Veri Kayıt Sistemi (Toggle Record):** Tek bir akıllı buton üzerinden mikro saniye hassasiyetinde zaman damgalı CSV/Excel uyumlu log kaydı başlatma ve durdurma.
* **Gelişmiş Geçmiş Analiz Modülü (Log Viewer):** Kaydedilen geçmiş test verilerini tek tıkla açarak, **Uygulanan PWM Sinyali** dahil olmak üzere 6 farklı grafikte eş zamanlı ve senkronize analiz edebilme yeteneği.
* **Entegre Loadcell Kalibrasyon Aracı:** NAU7802 sinyal dönüştürücü için arayüz üzerinden tek tıkla dara (Tare) alma ve bilinen ağırlıklar üzerinden hassas çarpan (Divider) kalibrasyonu yapabilme.
* **Çift Aşamalı Güvenlik Kilidi (Interlock):**
    * Arayüz üzerinden anlık olarak set edilebilen Maksimum Akım ve Minimum Voltaj limitleri.
    * Okuma durdurulduğu veya acil stop tetiklendiği an motor sinyalini anında güvenli rölanti noktasına (1000 PWM) çeken gömülü koruma mekanizması.
* **Profesyonel Kullanıcı Deneyimi (UX):** Dinamik terminal günlüğü, otomatik tam ekran (Maximized) yerleşimi ve şık bir açılış ekranı (Splash Screen) mimarisi.

## 📊 Sistem Mimarisi ve Veri Akışı
## 🔌 USB Haberleşme Protokolü (API)

Yazılım katmanları arasında veri senkronizasyonunu sağlayan tüm USB seri port komut seti listesi aşağıdaki gibidir. Sistem, bu komutları `\n` (LF) karakteri ile ayırt eder.

* `start_reading` : 10 Hz hızında sensör veri akışını başlatır. (Dönüş: `ok start_reading`)
* `stop_reading` : Veri akışını durdurur, PWM sinyalini güvenli 1000 konumuna çeker. (Dönüş: `ok stop_reading`)
* `emergency_stop` : Motoru anında durdurur (1000 PWM) ve donanımsal alarm verir. (Dönüş: `ok emergency_stop`)
* `set_pwm_<değer>` : Manuel PWM sinyali yollar (1000-2000). Örn: `set_pwm_1150` (Dönüş: `ok pwm 1150`)
* `test_<sn>_<adım>` : Otonom kademeli testi başlatır. Örn: `test_10_50` (Dönüş: `ok test delay:10 step:50`)
* `tare` : Loadcell için dara (offset) alır ve Flash'a yazar. (Dönüş: `ok tare (Offset: XXXX)`)
* `calibrate_<gram>` : Bilinen ağırlık ile loadcell çarpanını ayarlar. Örn: `calibrate_1000` (Dönüş: `ok calibrate (Factor: X.XX)`)
* `calibrate_esc` : ESC kalibrasyon döngüsünü başlatır. (Sadece batarya sökülüyken çalışır).
* `set_pol_<adet>` : Motor kutup sayısını ayarlar. Örn: `set_pol_28` (Dönüş: `ok pol 28 (SAVED)`)
* `set_max_current_<A>` : Maksimum akım korumasını set eder. Örn: `set_max_current_30` (Dönüş: `ok max_current 30.0`)
* `set_min_voltage_<V>` : Minimum voltaj korumasını set eder. Örn: `set_min_voltage_22.5` (Dönüş: `ok min_voltage 22.5`)
* `status` : Kayıtlı limitleri, kutup sayısını ve kalibrasyon değerlerini döner.

### 💡 Teknik Haberleşme Detayları
* **Protokol:** USB-CDC (Serial Emulation - 115200 bps).
* **Bitiş Karakteri:** `\n` (Line Feed).
* **Kalıcılık:** `tare`, `calibrate`, `set_pol`, `set_max_current` ve `set_min_voltage` komutları tetiklendiğinde parametreler otomatik olarak STM32'nin **Flash (EEPROM Emülasyonu)** hafızasına yazılır.
