/**
 * ESP32-S3 Chiller Alarm Monitoring with Firebase
 * Structure based on FirebaseClient library example
 * 
 * Features:
 * - Alarm detection (2 channels)
 * - WiFi monitoring
 * - Firebase real-time database
 * - LCD display (ST7735)
 * - RTC timestamp (DS3231)
 * - Event logging
 */

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <FirebaseClient.h>
#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_ST7735.h>
#include <Adafruit_GFX.h>
#include <SPI.h>
#include <WiFi.h>

// ============================================
// FIREBASE CREDENTIALS (FILL IN YOUR DATA)
// ============================================
#define API_KEY "YOUR_API_KEY_HERE"
#define USER_EMAIL "YOUR_EMAIL_HERE"
#define USER_PASSWORD "YOUR_PASSWORD_HERE"
#define DATABASE_URL "YOUR_DATABASE_URL_HERE"

// ============================================
// WIFI CONFIGURATION
// ============================================
#define WIFI_SSID "OPPO F9"
#define WIFI_PASSWORD "1234567881"
IPAddress staticIP(192, 168, 43, 50);
IPAddress gateway(192, 168, 43, 1);
IPAddress subnet(255, 255, 255, 0);

// ============================================
// HARDWARE PINS
// ============================================
// I2C pins (ESP32-S3)
const int SDA_PIN = 5;
const int SCL_PIN = 6;

// TFT pins
const uint8_t TFT_CS = 16, TFT_RST = 15, TFT_DC = 7;
const uint8_t TFT_MOSI = 8, TFT_SCLK = 18;

// Alarm pins
const uint8_t ALARM1_PIN = 19;
const uint8_t ALARM2_PIN = 21;

// ============================================
// DEVICE CONFIGURATION
// ============================================
#define DEVICE_ID "esp32-s3-001"

// Debounce settings
const unsigned long DEBOUNCE_DELAY = 50;
const uint8_t DEBOUNCE_READS = 5;
const unsigned long STATE_CHANGE_LOCKOUT = 500;

// Logging
#define MAX_LOGS 10
struct LogEntry {
  char timestamp[20];
  char message[50];
};

// ============================================
// FIREBASE SETUP
// ============================================
SSL_CLIENT ssl_client;

using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);

UserAuth user_auth(API_KEY, USER_EMAIL, USER_PASSWORD, 3000);
FirebaseApp app;
RealtimeDatabase Database;
AsyncResult databaseResult;

// ============================================
// DISPLAY SETUP
// ============================================
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

#define COLOR_BLACK 0x0000
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_WHITE 0xFFFF
#define COLOR_YELLOW 0xFFE0
#define COLOR_CYAN 0x07FF

// ============================================
// RTC SETUP
// ============================================
RTC_DS3231 rtc;
bool rtc_available = true;
unsigned long startup_millis = 0;

// ============================================
// GLOBAL VARIABLES
// ============================================
LogEntry logs[MAX_LOGS];
uint8_t log_count = 0;

// WiFi status
bool wifi_connected = false;
unsigned long lastWiFiReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000;

// Alarm states
int lastAlarm1State = -1;
int lastAlarm2State = -1;
unsigned long lastAlarm1ChangeTime = 0;
unsigned long lastAlarm2ChangeTime = 0;

// Firebase status
bool firebase_ready = false;
unsigned long lastFirebaseCheck = 0;
const unsigned long FIREBASE_CHECK_INTERVAL = 5000;

// ============================================
// FUNCTION DECLARATIONS
// ============================================
void processData(AsyncResult &aResult);
void sendAlarmAlert(uint8_t alarmNum, const char* status);
void sendWiFiStatusAlert(const char* status);
String getTimestamp();
void printTimestamped(const char* msg);
void addLog(const char* message);
void updateDisplay();
int debouncePin(uint8_t pin, int lastState);

