/*
 * ESP32-S3 Chiller Alarm Monitoring with Firebase
 * Based on: https://RandomNerdTutorials.com/esp32-firebase-realtime-database/
 * 
 * Features:
 * - Alarm detection (2 channels)
 * - WiFi monitoring
 * - Firebase Realtime Database
 * - LCD display (ST7735)
 * - RTC timestamp (DS3231)
 * - Event logging
 */

#define ENABLE_USER_AUTH
#define ENABLE_DATABASE

#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <FirebaseClient.h>
#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_ST7735.h>
#include <Adafruit_GFX.h>
#include <SPI.h>

// ============================================
// FIREBASE CREDENTIALS
// ============================================
#define WIFI_SSID "OPPO F9"
#define WIFI_PASSWORD "1234567881"

#define Web_API_KEY "AIzaSyB5RmZyN3fMAU54Yx9HzatqHT1yp8lYeSs"
#define DATABASE_URL "https://percobaan-c496e-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "admin1@gmail.com"
#define USER_PASS "admin1234"

// ============================================
// DEVICE CONFIGURATION
// ============================================
#define DEVICE_ID "esp32-s3-001"

// ============================================
// HARDWARE PINS
// ============================================
// I2C pins (ESP32-S3)
const int SDA_PIN = 5;
const int SCL_PIN = 6;

// TFT LCD pins
const uint8_t TFT_CS = 16, TFT_RST = 15, TFT_DC = 7;
const uint8_t TFT_MOSI = 8, TFT_SCLK = 18;

// Alarm pins
const uint8_t ALARM1_PIN = 19;
const uint8_t ALARM2_PIN = 21;

// ============================================
// DEBOUNCE & TIMING
// ============================================
const unsigned long DEBOUNCE_DELAY = 50;
const uint8_t DEBOUNCE_READS = 5;
const unsigned long STATE_CHANGE_LOCKOUT = 500;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000;

// ============================================
// LOGGING
// ============================================
#define MAX_LOGS 10
struct LogEntry {
  char timestamp[20];
  char message[50];
};

// ============================================
// COLORS
// ============================================
#define COLOR_BLACK 0x0000
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_WHITE 0xFFFF
#define COLOR_YELLOW 0xFFE0
#define COLOR_CYAN 0x07FF

// ============================================
// FIREBASE SETUP
// ============================================
WiFiClientSecure ssl_client;
UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);
FirebaseApp app;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// ============================================
// LCD SETUP
// ============================================
Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

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

// Alarm states
int lastAlarm1State = -1;
int lastAlarm2State = -1;
unsigned long lastAlarm1ChangeTime = 0;
unsigned long lastAlarm2ChangeTime = 0;

// ============================================
// FUNCTION DECLARATIONS
// ============================================
void processData(AsyncResult &aResult);
void sendAlarmAlert(uint8_t alarmNum, const char* status);
void sendWiFiStatusAlert(const char* status);
String getTimestamp();
String getDisplayTime();
void printTimestamped(const char* msg);
void addLog(const char* message);
void updateDisplay();
int debouncePin(uint8_t pin, int lastState);

// ============================================
// SETUP
// ============================================
void setup() {
    Serial.begin(115200);
    delay(2000);
    
    Serial.println("\n\n=== ESP32-S3 Chiller Alarm Monitoring ===\n");
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
    
    ssl_client.setInsecure();
    ssl_client.setConnectionTimeout(1000);
    ssl_client.setHandshakeTimeout(5);
    
    initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
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
    Serial.println("✅ Initialization Complete!\n");
}

// ============================================
// MAIN LOOP
// ============================================
void loop() {
    // Maintain Firebase authentication and async tasks
    app.loop();
    
    // Update WiFi status
    updateWiFiStatus();
    
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
    
    if (app.ready()) {
        sendAlarmAlert(alarmNum, (newState == LOW) ? "AKTIF" : "NORMAL");
    }
}

// ============================================
// FIREBASE FUNCTIONS
// ============================================

void sendAlarmAlert(uint8_t alarmNum, const char* status) {
    String ts = getTimestamp();
    String baseAlarmPath = "/devices/" DEVICE_ID "/alarm";
    String alarmPath = baseAlarmPath + String(alarmNum);
    
    // Send individual values (Option 1 approach)
    Database.set<int>(aClient, alarmPath + "/number", (int)alarmNum, processData, "RTDB_Alarm_Number");
    Database.set<String>(aClient, alarmPath + "/status", status, processData, "RTDB_Alarm_Status");
    Database.set<String>(aClient, alarmPath + "/timestamp", ts, processData, "RTDB_Alarm_Timestamp");
    Database.set<String>(aClient, alarmPath + "/type", "ALARM_CHANGE", processData, "RTDB_Alarm_Type");
    
    Serial.printf("[%s] 📤 Alarm %d alert sent to Firebase\n", ts.c_str(), alarmNum);
}

