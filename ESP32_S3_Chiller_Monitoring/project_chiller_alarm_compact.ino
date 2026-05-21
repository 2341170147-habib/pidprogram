// ESP32-S3 Chiller Alarm Monitoring with Firebase
// Based on: https://RandomNerdTutorials.com/esp32-firebase-realtime-database/
// Compact & organized version - No functionality changes

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

// ======================== CONFIG ========================
#define WIFI_SSID "OPPO F9"
#define WIFI_PASSWORD "1234567881"
#define Web_API_KEY "AIzaSyB5RmZyN3fMAU54Yx9HzatqHT1yp8lYeSs"
#define DATABASE_URL "https://percobaan-c496e-default-rtdb.asia-southeast1.firebasedatabase.app/"
#define USER_EMAIL "admin1@gmail.com"
#define USER_PASS "admin1234"
#define DEVICE_ID "esp32-s3-001"

// ======================== PINS ========================
const int SDA_PIN = 5, SCL_PIN = 6;
const uint8_t TFT_CS = 16, TFT_RST = 15, TFT_DC = 7, TFT_MOSI = 8, TFT_SCLK = 18;
const uint8_t ALARM1_PIN = 19, ALARM2_PIN = 21;

// ======================== TIMING ========================
const unsigned long DEBOUNCE_DELAY = 50;
const uint8_t DEBOUNCE_READS = 5;
const unsigned long STATE_CHANGE_LOCKOUT = 500;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000;

// ======================== COLORS ========================
#define COLOR_BLACK 0x0000
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_WHITE 0xFFFF
#define COLOR_YELLOW 0xFFE0
#define COLOR_CYAN 0x07FF

// ======================== STRUCTURES ========================
#define MAX_LOGS 10
struct LogEntry { char timestamp[20]; char message[50]; };

// ======================== FIREBASE ========================
WiFiClientSecure ssl_client;
UserAuth user_auth(Web_API_KEY, USER_EMAIL, USER_PASS);
FirebaseApp app;
using AsyncClient = AsyncClientClass;
AsyncClient aClient(ssl_client);
RealtimeDatabase Database;

// ======================== HARDWARE ========================
Adafruit_ST7735 tft(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);
RTC_DS3231 rtc;

// ======================== GLOBALS ========================
LogEntry logs[MAX_LOGS];
uint8_t log_count = 0;
bool rtc_available = true, wifi_connected = false, initialAlarmsSent = false;
int lastAlarm1State = -1, lastAlarm2State = -1;
unsigned long startup_millis = 0, lastWiFiReconnectAttempt = 0;
unsigned long lastAlarm1ChangeTime = 0, lastAlarm2ChangeTime = 0;

// ======================== DECLARATIONS ========================
void processData(AsyncResult &aResult);
void sendAlarmAlert(uint8_t alarmNum, const char* status);
void sendWiFiStatusAlert(const char* status);
String getTimestamp();
String getDisplayTime();
void updateDisplay();
int debouncePin(uint8_t pin, int lastState);

// ======================== SETUP ========================
void setup() {
    Serial.begin(115200);
    delay(2000);
    Serial.println("\n\n=== ESP32-S3 Chiller Alarm ===\n");
    startup_millis = millis();
    
    // Hardware init
    tft.initR(INITR_144GREENTAB);
    tft.setRotation(0);
    tft.fillScreen(COLOR_BLACK);
    Wire.begin(SDA_PIN, SCL_PIN);
    
    if (!rtc.begin()) {
        rtc_available = false;
        Serial.println("⚠️  RTC not found, using uptime");
    }
    if (rtc.lostPower()) rtc.adjust(DateTime(F(__DATE__), F(__TIME__)));
    
    pinMode(ALARM1_PIN, INPUT_PULLUP);
    pinMode(ALARM2_PIN, INPUT_PULLUP);
    Serial.println("✅ Hardware initialized");
    
    // Progress bar
    for (uint8_t i = 0; i <= 100; i += 10) {
        tft.fillScreen(COLOR_BLACK);
        tft.setTextColor(COLOR_YELLOW);
        tft.setTextSize(1);
        tft.setCursor(20, 30);
        tft.println("INITIALIZING...");
        tft.drawRect(10, 70, 108, 20, COLOR_WHITE);
        tft.fillRect(11, 71, (i * 108) / 100 - 1, 18, COLOR_CYAN);
        tft.setCursor(50, 100);
        tft.printf("%d%%", i);
        delay(200);
    }
    
    // Firebase init
    Serial.println("🔥 Initializing Firebase...");
    ssl_client.setInsecure();
    ssl_client.setConnectionTimeout(1000);
    ssl_client.setHandshakeTimeout(5);
    initializeApp(aClient, app, getAuth(user_auth), processData, "🔐 authTask");
    app.getApp<RealtimeDatabase>(Database);
    Database.url(DATABASE_URL);
    
    // WiFi init
    Serial.println("📡 Connecting WiFi...");
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    
    delay(500);
    lastAlarm1State = digitalRead(ALARM1_PIN);
    lastAlarm2State = digitalRead(ALARM2_PIN);
    
    log_count = 0;
    char buf[40];
    sprintf(buf, "A1: %s", lastAlarm1State == LOW ? "AKTIF" : "NORMAL");
    addLog(buf);
    sprintf(buf, "A2: %s", lastAlarm2State == LOW ? "AKTIF" : "NORMAL");
    addLog(buf);
    
    updateDisplay();
    Serial.println("✅ Ready!\n");
}