void setup()
{
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n\n=== ESP32-S3 Chiller Monitoring with Firebase Started ===\n");
    startup_millis = millis();
    
    // Initialize hardware
    initTFT();
    initRTC();
    initAlarmPins();
    
    for (uint8_t i = 0; i <= 100; i += 10) {
        displayProgressBar(i);
        delay(200);
    }
    
    // Initialize WiFi
    initWiFi();
    
    // Initialize Firebase
    Serial.println("\n🔥 Initializing Firebase...");
    
    set_ssl_client_insecure_and_buffer(ssl_client);
    
    initializeApp(aClient, app, getAuth(user_auth), "🔐 authTask");
    
    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);
    
    Serial.println("✅ Firebase initialization started");
    
    delay(500);
    
    // Initialize alarm states
    lastAlarm1State = digitalRead(ALARM1_PIN);
    lastAlarm2State = digitalRead(ALARM2_PIN);
    
    log_count = 0;
    addLog(lastAlarm1State == LOW ? "A1: AKTIF" : "A1: NORMAL");
    addLog(lastAlarm2State == LOW ? "A2: AKTIF" : "A2: NORMAL");
    
    printTimestamped(lastAlarm1State == LOW ? "A1 AKTIF" : "A1 NORMAL");
    printTimestamped(lastAlarm2State == LOW ? "A2 AKTIF" : "A2 NORMAL");
    
    updateDisplay();
    Serial.println("✅ Initialization Complete!");
}

void loop()
{
    // Maintain Firebase authentication and async tasks
    app.loop();
    
    // Update WiFi status
    updateWiFiStatus();
    
    // Check Firebase ready status
    if (millis() - lastFirebaseCheck >= FIREBASE_CHECK_INTERVAL) {
        lastFirebaseCheck = millis();
        firebase_ready = app.ready();
    }
    
    // Check Alarm 1
    if (millis() - lastAlarm1ChangeTime >= STATE_CHANGE_LOCKOUT) {
        int alarm1 = debouncePin(ALARM1_PIN, lastAlarm1State);
        if (alarm1 != -1 && alarm1 != lastAlarm1State) {
            handleAlarmChange(1, alarm1);
            lastAlarm1State = alarm1;
            lastAlarm1ChangeTime = millis();
            updateDisplay();
        }
    }
    
    // Check Alarm 2
    if (millis() - lastAlarm2ChangeTime >= STATE_CHANGE_LOCKOUT) {
        int alarm2 = debouncePin(ALARM2_PIN, lastAlarm2State);
        if (alarm2 != -1 && alarm2 != lastAlarm2State) {
            handleAlarmChange(2, alarm2);
            lastAlarm2State = alarm2;
            lastAlarm2ChangeTime = millis();
            updateDisplay();
        }
    }
    
    // Process Firebase results
    processData(databaseResult);
    
    delay(10);
}

// ============================================
// INITIALIZATION FUNCTIONS
// ============================================

void initRTC() {
    Wire.begin(SDA_PIN, SCL_PIN);
    if (!rtc.begin()) {
        rtc_available = false;
        Serial.println("⚠️  RTC tidak terdeteksi! Menggunakan uptime saja.");
    } else {
        Serial.println("✅ RTC DS3231 terdeteksi");
        if (rtc.lostPower()) {
            Serial.println("⚠️  RTC kehilangan power, setting waktu...");
            rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
        }
    }
}

void initTFT() {
    tft.initR(INITR_144GREENTAB);
    tft.setRotation(0);
    tft.fillScreen(COLOR_BLACK);
    Serial.println("✅ TFT ST7735 initialized");
}

void initAlarmPins() {
    pinMode(ALARM1_PIN, INPUT_PULLUP);
    pinMode(ALARM2_PIN, INPUT_PULLUP);
    Serial.println("✅ Alarm pins initialized (pin 19, 21)");
}

void initWiFi() {
    Serial.println("\n📡 Initializing WiFi...");
    Serial.printf("SSID: %s\n", WIFI_SSID);
    Serial.printf("Static IP: %s\n", staticIP.toString().c_str());
    
    if (!WiFi.config(staticIP, gateway, subnet)) {
        Serial.println("❌ Failed to configure static IP!");
    } else {
        Serial.println("✅ Static IP configured");
    }
    
    WiFi.mode(WIFI_STA);
    connectWiFiNonBlocking();
}

// ============================================
// WiFi MANAGEMENT
// ============================================

void updateWiFiStatus() {
    unsigned long currentTime = millis();
    int wifiStatus = WiFi.status();
    bool currentWiFiConnected = (wifiStatus == WL_CONNECTED);
    
    if (currentWiFiConnected != wifi_connected) {
        wifi_connected = currentWiFiConnected;
        wifi_connected ? handleWiFiConnect() : handleWiFiDisconnect();
        updateDisplay();
    }
    
    if (!wifi_connected && (currentTime - lastWiFiReconnectAttempt >= WIFI_RECONNECT_INTERVAL)) {
        connectWiFiNonBlocking();
    }
}