void sendWiFiStatusAlert(const char* status) {
    String ts = getTimestamp();
    String wifiPath = "/devices/" DEVICE_ID "/wifi_status";
    
    // Send individual values (Option 1 approach)
    Database.set<String>(aClient, wifiPath + "/wifi_status", status, processData, "RTDB_WiFi_Status");
    Database.set<String>(aClient, wifiPath + "/timestamp", ts, processData, "RTDB_WiFi_Timestamp");
    Database.set<String>(aClient, wifiPath + "/type", "WIFI_STATUS", processData, "RTDB_WiFi_Type");
    
    Serial.printf("[%s] 📤 WiFi status alert sent to Firebase\n", ts.c_str());
}

void createEventLog(const char* event) {
    String ts = getTimestamp();
    String pathKey = ts;
    pathKey.replace(" ", "_");
    pathKey.replace(":", "-");
    
    String logPath = "/devices/" DEVICE_ID "/event_logs/" + pathKey;
    
    // Send individual values (Option 1 approach)
    Database.set<String>(aClient, logPath + "/event", event, processData, "RTDB_Log_Event");
    Database.set<String>(aClient, logPath + "/timestamp", ts, processData, "RTDB_Log_Timestamp");
    Database.set<String>(aClient, logPath + "/type", "LOG", processData, "RTDB_Log_Type");
}

void processData(AsyncResult &aResult) {
    if (!aResult.isResult())
        return;
    
    if (aResult.isEvent()) {
        Firebase.printf("🔔 Event: %s, msg: %s, code: %d\n", 
            aResult.uid().c_str(), 
            aResult.eventLog().message().c_str(), 
            aResult.eventLog().code());
    }
    
    if (aResult.isDebug()) {
        Firebase.printf("🐛 Debug: %s\n", aResult.debug().c_str());
    }
    
    if (aResult.isError()) {
        Firebase.printf("❌ Error: %s, code: %d\n", 
            aResult.error().message().c_str(), 
            aResult.error().code());
    }
    
    if (aResult.available()) {
        Firebase.printf("✅ Response: %s\n", aResult.c_str());
    }
}

// ============================================
// TIMESTAMP & LOGGING
// ============================================

String getTimestamp() {
    if (rtc_available) {
        DateTime now = rtc.now();
        char buffer[25];
        sprintf(buffer, "%04d-%02d-%02d %02d:%02d:%02d", 
                now.year(), now.month(), now.day(),
                now.hour(), now.minute(), now.second());
        return String(buffer);
    } else {
        unsigned long uptime = millis() - startup_millis;
        char buffer[25];
        sprintf(buffer, "2026-01-01 %02lu:%02lu:%02lu", 
                (uptime / 3600000) % 24, 
                (uptime / 60000) % 60, 
                (uptime / 1000) % 60);
        return String(buffer);
    }
}

String getDisplayTime() {
    if (rtc_available) {
        DateTime now = rtc.now();
        char buffer[20];
        sprintf(buffer, "%02d:%02d:%02d", 
                now.hour(), now.minute(), now.second());
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
        String displayTime = getDisplayTime();
        strcpy(logs[log_count].timestamp, displayTime.c_str());
        strcpy(logs[log_count].message, message);
        log_count++;
    } else {
        for (uint8_t i = 0; i < MAX_LOGS - 1; i++) {
            strcpy(logs[i].timestamp, logs[i + 1].timestamp);
            strcpy(logs[i].message, logs[i + 1].message);
        }
        String displayTime = getDisplayTime();
        strcpy(logs[MAX_LOGS - 1].timestamp, displayTime.c_str());
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
    
    int fbColor = app.ready() ? COLOR_GREEN : COLOR_RED;
    tft.setTextColor(fbColor);
    tft.setCursor(5, 52);
    tft.printf("FB: %s", app.ready() ? "RDY" : "...");
    
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
    
    for (uint8_t i = 0; i < displayCount; i++) {
        uint8_t logIndex = log_count - displayCount + i;
        
        tft.setCursor(5, startY + (i * lineHeight));
        tft.printf("%s %s", logs[logIndex].timestamp, logs[logIndex].message);
    }
}
