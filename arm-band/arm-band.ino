#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_SSD1306.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <WiFiClientSecure.h>


#include <Wire.h>
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// WiFi Credentials
const char* ssid = "inbook";
const char* password = "123456789";

// WiFi Client
WiFiClient client;

const int sensorPin = A0;  // A0 is the input pin for the heart rate sensor
const int statusLED = 2;   // GPIO2 (commonly labeled as D4 on NodeMCU)

// Variables
int sensorValue;           // Variable to store the value from the sensor
int count = 0;             // Pulse count
unsigned long starttime = 0;
unsigned long endtime = 0;
int heartrate = 0;         // Heart rate (BPM)
boolean counted = false;   // Flag to prevent multiple counts for the same pulse

// MPU6050 Object
Adafruit_MPU6050 mpu;

// OLED display object
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);  // Adjust parameters as needed

// Thresholds for detecting a fall
const float LOWER_THRESHOLD = 0.5;   // m/s^2
const float UPPER_THRESHOLD = 15.0;  // m/s^2

// Timing variables
unsigned long previousMillis = 0;  // Stores the last time data was sent
const long interval = 20000;       // Interval for sending data (20 seconds)

void setup() {
  Serial.begin(115200);  // Start serial communication
  Serial.println("Initializing MPU6050 and connecting to WiFi...");

  // Initialize MPU6050
  if (!mpu.begin()) {
    Serial.println("Failed to initialize MPU6050. Halting.");
    while (1);
  }
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_5_HZ);

  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connected!");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  // Initialize OLED display
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F("SSD1306 allocation failed"));
    for (;;); // Infinite loop
  }
  display.display();
  delay(2000);  // Pause for 2 seconds to show startup screen
}


void sendDataToAPI(int pulse, int bpm, float temperature, bool fallDetected) {
    if (WiFi.status() == WL_CONNECTED) {
        WiFiClientSecure client;
        client.setInsecure(); // Bypass SSL for testing; replace with a certificate for production

        HTTPClient http;

        // Construct API URL
        String apiURL = "https://mangalgrahsevasanstha.org.in/insert_data.php";

        // Construct POST data payload
        String postData = "pulse=" + String(pulse) +
                          "&bpm=" + String(bpm) +
                          "&temperature=" + String(temperature, 2) + // Format temperature to 2 decimal places
                          "&fall_detected=" + String(fallDetected ? 1 : 0);

        Serial.print("API URL: ");
        Serial.println(apiURL);
        Serial.print("POST Data: ");
        Serial.println(postData);

        // Start HTTP request
        http.begin(client, apiURL); // Use HTTPS with WiFiClientSecure
        http.addHeader("Content-Type", "application/x-www-form-urlencoded");

        // Send POST request
        int httpResponseCode = http.POST(postData);

        // Handle redirects (HTTP 301 or 302)
        if (httpResponseCode == 301 || httpResponseCode == 302) {
            String newLocation = http.header("Location");
            Serial.print("Redirected to: ");
            Serial.println(newLocation);

            if (!newLocation.isEmpty()) {
                http.end(); // Close the current connection
                http.begin(client, newLocation); // Start a new connection with the redirected URL
                http.addHeader("Content-Type", "application/x-www-form-urlencoded");
                httpResponseCode = http.POST(postData); // Retry POST request
            } else {
                Serial.println("Redirect location header is empty!");
            }
        }

        // Check the final response
        if (httpResponseCode > 0) {
            Serial.print("HTTP Response Code: ");
            Serial.println(httpResponseCode);
            Serial.print("Server Response: ");
            Serial.println(http.getString());
        } else {
            Serial.print("Error in sending POST request. HTTP Response Code: ");
            Serial.println(httpResponseCode);
        }

        // Close connection
        http.end();
    } else {
        Serial.println("WiFi Disconnected. Unable to send data.");
    }
}





void loop() {
  // Read data from MPU6050
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);
  sensorValue = analogRead(sensorPin);

  // Calculate the magnitude of acceleration
  float acceleration_magnitude = sqrt(
    pow(a.acceleration.x, 2) + pow(a.acceleration.y, 2) + pow(a.acceleration.z, 2)
  );

  // Heart rate detection logic
  if ((sensorValue >= 590 && sensorValue <= 680) && counted == false) { // Adjust the threshold range as needed
    count++;
    digitalWrite(statusLED, HIGH);  // Light up LED to show pulse detected
    delay(10);  // Small delay to prevent multiple counts
    digitalWrite(statusLED, LOW);
    counted = true;  // Prevent multiple counts for the same pulse
  } else if (sensorValue < 590) {  // Reset flag when pulse is over
    counted = false;
    digitalWrite(statusLED, LOW);
  }

  // Calculate BPM based on count over 15 seconds
  heartrate = (count * 60) / 15;  // BPM calculation based on 15-second count

  // Display BPM on OLED
  display.clearDisplay();
  display.setTextColor(WHITE);
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.println("Heart Rate");
  display.setCursor(0, 28);
  display.print("BPM: ");
  display.print(heartrate);
  display.display();

  // Log data to Serial Monitor
  Serial.println("=== Sensor Data ===");
  Serial.print("Acceleration Magnitude: ");
  Serial.println(acceleration_magnitude);
  Serial.print("Temperature: ");
  Serial.println(temp.temperature);
  Serial.println(heartrate);

  // Detect free fall or impact
  static bool fallDetected = false;
  if (acceleration_magnitude < LOWER_THRESHOLD) {
    Serial.println("Warning: Possible free-fall detected!");
    fallDetected = true;
  } else if (acceleration_magnitude > UPPER_THRESHOLD) {
    Serial.println("Warning: Sudden impact detected!");
    fallDetected = true;
  }

  // If fall detected, send data immediately
  if (fallDetected) {
    sendDataToAPI(heartrate, heartrate, temp.temperature, true);
    fallDetected = false;  // Reset the flag after sending
  }

  // Send data to API at regular intervals
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    sendDataToAPI(heartrate, heartrate, temp.temperature, false);
  }

  delay(500);  // Small delay to ensure stability
}

