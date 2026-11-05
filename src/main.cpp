#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// ---- Wi-Fi & MQTT ----
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASS = "";
const char* MQTT_HOST = "broker.hivemq.com";
const int   MQTT_PORT = 1883;
const char* CLIENT_ID = "cleanbin-unib-rifqi01";

const char* TOPIC_TELEMETRY = "unib/cleanbin/rifqi01/telemetry";
const char* TOPIC_EVENT     = "unib/cleanbin/rifqi01/event";
const char* TOPIC_STATUS    = "unib/cleanbin/rifqi01/status";

// ---- Pin ----
#define PIN_TRIG_TANGAN 5
#define PIN_ECHO_TANGAN 18
#define PIN_TRIG_ISI    19
#define PIN_ECHO_ISI    21
#define PIN_SERVO       13
#define PIN_LED_PENUH   2
#define PIN_BUZZER      15

// ---- Parameter ----
const float JARAK_TANGAN_CM     = 20.0;
const float TINGGI_TONG_CM      = 30.0;
const float AMBANG_PENUH_PERSEN = 80.0;
const unsigned long INTERVAL_TANGAN_MS    = 200;
const unsigned long INTERVAL_TELEMETRI_MS = 2000;
const unsigned long TUTUP_SETELAH_MS      = 3000;
const unsigned long INTERVAL_BEEP_MS      = 500;
const unsigned long INTERVAL_RECONNECT_MS = 5000;

WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);
Servo tutup;

unsigned long waktuTanganCek = 0, waktuTanganTerlihat = 0;
unsigned long waktuTelemetri = 0, waktuBeep = 0, waktuReconnect = 0;
unsigned long nomorPaket = 0;
bool terbuka = false, penuh = false, beepOn = false;
float persenIsi = 0;

float bacaJarakCm(int trig, int echo) {
  digitalWrite(trig, LOW);
  delayMicroseconds(2);
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);
  long durasi = pulseIn(echo, HIGH, 30000);
  if (durasi == 0) return -1;
  return durasi * 0.0343 / 2;
}

// Cetak ke Serial sekaligus publish ke MQTT (kalau terhubung)
void kirim(const char* topik, const char* payload, bool retained = false) {
  Serial.println(payload);
  if (mqtt.connected()) mqtt.publish(topik, payload, retained);
}

void pastikanTerhubung(unsigned long sekarang) {
  if (WiFi.status() != WL_CONNECTED) return;
  if (mqtt.connected()) return;
  if (sekarang - waktuReconnect < INTERVAL_RECONNECT_MS) return;
  waktuReconnect = sekarang;

  Serial.println("{\"event\":\"mqtt_connecting\"}");
  // LWT: jika perangkat putus tiba-tiba, broker mempublish "offline"
  if (mqtt.connect(CLIENT_ID, nullptr, nullptr, TOPIC_STATUS, 1, true, "offline")) {
    mqtt.publish(TOPIC_STATUS, "online", true);
    Serial.println("{\"event\":\"mqtt_connected\"}");
  } else {
    Serial.printf("{\"event\":\"mqtt_failed\",\"rc\":%d}\n", mqtt.state());
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(PIN_TRIG_TANGAN, OUTPUT);
  pinMode(PIN_ECHO_TANGAN, INPUT);
  pinMode(PIN_TRIG_ISI, OUTPUT);
  pinMode(PIN_ECHO_ISI, INPUT);
  pinMode(PIN_LED_PENUH, OUTPUT);
  pinMode(PIN_BUZZER, OUTPUT);
  tutup.attach(PIN_SERVO);
  tutup.write(0);

  WiFi.begin(WIFI_SSID, WIFI_PASS, 6);  // channel 6 = cepat di Wokwi
  mqtt.setServer(MQTT_HOST, MQTT_PORT);
  mqtt.setBufferSize(512);
  mqtt.setSocketTimeout(2);

  Serial.println("{\"event\":\"boot\",\"msg\":\"CleanBin aktif\"}");
}

void loop() {
  unsigned long sekarang = millis();

  // 0. Jaga koneksi MQTT (non-blocking)
  pastikanTerhubung(sekarang);
  mqtt.loop();

  // 1. Sensor tangan -> buka/tutup
  if (sekarang - waktuTanganCek >= INTERVAL_TANGAN_MS) {
    waktuTanganCek = sekarang;
    float jarakTangan = bacaJarakCm(PIN_TRIG_TANGAN, PIN_ECHO_TANGAN);
    if (jarakTangan > 0 && jarakTangan < JARAK_TANGAN_CM) {
      waktuTanganTerlihat = sekarang;
      if (!terbuka) {
        tutup.write(90);
        terbuka = true;
        kirim(TOPIC_EVENT, "{\"event\":\"lid\",\"state\":\"open\"}");
      }
    }
  }
  if (terbuka && sekarang - waktuTanganTerlihat >= TUTUP_SETELAH_MS) {
    tutup.write(0);
    terbuka = false;
    kirim(TOPIC_EVENT, "{\"event\":\"lid\",\"state\":\"closed\"}");
  }

  // 2. Sensor isi + telemetri
  if (sekarang - waktuTelemetri >= INTERVAL_TELEMETRI_MS) {
    waktuTelemetri = sekarang;
    float jarakIsi = bacaJarakCm(PIN_TRIG_ISI, PIN_ECHO_ISI);
    if (jarakIsi < 0) {
      kirim(TOPIC_EVENT, "{\"error\":\"Gagal membaca sensor isi\"}");
    } else {
      persenIsi = constrain((TINGGI_TONG_CM - jarakIsi) / TINGGI_TONG_CM * 100.0, 0, 100);
      bool penuhBaru = (persenIsi >= AMBANG_PENUH_PERSEN);
      if (penuhBaru != penuh) {
        penuh = penuhBaru;
        char ev[64];
        snprintf(ev, sizeof(ev), "{\"event\":\"full_status\",\"full\":%s}", penuh ? "true" : "false");
        kirim(TOPIC_EVENT, ev);
      }
      digitalWrite(PIN_LED_PENUH, penuh ? HIGH : LOW);

      char buf[256];
      snprintf(buf, sizeof(buf),
        "{\"device_id\":\"cleanbin-01\",\"seq\":%lu,\"uptime_ms\":%lu,"
        "\"fill_cm\":%.1f,\"fill_pct\":%.1f,\"lid\":\"%s\",\"full\":%s}",
        ++nomorPaket, sekarang, jarakIsi, persenIsi,
        terbuka ? "open" : "closed", penuh ? "true" : "false");
      kirim(TOPIC_TELEMETRY, buf);
    }
  }

  // 3. Buzzer putus-putus saat penuh
  if (penuh) {
    if (sekarang - waktuBeep >= INTERVAL_BEEP_MS) {
      waktuBeep = sekarang;
      beepOn = !beepOn;
      if (beepOn) tone(PIN_BUZZER, 2000); else noTone(PIN_BUZZER);
    }
  } else if (beepOn) {
    noTone(PIN_BUZZER);
    beepOn = false;
  }
}