// ======================== MAIN LOOP ========================
void loop() {
    app.loop();
    
    // WiFi status check
    int wifiStatus = WiFi.status();
    bool currentWiFiConnected = (wifiStatus == WL_CONNECTED);
    if (currentWiFiConnected != wifi_connected) {
        wifi_connected = currentWiFiConnected;
        if (wifi_connected) {
            Serial.printf("[%s] ✅ WiFi Connected! IP: %s\n", getTimestamp().c_str(), WiFi.localIP().toString().c_str());
            addLog("WiFi: CON");
        } else {
            Serial.printf("[%s] ⚠️  WiFi Disconnected\n", getTimestamp().c_str());
            addLog("WiFi: DIS");
            initialAlarmsSent = false;
        }
        updateDisplay();
    }
    
    if (!wifi_connected && (millis() - lastWiFiReconnectAttempt >= WIFI_RECONNECT_INTERVAL)) {
        Serial.printf("[%s] Reconnecting WiFi...\n", getTimestamp().c_str());
        WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
        lastWiFiReconnectAttempt = millis();
    }
    
    // Send initial state once
    if (!initialAlarmsSent && app.ready() && wifi_connected) {
        initialAlarmsSent = true;
        Serial.println("📡 Sending initial state...");
        const char* a1 = (lastAlarm1State == LOW) ? "AKTIF" : "NORMAL";
        const char* a2 = (lastAlarm2State == LOW) ? "AKTIF" : "NORMAL";
        sendAlarmAlert(1, a1);
        delay(300);
        sendAlarmAlert(2, a2);
        delay(300);
        sendWiFiStatusAlert("CONNECTED");
        Serial.println("✅ Initial state sent\n");
    }
    
    // Check alarms
    if (millis() - lastAlarm1ChangeTime >= STATE_CHANGE_LOCKOUT) {
        int alarm1 = debouncePin(ALARM1_PIN, lastAlarm1State);
        if (alarm1 != -1 && alarm1 != lastAlarm1State) {
            lastAlarm1State = alarm1;
            lastAlarm1ChangeTime = millis();
            const char* status = (alarm1 == LOW) ? "AKTIF" : "NORMAL";
            char buf[40];
            sprintf(buf, "A1: %s", status);
            Serial.printf("[%s] %s\n", getTimestamp().c_str(), buf);
            addLog(buf);
            if (app.ready()) sendAlarmAlert(1, status);
            updateDisplay();
        }
    }
    
    if (millis() - lastAlarm2ChangeTime >= STATE_CHANGE_LOCKOUT) {
        int alarm2 = debouncePin(ALARM2_PIN, lastAlarm2State);
        if (alarm2 != -1 && alarm2 != lastAlarm2State) {
            lastAlarm2State = alarm2;
            lastAlarm2ChangeTime = millis();
            const char* status = (alarm2 == LOW) ? "AKTIF" : "NORMAL";
            char buf[40];
            sprintf(buf, "A2: %s", status);
            Serial.printf("[%s] %s\n", getTimestamp().c_str(), buf);
            addLog(buf);
            if (app.ready()) sendAlarmAlert(2, status);
            updateDisplay();
        }
    }
    
    delay(10);
}

// ======================== DEBOUNCE ========================
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

