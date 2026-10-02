#include <Arduino.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

const int trigPinHand = 19;
const int echoPinHand = 18;
const int trigPinBin = 17;
const int echoPinBin = 16;
const int servoPin = 21;
const int ledRed = 13;
const int ledGreen = 12;
const int ledBlue = 14;

Servo binServo;

const int SLOW_OPEN_SPEED = 0;
const int SLOW_CLOSE_SPEED = 180;
const int STOP_MOTOR = 90;
const int SPIN_TIME = 1500;
const int HOLD_TIME = 1500;

enum BinState
{
  CLOSED,
  OPENING,
  OPEN,
  CLOSING
};

BinState binState = CLOSED;
unsigned long actionTimestamp = 0;
int openCounter = 0;

const char *mqtt_server = "io.adafruit.com";
const int mqtt_port = 1883;
const char *io_username = "YOUR_USERNAME";
const char *io_key = "YOUR_SECRET_KEY";
const char *topic_capacity = "YOUR_USERNAME/feeds/smartbin-capacity";
const char *topic_opens = "YOUR_USERNAME/feeds/smartbin-opens";

WiFiClient espClient;
PubSubClient client(espClient);

const float DISTANCE_HAND_OPEN = 15.0;
const float BIN_DEPTH_MAX = 23.61;
const float BIN_FULL_THRESHOLD = 3.0;

unsigned long lastPublishTime = 0;
const long publishInterval = 5000;

float getDistance(int trigPin, int echoPin)
{
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  long duration = pulseIn(echoPin, HIGH, 30000);
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

void setup()
{
  Serial.begin(115200);

  pinMode(trigPinHand, OUTPUT);
  pinMode(echoPinHand, INPUT);
  pinMode(trigPinBin, OUTPUT);
  pinMode(echoPinBin, INPUT);
  pinMode(ledRed, OUTPUT);
  pinMode(ledGreen, OUTPUT);
  pinMode(ledBlue, OUTPUT);
  setLedColor(0, 1, 0);

  binServo.setPeriodHertz(50);
  binServo.attach(servoPin, 500, 2400);
  binServo.write(STOP_MOTOR);

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

void loop()
{
  if (!client.connected())
    reconnectMQTT();

  client.loop();
  unsigned long currentMillis = millis();

  float handDistance = getDistance(trigPinHand, echoPinHand);
  if (handDistance < 0)
    handDistance = 0;

  delay(5);

  float capacityDistance = getDistance(trigPinBin, echoPinBin);
  if (capacityDistance < 0)
    capacityDistance = 0;

  float percentage = 0;
  if (capacityDistance > 0 && capacityDistance <= BIN_DEPTH_MAX)
  {
    percentage = ((BIN_DEPTH_MAX - capacityDistance) / (BIN_DEPTH_MAX - BIN_FULL_THRESHOLD)) * 100.0;
    if (percentage > 100)
      percentage = 100;
    if (percentage < 0)
      percentage = 0;
  }

  if (percentage >= 85.0)
  {
    setLedColor(1, 0, 0);
  }
  else if (percentage <= 10.0)
  {
    setLedColor(0, 1, 0);
  }
  else
  {
    setLedColor(1, 1, 0);
  }

  if (handDistance > 0 && handDistance <= DISTANCE_HAND_OPEN && binState == CLOSED)
  {
    binState = OPENING;
    binServo.write(SLOW_OPEN_SPEED);
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

  if (currentMillis - lastPublishTime >= publishInterval)
  {
    lastPublishTime = currentMillis;
    Serial.print("Jarak Sampah: ");
    Serial.print(capacityDistance);
    Serial.print(" cm | ");
    Serial.printf("Kapasitas: %.1f%%\n", percentage);

    client.publish(topic_capacity, String(percentage, 1).c_str());
  }

  delay(20);
}