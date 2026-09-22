#include <Arduino.h>
#include <WiFiManager.h>
#include <PubSubClient.h>
#include <ESP32Servo.h>

// --- Pin Definitions ---
const int trigPin = 19;
const int echoPin = 18;
const int servoPin = 21;
Servo myServo;

// --- MQTT Settings ---
// Using HiveMQ's free public broker
const char *mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883;

// Change "my_unique_esp32_123" to something unique so you don't conflict with others
const char *topic_pub_distance = "my_unique_esp32_123/distance";
const char *topic_sub_override = "my_unique_esp32_123/servo_override";

WiFiClient espClient;
PubSubClient client(espClient);

// --- Timing and State Variables ---
unsigned long overrideEndTime = 0;
bool isOverrideActive = false;
unsigned long lastPublishTime = 0;
const long publishInterval = 1000; // Publish distance every 1 second

// Function to calculate distance
float getDistance(int trigPin, int echoPin)
{
  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);

  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  // Added a 30,000 microsecond timeout so it doesn't block MQTT if nothing is detected
  long duration = pulseIn(echoPin, HIGH, 30000);
  if (duration == 0)
    return 0; // Timeout

  float distance = duration * 0.034 / 2;
  return distance;
}

// Callback function executed when an MQTT message is received
void mqttCallback(char *topic, byte *payload, unsigned int length)
{
  String messageTemp;
  for (unsigned int i = 0; i < length; i++)
  {
    messageTemp += (char)payload[i];
  }

  Serial.print("MQTT Override Command Received: ");
  Serial.println(messageTemp);

  // Convert the received message string to an integer angle
  int targetAngle = messageTemp.toInt();
  targetAngle = constrain(targetAngle, 0, 180);

  // Move the servo to the override angle
  myServo.write(targetAngle);

  // Activate the override state for the next 3 seconds (3000 milliseconds)
  isOverrideActive = true;
  overrideEndTime = millis() + 3000;
}

// Function to reconnect to MQTT broker if connection is lost
void reconnectMQTT()
{
  while (!client.connected())
  {
    Serial.print("Attempting MQTT connection...");
    // Create a random client ID
    String clientId = "ESP32Client-";
    clientId += String(random(0xffff), HEX);

    if (client.connect(clientId.c_str()))
    {
      Serial.println("Connected to MQTT Broker!");
      // Subscribe to the override command topic
      client.subscribe(topic_sub_override);
    }
    else
    {
      Serial.print("failed, rc=");
      Serial.print(client.state());
      Serial.println(" try again in 5 seconds");
      delay(5000);
    }
  }
}

void setup()
{
  Serial.begin(115200);

  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);

  myServo.setPeriodHertz(50);
  myServo.attach(servoPin, 500, 2400);

  // --- WiFiManager Setup ---
  WiFiManager wm;
  // wm.resetSettings(); // Uncomment to wipe saved WiFi credentials for testing

  Serial.println("Connecting to WiFi via WiFiManager...");
  // Creates an Access Point named "ESP32_Setup". Connect to it with your phone to configure WiFi.
  bool res = wm.autoConnect("ESP32_Setup");

  if (!res)
  {
    Serial.println("Failed to connect to WiFi");
    ESP.restart();
  }
  Serial.println("WiFi connected successfully!");

  // --- MQTT Setup ---
  client.setServer(mqtt_server, mqtt_port);
  client.setCallback(mqttCallback);
}

void loop()
{
  // Ensure MQTT remains connected
  if (!client.connected())
  {
    reconnectMQTT();
  }
  client.loop(); // Process incoming MQTT messages

  unsigned long currentMillis = millis();

  // Check if the 3-second override period has expired
  if (isOverrideActive && currentMillis > overrideEndTime)
  {
    isOverrideActive = false;
    Serial.println("Override finished. Returning to automatic mode.");
  }

  // Get distance
  float distance = getDistance(trigPin, echoPin);
  if (distance < 0)
    distance = 0;

  // Publish distance to MQTT every 1 second (to avoid spamming the free broker)
  if (currentMillis - lastPublishTime >= publishInterval)
  {
    lastPublishTime = currentMillis;
    String distString = String(distance, 2);
    client.publish(topic_pub_distance, distString.c_str());
  }

  // --- Automatic Servo Logic (Only runs if Override is NOT active) ---
  if (!isOverrideActive)
  {
    int servoAngle;

    // Less than 10 cm -> Clockwise (180), Else -> Counter-Clockwise (0)
    if (distance > 0 && distance < 10.0)
    {
      servoAngle = 180;
    }
    else
    {
      servoAngle = 0;
    }

    myServo.write(servoAngle);
  }

  delay(50); // Small delay for stability
}