// ======================== FIREBASE SEND ========================
void sendAlarmAlert(uint8_t alarmNum, const char* status) {
    String ts = getTimestamp();
    String path = "/devices/" DEVICE_ID "/alarm" + String(alarmNum);
    Database.set<int>(aClient, path + "/number", (int)alarmNum, processData, "RTDB_Alarm_Num");
    Database.set<String>(aClient, path + "/status", status, processData, "RTDB_Alarm_Status");
    Database.set<String>(aClient, path + "/timestamp", ts, processData, "RTDB_Alarm_TS");
    Database.set<String>(aClient, path + "/type", "ALARM_CHANGE", processData, "RTDB_Alarm_Type");
    Serial.printf("[%s] 📤 Alarm %d: %s\n", ts.c_str(), alarmNum, status);
}

void sendWiFiStatusAlert(const char* status) {
    String ts = getTimestamp();
    String path = "/devices/" DEVICE_ID "/wifi_status";
    Database.set<String>(aClient, path + "/wifi_status", status, processData, "RTDB_WiFi_Status");
    Database.set<String>(aClient, path + "/timestamp", ts, processData, "RTDB_WiFi_TS");
    Database.set<String>(aClient, path + "/type", "WIFI_STATUS", processData, "RTDB_WiFi_Type");
    Serial.printf("[%s] 📤 WiFi: %s\n", ts.c_str(), status);
}

void processData(AsyncResult &aResult) {
    if (!aResult.isResult()) return;
    if (aResult.isError()) 
        Firebase.printf("❌ Error: %s (code: %d)\n", aResult.error().message().c_str(), aResult.error().code());
}

// ======================== TIMESTAMP ========================
String getTimestamp() {
    if (rtc_available) {
        DateTime now = rtc.now();
        char buf[25];
        sprintf(buf, "%04d-%02d-%02d %02d:%02d:%02d", now.year(), now.month(), now.day(), now.hour(), now.minute(), now.second());
        return String(buf);
    }
    unsigned long uptime = millis() - startup_millis;
    char buf[25];
    sprintf(buf, "2026-01-01 %02lu:%02lu:%02lu", (uptime / 3600000) % 24, (uptime / 60000) % 60, (uptime / 1000) % 60);
    return String(buf);
}

String getDisplayTime() {
    if (rtc_available) {
        DateTime now = rtc.now();
        char buf[20];
        sprintf(buf, "%02d:%02d:%02d", now.hour(), now.minute(), now.second());
        return String(buf);
    }
    unsigned long uptime = millis() - startup_millis;
    char buf[20];
    sprintf(buf, "%02lu:%02lu:%02lu", (uptime / 3600000) % 24, (uptime / 60000) % 60, (uptime / 1000) % 60);
    return String(buf);
}

void addLog(const char* message) {
    if (log_count < MAX_LOGS) {
        strcpy(logs[log_count].timestamp, getDisplayTime().c_str());
        strcpy(logs[log_count].message, message);
        log_count++;
    } else {
        for (uint8_t i = 0; i < MAX_LOGS - 1; i++) {
            strcpy(logs[i].timestamp, logs[i + 1].timestamp);
            strcpy(logs[i].message, logs[i + 1].message);
        }
        strcpy(logs[MAX_LOGS - 1].timestamp, getDisplayTime().c_str());
        strcpy(logs[MAX_LOGS - 1].message, message);
    }
}

// ======================== DISPLAY ========================
void updateDisplay() {
    tft.fillScreen(COLOR_BLACK);
    tft.setTextColor(COLOR_WHITE);
    tft.setTextSize(1);
    tft.setCursor(5, 5);
    tft.println("STATUS:");
    
    int a1Color = (lastAlarm1State == LOW) ? COLOR_RED : COLOR_GREEN;
    tft.setTextColor(a1Color);
    tft.setCursor(5, 18);
    tft.printf("A1: %s", (lastAlarm1State == LOW) ? "AKTIF" : "NORMAL");
    
    int a2Color = (lastAlarm2State == LOW) ? COLOR_RED : COLOR_GREEN;
    tft.setTextColor(a2Color);
    tft.setCursor(5, 30);
    tft.printf("A2: %s", (lastAlarm2State == LOW) ? "AKTIF" : "NORMAL");
    
    int wColor = wifi_connected ? COLOR_GREEN : COLOR_RED;
    tft.setTextColor(wColor);
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
    
    tft.setTextColor(COLOR_WHITE);
    uint8_t cnt = (log_count < 4) ? log_count : 4;
    for (uint8_t i = 0; i < cnt; i++) {
        uint8_t idx = log_count - cnt + i;
        tft.setCursor(5, 77 + (i * 10));
        tft.printf("%s %s", logs[idx].timestamp, logs[idx].message);
    }
}
