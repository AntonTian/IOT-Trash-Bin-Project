#include <Arduino.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// ==========================================
// 1. PENGATURAN PIN (Hanya 2 Sensor)
// ==========================================
// Sensor 1: Deteksi Tangan (Mengendalikan Servo)
const int trigPinHand = 19;
const int echoPinHand = 18;

// Sensor 2: Deteksi Kapasitas Tong (Mengendalikan LED & MQTT)
const int trigPinBin = 17;
const int echoPinBin = 16;

// Pin Komponen Lainnya
const int servoPin = 21;
const int ledRed = 13; // Pin 13 dan 14 ditukar agar warna sesuai
const int ledGreen = 12;
const int ledBlue = 14;

Servo binServo;

// ==========================================
// 2. PENGATURAN SERVO CONTINUOUS (MG996R)
// ==========================================
const int SLOW_OPEN_SPEED = 0;    // Kecepatan menggulung tali
const int SLOW_CLOSE_SPEED = 180; // Kecepatan mengulur tali
const int STOP_MOTOR = 90;        // Berhenti

const int SPIN_TIME = 1500; // Durasi putaran (ms) untuk membuka/menutup
const int HOLD_TIME = 1500; // Durasi tutup terbuka (ms)

// State Machine untuk Servo Continuous
enum BinState
{
  CLOSED,
  OPENING,
  OPEN,
  CLOSING
};
BinState binState = CLOSED;
unsigned long actionTimestamp = 0;

// Variabel Penghitung (Counter)
int openCounter = 0;

// ==========================================
// 3. PENGATURAN MQTT ADAFRUIT IO
// ==========================================
const char *mqtt_server = "io.adafruit.com";
const int mqtt_port = 1883;

// --- KREDENSIAL ADAFRUIT IO ---
const char *io_username = "YOUR_USERNAME";
const char *io_key = "YOUR_SECRET_KEY";

const char *topic_capacity = "YOUR_USERNAME/feeds/smartbin-capacity";
const char *topic_opens = "YOUR_USERNAME/feeds/smartbin-opens";

WiFiClient espClient;
PubSubClient client(espClient);

// Batas Jarak
const float DISTANCE_HAND_OPEN = 15.0;
const float BIN_DEPTH_MAX = 23.61;
const float BIN_FULL_THRESHOLD = 3.0;

unsigned long lastPublishTime = 0;
const long publishInterval = 5000;

// ==========================================
// 4. FUNGSI PENDUKUNG
// ==========================================
float getDistance(int trigPin, int echoPin)
{
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000); // 30ms timeout
  if (duration == 0)
    return 0;
  return duration * 0.034 / 2;
}

void setLedColor(int r, int g, int b)
{
  digitalWrite(ledRed, r);
  digitalWrite(ledGreen, g);
  digitalWrite(ledBlue, b);
}

