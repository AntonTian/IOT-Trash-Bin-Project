#include <Arduino.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// ==========================================
// 1. PENGATURAN PIN
// ==========================================
const int trigPinHand = 19;
const int echoPinHand = 18;
const int trigPinBin = 5;
const int echoPinBin = 16;
const int servoPin = 21;
const int ledRed = 14;
const int ledGreen = 12;
const int ledBlue = 13;

Servo binServo;

// ==========================================
// 2. PENGATURAN MQTT ADAFRUIT IO
// ==========================================
const char* mqtt_server = "io.adafruit.com";
const int mqtt_port = 1883;

// --- GANTI DENGAN KREDENSIAL ADAFRUIT IO ANDA ---
const char* io_username = "USERNAME_ADAFRUIT_ANDA";
const char* io_key      = "AIO_KEY_ANDA";

// Format Topik: username/feeds/nama-feed
// PASTI KAN MENGGANTI "USERNAME_ADAFRUIT_ANDA" DI BAWAH INI JUGA!
const char* topic_capacity = "USERNAME_ADAFRUIT_ANDA/feeds/smartbin-capacity";
const char* topic_opens    = "USERNAME_ADAFRUIT_ANDA/feeds/smartbin-opens"; 

WiFiClient espClient;
PubSubClient client(espClient);

// Batas Jarak
const float DISTANCE_HAND_OPEN = 15.0; 
const float BIN_DEPTH_MAX = 40.0;      
const float BIN_FULL_THRESHOLD = 10.0; 

// Variabel Waktu & State
bool isBinOpen = false;
unsigned long lastPublishTime = 0;
const long publishInterval = 5000; 
unsigned long openTimestamp = 0;
const int openDuration = 3000; 

// ==========================================
// 3. FUNGSI PENDUKUNG
// ==========================================
float getDistance(int trigPin, int echoPin) {
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0) return 0;
  return duration * 0.034 / 2;
}

void setLedColor(int r, int g, int b) {
  digitalWrite(ledRed, r);
  digitalWrite(ledGreen, g);
  digitalWrite(ledBlue, b);
}

void reconnectMQTT() {
  while (!client.connected()) {
    Serial.print("Menghubungkan ke Adafruit IO...");
    String clientId = "ESP32-" + String(random(0xffff), HEX);
    
    // Login menggunakan Username dan AIO Key
    if (client.connect(clientId.c_str(), io_username, io_key)) {
      Serial.println("Berhasil!");
    } else {
      Serial.print("Gagal, rc=");
      Serial.print(client.state());
      Serial.println(" coba lagi dalam 5 detik");
      delay(5000);
    }
  }
}

// ==========================================
// 4. SETUP
// ==========================================
void setup() {
  Serial.begin(115200);

  pinMode(trigPinHand, OUTPUT);
  pinMode(echoPinHand, INPUT);
  pinMode(trigPinBin, OUTPUT);
  pinMode(echoPinBin, INPUT);

  pinMode(ledRed, OUTPUT);
  pinMode(ledGreen, OUTPUT);
  pinMode(ledBlue, OUTPUT);
  setLedColor(0, 0, 0);

  binServo.setPeriodHertz(50);
  binServo.attach(servoPin, 500, 2400);
  binServo.write(0);

  WiFiManager wm;
  Serial.println("Memulai WiFiManager...");
  if (!wm.autoConnect("SmartBin_Setup")) {
    Serial.println("Gagal terhubung ke WiFi");
    ESP.restart();
  }
  Serial.println("WiFi Terhubung!");

  client.setServer(mqtt_server, mqtt_port);
}

// ==========================================
// 5. LOOP UTAMA
// ==========================================
void loop() {
  if (!client.connected()) reconnectMQTT();
  client.loop();

  unsigned long currentMillis = millis();
  float handDistance = getDistance(trigPinHand, echoPinHand);
  
  // --- 1. LOGIKA BUKA TUTUP ---
  if (handDistance > 0 && handDistance <= DISTANCE_HAND_OPEN && !isBinOpen) {
    isBinOpen = true;
    binServo.write(180); 
    openTimestamp = currentMillis;
    Serial.println("Tempat Sampah Dibuka!");
    
    // Kirim angka 1 langsung ke topik (tanpa JSON)
    client.publish(topic_opens, "1");
  }

  // Auto-close
  if (isBinOpen && (currentMillis - openTimestamp >= openDuration)) {
    if (handDistance > DISTANCE_HAND_OPEN || handDistance == 0) {
      isBinOpen = false;
      binServo.write(0);
      Serial.println("Tempat Sampah Ditutup.");
    } else {
      openTimestamp = currentMillis; 
    }
  }

  // --- 2. LOGIKA KAPASITAS ---
  if (currentMillis - lastPublishTime >= publishInterval) {
    lastPublishTime = currentMillis;

    float capacityDistance = getDistance(trigPinBin, echoPinBin);
    float percentage = 0;
    
    if (capacityDistance > 0 && capacityDistance <= BIN_DEPTH_MAX) {
      percentage = ((BIN_DEPTH_MAX - capacityDistance) / (BIN_DEPTH_MAX - BIN_FULL_THRESHOLD)) * 100.0;
      if (percentage > 100) percentage = 100;
      if (percentage < 0) percentage = 0;
    }

    if (capacityDistance <= 0) {
      setLedColor(0, 0, 0); 
    } else if (capacityDistance <= BIN_FULL_THRESHOLD) {
      setLedColor(1, 0, 0); 
    } else if (capacityDistance > BIN_FULL_THRESHOLD && capacityDistance <= (BIN_DEPTH_MAX / 2)) {
      setLedColor(1, 1, 0); 
    } else {
      setLedColor(0, 1, 0); 
    }

    Serial.printf("Kapasitas Penuh: %.1f%%\n", percentage);
    
    // Kirim nilai persentase ke Adafruit IO (diubah menjadi String)
    client.publish(topic_capacity, String(percentage, 1).c_str());
  }

  delay(50);
}