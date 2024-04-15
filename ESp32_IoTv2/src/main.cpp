#include <WiFi.h>
#include <HardwareSerial.h>
#include "SDS011.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <Adafruit_Sensor.h>
#include <DHT.h>
#include <DHT_U.h>
#include <PubSubClient.h>
#include "sys_capserver.hpp"
#include "sys_wifi.hpp"

const char *mqtt_ssid = "testuser";
const char *mqtt_password = "wukdeg-9pimty-zomCew";
const char *mqtt_server = "broker.hivemq.com";
const int mqtt_port = 1883; // MQTT default port
const char *mqtt_topic = "esp32/test11";

#define DHTPIN 19     // Digital pin connected to the DHT sensor
#define DHTTYPE DHT11 // DHT 11

#define BOOT_PIN 0

#define RX_PIN 16 // Choose appropriate RX pin on ESP32
#define TX_PIN 17 // Choose appropriate TX pin on ESP32

// Define a struct to hold PM2.5 and PM10 data
typedef struct
{
    float pm25;
    float pm10;
} SDS011_SensorData;

typedef struct
{
    float temp;
    float humi;
} DHT11_SensorData;

SDS011 sds011;
DHT_Unified dht(DHTPIN, DHTTYPE);
WiFiClient espClient;
PubSubClient client(espClient);
volatile uint8_t Interupt_Flag = 0;
QueueHandle_t sensor1Queue;
QueueHandle_t sensor2Queue;

// This function will be called when the BOOT pin transitions from LOW to HIGH (rising edge)
void bootInterruptHandler()
{
    Serial.println("Interupt ocur");
    Interupt_Flag = 1;
}

// Task function for Task 1
void task1Function(void *parameter)
{
    while (true)
    {
        float pm25, pm10;
        SDS011_SensorData SDS011_SensorData_t;
        vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);
        if (sds011.read(&pm25, &pm10))
        {
            SDS011_SensorData_t.pm25 = pm25;
            SDS011_SensorData_t.pm10 = pm10;
            xQueueOverwrite(sensor1Queue, &SDS011_SensorData_t);
        }
        else
        {
            Serial.println("Failed to read data from SDS011");
        }

        vTaskPrioritySet(NULL, 1);
        vTaskDelay(pdMS_TO_TICKS(500)); // Delay for 500 msecond
    }
}
void task2Function(void *parameter)
{
    while (true)
    {
        DHT11_SensorData DHT11_SensorData_t = {0, 0};

        vTaskPrioritySet(NULL, configMAX_PRIORITIES - 1);
        sensors_event_t event;

        dht.temperature().getEvent(&event);
        if (isnan(event.temperature))
        {
            Serial.println(F("Error reading temperature!"));
        }
        else
        {
            DHT11_SensorData_t.temp = event.temperature;
        }
        // Get humidity event and print its value.
        dht.humidity().getEvent(&event);
        vTaskPrioritySet(NULL, 1);
        if (isnan(event.relative_humidity))
        {
            Serial.println(F("Error reading humidity!"));
        }
        else
        {
            DHT11_SensorData_t.humi = event.relative_humidity;
        }
        xQueueOverwrite(sensor2Queue, &DHT11_SensorData_t);
        vTaskDelay(pdMS_TO_TICKS(500)); // Delay for 500 msecond
    }
}
void task3Function(void *parameter)
{
    while (true)
    {
        sys_capserver_proc();
        if (Interupt_Flag)
        {
            clearWiFiCredentials();
            Interupt_Flag = 0;
        }
        if (wifiState == WIFI_CONNECTED)
        {
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

void task4Function(void *parameter)
{
    while (true)
    {
        if (wifiState == WIFI_CONFIGURED_NOT_CONNECTED)
        {
            pinMode(2, OUTPUT);
            digitalWrite(2, !digitalRead(2)); // Toggle the LED pin state
        }

        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void task5Function(void *parameter)
{
    while (true)
    {
        SDS011_SensorData SDS011_SensorData_t;
        DHT11_SensorData DHT11_SensorData_t;
        // Read data from the queue and remove it
        if (xQueueReceive(sensor1Queue, &SDS011_SensorData_t, 0) == pdTRUE)
        { // 0 means don't block if the queue is empty
            // Print the data to the log
            Serial.print("PM2.5: ");
            Serial.print(SDS011_SensorData_t.pm25);
            Serial.print(", PM10: ");
            Serial.println(SDS011_SensorData_t.pm10);
        }
        else
        {
            // Queue is empty, print a message indicating that
            Serial.println("Queue is empty");
        }
        if (xQueueReceive(sensor2Queue, &DHT11_SensorData_t, 0) == pdTRUE)
        { // 0 means don't block if the queue is empty
            // Print the data to the log
            Serial.print("Temp: ");
            Serial.print(DHT11_SensorData_t.temp);
            Serial.print(", Humi: ");
            Serial.println(DHT11_SensorData_t.humi);
        }
        else
        {
            // Queue is empty, print a message indicating that
            Serial.println("Queue is empty");
        }
        if (wifiState == WIFI_CONNECTED)
        {
            // Create a character array to hold the JSON string
            char jsonBuffer[128];

            // Format the sensor data into the JSON string
            snprintf(jsonBuffer, 128, "{\"id\": %d, \"pm25\": %.2f, \"pm10\": %.2f, \"hum\": %.2f, \"temp\": %.2f}",
                     1, SDS011_SensorData_t.pm25, SDS011_SensorData_t.pm10, DHT11_SensorData_t.humi, DHT11_SensorData_t.temp);
            if (!client.connected())
            {
                client.connect("ESP32Client");
            }
            client.loop();
            client.publish(mqtt_topic, jsonBuffer);
        }
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}

void setup()
{
    Serial.begin(9600);
    sds011.begin(&Serial2);
    dht.begin();
    sys_wifi_init();
    sys_capserver_init();
    pinMode(BOOT_PIN, INPUT_PULLUP);                                                // Configure BOOT pin as input with internal pull-up resistor
    attachInterrupt(digitalPinToInterrupt(BOOT_PIN), bootInterruptHandler, RISING); // Attach interrupt handler to rising edge of BOOT pin
    Serial.println("All Done!");
    client.setServer(mqtt_server, mqtt_port);
    // Create queues
    sensor1Queue = xQueueCreate(1, sizeof(SDS011_SensorData)); // Adjust queue size and data type
    sensor2Queue = xQueueCreate(1, sizeof(DHT11_SensorData));  // Adjust queue size and data type

    xTaskCreate(
        task1Function, // Task function
        "Task 1",      // Task name
        10000,         // Stack size (bytes)
        NULL,          // Task parameters
        1,             // Task priority
        NULL           // Task handle
    );
    xTaskCreate(
        task2Function, // Task function
        "Task 2",      // Task name
        10000,         // Stack size (bytes)
        NULL,          // Task parameters
        1,             // Task priority
        NULL           // Task handle
    );
    xTaskCreate(
        task3Function, // Task function
        "Task 3",      // Task name
        10000,         // Stack size (bytes)
        NULL,          // Task parameters
        1,             // Task priority
        NULL           // Task handle
    );
    xTaskCreate(
        task4Function, // Task function
        "Task 4",      // Task name
        10000,         // Stack size (bytes)
        NULL,          // Task parameters
        1,             // Task priority
        NULL           // Task handle
    );
    xTaskCreate(
        task5Function, // Task function
        "Task 5",      // Task name
        10000,         // Stack size (bytes)
        NULL,          // Task parameters
        1,             // Task priority
        NULL           // Task handle
    );
}

void loop()
{
}
