# CleanBin: Tempat Sampah Pintar Berbasis ESP32

Proyek mata kuliah Teknologi IoT, Prodi S1 Informatika FT UNIB.

## Fitur
- Tutup otomatis terbuka saat tangan mendekat (HC-SR04 + servo)
- Pengukuran isi tempat sampah dalam persen (HC-SR04)
- Indikator penuh: LED dan buzzer
- Telemetri JSON dikirim via MQTT

## Komponen dan Pin
| Komponen | Pin ESP32 |
|---|---|
| HC-SR04 tangan (TRIG/ECHO) | D5 / D18 |
| HC-SR04 isi (TRIG/ECHO) | D19 / D21 |
| Servo | D13 |
| LED (via 220 Ω) | D2 |
| Buzzer | D15 |

## Topik MQTT
- `unib/cleanbin/rifqi01/telemetry`
- `unib/cleanbin/rifqi01/event`
- `unib/cleanbin/rifqi01/status`

## Cara Menjalankan
1. Pasang VS Code, PlatformIO IDE, dan Wokwi Simulator.
2. Buka folder proyek, lalu Build (PlatformIO).
3. Tekan F1, pilih "Wokwi: Start Simulator".
