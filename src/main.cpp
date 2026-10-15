// Blynk Credentials
#define BLYNK_TEMPLATE_ID "-"
#define BLYNK_TEMPLATE_NAME "-"
#define BLYNK_AUTH_TOKEN "-"
#define BLYNK_PRINT Serial

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <BlynkSimpleEsp32.h>
#include <ESP32Servo.h>

// Nomor pin yang digunakan
const int trigPinHand = 19;
const int echoPinHand = 18;
const int trigPinBin = 17;
const int echoPinBin = 16;
const int servoPin = 21;
const int buttonPin = 22;
const int ledRed = 13;
const int ledGreen = 12;
const int ledBlue = 14;

Servo binServo;

// Konfigurasi kecepatan servo
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

// variabel debounce tombol pickup
int buttonState = HIGH;
int lastFlickerableState = HIGH;
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 50;

// threshold sensor ultrasonik
const float DISTANCE_HAND_OPEN = 15.0;
const float BIN_DEPTH_MAX = 23.61;
const float BIN_FULL_THRESHOLD = 3.0;

unsigned long lastPublishTime = 0;
const long publishInterval = 5000;
float capacityDistance = BIN_DEPTH_MAX;
float percentage = 0.0;

// get distance function
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

// clean distance function (Median Filter)
float getCleanDistanceCm(int trigPin, int echoPin)
{
  const int NUM_SAMPLES = 7;
  float readings[NUM_SAMPLES];

  for (int i = 0; i < NUM_SAMPLES; i++)
  {
    digitalWrite(trigPin, LOW);
    delayMicroseconds(2);
    digitalWrite(trigPin, HIGH);
    delayMicroseconds(10);
    digitalWrite(trigPin, LOW);

    long duration = pulseIn(echoPin, HIGH, 30000);
    if (duration == 0)
    {
      readings[i] = 999.0;
    }
    else
    {
      readings[i] = duration * 0.0343 / 2.0;
    }
    delay(30);
  }

  for (int i = 0; i < NUM_SAMPLES - 1; i++)
  {
    for (int j = 0; j < NUM_SAMPLES - i - 1; j++)
    {
      if (readings[j] > readings[j + 1])
      {
        float temp = readings[j];
        readings[j] = readings[j + 1];
        readings[j + 1] = temp;
      }
    }
  }
  return readings[NUM_SAMPLES / 2];
}

// warna lampu LED
void setLedColor(int r, int g, int b)
{
  digitalWrite(ledRed, r);
  digitalWrite(ledGreen, g);
  digitalWrite(ledBlue, b);
}

// Pickup button function
void checkPickupButton()
{
  unsigned long currentMillis = millis();
  int currentState = digitalRead(buttonPin);

  if (currentState != lastFlickerableState)
  {
    lastDebounceTime = currentMillis;
    lastFlickerableState = currentState;
  }

  if ((currentMillis - lastDebounceTime) > debounceDelay)
  {
    if (currentState != buttonState)
    {
      buttonState = currentState;
      if (buttonState == LOW)
      {
        Serial.println("====== Sampah telah diambil! ======");

        percentage = 0.0;
        setLedColor(0, 1, 0);

        Blynk.virtualWrite(V3, 1);
        Blynk.virtualWrite(V1, 0.0);

        delay(100);
        Blynk.virtualWrite(V3, 0);
      }
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
  pinMode(buttonPin, INPUT_PULLUP);

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

  Blynk.config(BLYNK_AUTH_TOKEN);
  Blynk.connect();
}

void loop()
{
  Blynk.run();

  unsigned long currentMillis = millis();

  // Pickup button logic
  checkPickupButton();

  // Hand sensor & servo logic
  float handDistance = getDistance(trigPinHand, echoPinHand);
  if (handDistance < 0)
    handDistance = 0;

  if (handDistance > 0 && handDistance <= DISTANCE_HAND_OPEN && binState == CLOSED)
  {
    binState = OPENING;
    binServo.write(SLOW_OPEN_SPEED);
    actionTimestamp = currentMillis;

    openCounter++;
    Serial.print("Tempat Sampah Dibuka! (Total buka: ");
    Serial.print(openCounter);
    Serial.println(" kali)");

    Blynk.virtualWrite(V2, openCounter);
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

  // capacity sensor logic
  if (currentMillis - lastPublishTime >= publishInterval && binState == CLOSED)
  {
    lastPublishTime = currentMillis;

    float cleanDist = getCleanDistanceCm(trigPinBin, echoPinBin);

    if (cleanDist < 900.0)
    {
      capacityDistance = cleanDist;
      percentage = ((BIN_DEPTH_MAX - capacityDistance) / (BIN_DEPTH_MAX - BIN_FULL_THRESHOLD)) * 100.0;
      if (percentage > 100.0)
        percentage = 100.0;
      if (percentage < 0.0)
        percentage = 0.0;
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

    Serial.print("Jarak Sampah (Filtered): ");
    Serial.print(capacityDistance);
    Serial.print(" cm | Kapasitas: ");
    Serial.print(percentage);
    Serial.println("%");

    Blynk.virtualWrite(V1, percentage);
  }

  delay(20);
}