void connectWiFiNonBlocking() {
    if (WiFi.status() == WL_DISCONNECTED || WiFi.status() == WL_IDLE_STATUS) {
        String ts = getTimestamp();
        Serial.printf("[%s] Connecting to WiFi: \"%s\"\n", ts.c_str(), WIFI_SSID);
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        lastWiFiReconnectAttempt = millis();
    }
}

void handleWiFiConnect() {
    String ts = getTimestamp();
    Serial.printf("[%s] ✅ WiFi Connected!\n", ts.c_str());
    Serial.printf("    IP Address: %s\n", WiFi.localIP().toString().c_str());
    addLog("WiFi: CON");
}

void handleWiFiDisconnect() {
    String ts = getTimestamp();
    Serial.printf("[%s] ⚠️  WiFi Disconnected!\n", ts.c_str());
    Serial.printf("[%s] Alarm detection continues offline...\n", ts.c_str());
    addLog("WiFi: DIS");
}

// ============================================
// ALARM HANDLING
// ============================================

int debouncePin(uint8_t pin, int lastState) {
    int readings[DEBOUNCE_READS];
    bool allSame = true;
    
    for (uint8_t i = 0; i < DEBOUNCE_READS; i++) {
        readings[i] = digitalRead(pin);
        if (i > 0 && readings[i] != readings[0]) allSame = false;
        delay(DEBOUNCE_DELAY);
    }
    
    return (allSame && readings[0] != lastState) ? readings[0] : -1;
}

void handleAlarmChange(uint8_t alarmNum, int newState) {
    String message = String("A") + String(alarmNum) + ": " + ((newState == LOW) ? "AKTIF" : "NORMAL");
    printTimestamped(message.c_str());
    addLog(message.c_str());
    
    if (firebase_ready) {
        sendAlarmAlert(alarmNum, (newState == LOW) ? "AKTIF" : "NORMAL");
    }
}

// ============================================
// FIREBASE FUNCTIONS
// ============================================

void sendAlarmAlert(uint8_t alarmNum, const char* status) {
    String ts = getTimestamp();
    
    // Build the path
    String path = "/devices/" DEVICE_ID "/alerts/alarm" + String(alarmNum);
    
    // Create JSON object using object_t
    object_t json_obj, alarm_obj;
    json_obj["alarm_number"] = (int)alarmNum;
    json_obj["status"] = status;
    json_obj["timestamp"] = ts.c_str();
    json_obj["type"] = "ALARM_CHANGE";
    
    // Send to Firebase
    bool status_result = Database.set<object_t>(aClient, path.c_str(), json_obj);
    
    if (status_result) {
        Serial.printf("[%s] ✅ Alarm alert sent to Firebase\n", ts.c_str());
    } else {
        Serial.printf("[%s] ❌ Firebase send failed\n", ts.c_str());
    }
}

void sendWiFiStatusAlert(const char* status) {
    String ts = getTimestamp();
    
    // Build the path
    String path = "/devices/" DEVICE_ID "/alerts/wifi_status";
    
    // Create JSON object
    object_t json_obj;
    json_obj["wifi_status"] = status;
    json_obj["timestamp"] = ts.c_str();
    json_obj["type"] = "WIFI_STATUS";
    
    // Send to Firebase
    bool status_result = Database.set<object_t>(aClient, path.c_str(), json_obj);
    
    if (status_result) {
        Serial.printf("[%s] ✅ WiFi alert sent to Firebase\n", ts.c_str());
    } else {
        Serial.printf("[%s] ❌ Firebase send failed\n", ts.c_str());
    }
}

void createEventLog(const char* event) {
    String ts = getTimestamp();
    
    // Build the path
    String path = "/devices/" DEVICE_ID "/event_logs/" + ts;
    path.replace(":", "-");
    
    // Create JSON object
    object_t json_obj;
    json_obj["event"] = event;
    json_obj["timestamp"] = ts.c_str();
    json_obj["type"] = "LOG";
    
    // Send to Firebase
    bool status_result = Database.set<object_t>(aClient, path.c_str(), json_obj);
    
    if (status_result) {
        Serial.printf("[%s] ✅ Event log sent to Firebase\n", ts.c_str());
    } else {
        Serial.printf("[%s] ❌ Firebase send failed\n", ts.c_str());
    }
}

void processData(AsyncResult &aResult) {
    // Exit when no result available
    if (!aResult.isResult())
        return;
    
    if (aResult.isEvent()) {
        Firebase.printf("Event: %s, msg: %s, code: %d\n", 
            aResult.uid().c_str(), 
            aResult.eventLog().message().c_str(), 
            aResult.eventLog().code());
    }
    
    if (aResult.isDebug()) {
        Firebase.printf("Debug: %s\n", aResult.debug().c_str());
    }
    
    if (aResult.isError()) {
        Firebase.printf("Error: %s, code: %d\n", 
            aResult.error().message().c_str(), 
            aResult.error().code());
    }
    
    if (aResult.available()) {
        Firebase.printf("Response: %s\n", aResult.c_str());
    }
}