void reconnectMQTT()
{
  while (!client.connected())
  {
    Serial.print("Menghubungkan ke Adafruit IO...");
    String clientId = "ESP32-" + String(random(0xffff), HEX);

    if (client.connect(clientId.c_str(), io_username, io_key))
    {
      Serial.println("Berhasil!");
    }
    else
    {
      Serial.print("Gagal, rc=");
      Serial.print(client.state());
      Serial.println(" coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

// ==========================================
// 5. SETUP
// ==========================================
void setup()
{
  Serial.begin(115200);

  // Inisialisasi Pin Sensor
  pinMode(trigPinHand, OUTPUT);
  pinMode(echoPinHand, INPUT);
  pinMode(trigPinBin, OUTPUT);
  pinMode(echoPinBin, INPUT);

  // Inisialisasi LED
  pinMode(ledRed, OUTPUT);
  pinMode(ledGreen, OUTPUT);
  pinMode(ledBlue, OUTPUT);
  setLedColor(0, 1, 0); // Default ke Hijau saat menyala

  // Inisialisasi Servo
  binServo.setPeriodHertz(50);
  binServo.attach(servoPin, 500, 2400);
  binServo.write(STOP_MOTOR);

  // Inisialisasi WiFi
  WiFiManager wm;
  Serial.println("Memulai WiFiManager...");
  if (!wm.autoConnect("SmartBin_Setup"))
  {
    Serial.println("Gagal terhubung ke WiFi");
    ESP.restart();
  }
  Serial.println("WiFi Terhubung!");

  client.setServer(mqtt_server, mqtt_port);
}

// ==========================================
// 6. LOOP UTAMA
// ==========================================
void loop()
{
  if (!client.connected())
    reconnectMQTT();

  client.loop();
  unsigned long currentMillis = millis();

  // Baca sensor tangan (D19 & D18) untuk Servo
  float handDistance = getDistance(trigPinHand, echoPinHand);
  if (handDistance < 0)
    handDistance = 0;

  delay(5); // Jeda kecil agar sinyal ultrasonik tidak bertabrakan (cross-talk)

  // Baca sensor kapasitas (D17 & D16)
  float capacityDistance = getDistance(trigPinBin, echoPinBin);
  if (capacityDistance < 0)
    capacityDistance = 0;

  // --- MENGHITUNG PERSENTASE KAPASITAS (Real-time) ---
  float percentage = 0;
  if (capacityDistance > 0 && capacityDistance <= BIN_DEPTH_MAX)
  {
    percentage = ((BIN_DEPTH_MAX - capacityDistance) / (BIN_DEPTH_MAX - BIN_FULL_THRESHOLD)) * 100.0;
    if (percentage > 100)
      percentage = 100;
    if (percentage < 0)
      percentage = 0;
  }

  // --- 1. LOGIKA INDIKATOR LED KAPASITAS (Berdasarkan Persentase) ---
  if (percentage >= 85.0)
  {
    setLedColor(1, 0, 0); // Merah (Penuh: >= 85%)
  }
  else if (percentage <= 10.0)
  {
    setLedColor(0, 1, 0); // Hijau (Aman/Kosong: <= 10%)
  }
  else
  {
    setLedColor(1, 1, 0); // Kuning (Antara 10% dan 85%)
  }

  // --- 2. LOGIKA SERVO CONTINUOUS (NON-BLOCKING) ---
  if (handDistance > 0 && handDistance <= DISTANCE_HAND_OPEN && binState == CLOSED)
  {
    binState = OPENING;
    binServo.write(SLOW_OPEN_SPEED); // Mulai menggulung
    actionTimestamp = currentMillis;

    openCounter++;
    Serial.print("Tempat Sampah Dibuka! (Total buka: ");
    Serial.print(openCounter);
    Serial.println(" kali)");

    client.publish(topic_opens, String(openCounter).c_str());
  }

  if (binState == OPENING)
  {
    if (currentMillis - actionTimestamp >= SPIN_TIME)
    {
      binServo.write(STOP_MOTOR);
      binState = OPEN;
      actionTimestamp = currentMillis;
      Serial.println("Tutup terbuka. Menunggu...");
    }
  }
  else if (binState == OPEN)
  {
    if (currentMillis - actionTimestamp >= HOLD_TIME)
    {
      if (handDistance > 0 && handDistance <= DISTANCE_HAND_OPEN)
      {
        actionTimestamp = currentMillis;
      }
      else
      {
        binState = CLOSING;
        binServo.write(SLOW_CLOSE_SPEED);
        actionTimestamp = currentMillis;
        Serial.println("Waktu habis. Menutup perlahan...");
      }
    }
  }
  else if (binState == CLOSING)
  {
    if (currentMillis - actionTimestamp >= SPIN_TIME)
    {
      binServo.write(STOP_MOTOR);
      binState = CLOSED;
      Serial.println("Tempat Sampah Ditutup sepenuhnya.");
    }
  }

  // --- 3. LOGIKA MQTT KAPASITAS (Publish setiap 5 detik) ---
  if (currentMillis - lastPublishTime >= publishInterval)
  {
    lastPublishTime = currentMillis;

    // Menampilkan Jarak (cm) dan Persentase (%) di Serial Monitor
    Serial.print("Jarak Sampah: ");
    Serial.print(capacityDistance);
    Serial.print(" cm | ");
    Serial.printf("Kapasitas: %.1f%%\n", percentage);

    client.publish(topic_capacity, String(percentage, 1).c_str());
  }

  delay(20);
}