// ============================================
// TIMESTAMP & LOGGING
// ============================================

String getTimestamp() {
    if (rtc_available) {
        DateTime now = rtc.now();
        char buffer[20];
        sprintf(buffer, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
        return String(buffer);
    } else {
        unsigned long uptime = millis() - startup_millis;
        char buffer[20];
        sprintf(buffer, "%02lu:%02lu:%02lu", 
                (uptime / 3600000) % 24, 
                (uptime / 60000) % 60, 
                (uptime / 1000) % 60);
        return String(buffer);
    }
}

void printTimestamped(const char* msg) {
    String ts = getTimestamp();
    Serial.printf("[%s] %s\n", ts.c_str(), msg);
}

void addLog(const char* message) {
    if (log_count < MAX_LOGS) {
        String ts = getTimestamp();
        strcpy(logs[log_count].timestamp, ts.c_str());
        strcpy(logs[log_count].message, message);
        log_count++;
    } else {
        for (uint8_t i = 0; i < MAX_LOGS - 1; i++) {
            strcpy(logs[i].timestamp, logs[i + 1].timestamp);
            strcpy(logs[i].message, logs[i + 1].message);
        }
        String ts = getTimestamp();
        strcpy(logs[MAX_LOGS - 1].timestamp, ts.c_str());
        strcpy(logs[MAX_LOGS - 1].message, message);
    }
}

// ============================================
// DISPLAY FUNCTIONS
// ============================================

void displayProgressBar(uint8_t progress) {
    tft.fillScreen(COLOR_BLACK);
    tft.setTextColor(COLOR_YELLOW);
    tft.setTextSize(1);
    tft.setCursor(20, 30);
    tft.println("INITIALIZING...");
    
    tft.drawRect(10, 70, 108, 20, COLOR_WHITE);
    uint8_t fillWidth = (progress * 108) / 100;
    tft.fillRect(11, 71, fillWidth - 1, 18, COLOR_CYAN);
    
    tft.setTextColor(COLOR_WHITE);
    tft.setCursor(50, 100);
    tft.printf("%d%%", progress);
}

void updateDisplay() {
    tft.fillScreen(COLOR_BLACK);
    displayAlarmStatus();
    displayLogs();
}

void displayAlarmStatus() {
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 5);
    tft.println("STATUS:");
    
    int alarm1Color = (lastAlarm1State == LOW) ? COLOR_RED : COLOR_GREEN;
    tft.setTextColor(alarm1Color);
    tft.setCursor(5, 18);
    tft.printf("A1: %s", (lastAlarm1State == LOW) ? "AKTIF" : "NORMAL");
    
    int alarm2Color = (lastAlarm2State == LOW) ? COLOR_RED : COLOR_GREEN;
    tft.setTextColor(alarm2Color);
    tft.setCursor(5, 30);
    tft.printf("A2: %s", (lastAlarm2State == LOW) ? "AKTIF" : "NORMAL");
    
    int wifiColor = wifi_connected ? COLOR_GREEN : COLOR_RED;
    tft.setTextColor(wifiColor);
    tft.setCursor(5, 42);
    tft.printf("WiFi: %s", wifi_connected ? "CON" : "DIS");
    
    int fbColor = firebase_ready ? COLOR_GREEN : COLOR_RED;
    tft.setTextColor(fbColor);
    tft.setCursor(5, 52);
    tft.printf("FB: %s", firebase_ready ? "RDY" : "...");
    
    tft.drawLine(0, 62, 128, 62, COLOR_WHITE);
    
    tft.setTextColor(COLOR_YELLOW);
    tft.setCursor(5, 67);
    tft.println("LOGS:");
}

void displayLogs() {
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    
    uint8_t displayCount = (log_count < 4) ? log_count : 4;
    uint8_t startY = 77;
    uint8_t lineHeight = 10;
    
    // Display from oldest to newest (newest at bottom)
    for (uint8_t i = 0; i < displayCount; i++) {
        uint8_t logIndex = log_count - displayCount + i;
        
        tft.setCursor(5, startY + (i * lineHeight));
        tft.printf("%s %s", logs[logIndex].timestamp, logs[logIndex].message);
    }
}
