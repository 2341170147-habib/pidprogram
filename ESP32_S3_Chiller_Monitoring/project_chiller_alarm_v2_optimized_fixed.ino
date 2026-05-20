#include <Wire.h>
#include "RTClib.h"
#include <Adafruit_ST7735.h>
#include <Adafruit_GFX.h>
#include <SPI.h>
#include <WiFi.h>

// ============================================
// CONFIGURATION
// ============================================

// RTC
RTC_DS3231 rtc;
bool rtc_available = true;
unsigned long startup_millis = 0;

// I2C pins (ESP32-S3)
const int SDA_PIN = 5;
const int SCL_PIN = 6;

// TFT pins
const uint8_t TFT_CS = 16, TFT_RST = 15, TFT_DC = 7;
const uint8_t TFT_MOSI = 8, TFT_SCLK = 18;

// Alarm pins
const uint8_t ALARM1_PIN = 19;
const uint8_t ALARM2_PIN = 21;

// WiFi
const char* WIFI_SSID = "OPPO F9";
const char* WIFI_PASSWORD = "1234567881";
IPAddress staticIP(192, 168, 43, 50);
IPAddress gateway(192, 168, 43, 1);
IPAddress subnet(255, 255, 255, 0);

// Debounce
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
// GLOBAL VARIABLES
// ============================================

Adafruit_ST7735 tft = Adafruit_ST7735(TFT_CS, TFT_DC, TFT_MOSI, TFT_SCLK, TFT_RST);

#define COLOR_BLACK 0x0000
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_WHITE 0xFFFF
#define COLOR_YELLOW 0xFFE0
#define COLOR_CYAN 0x07FF

LogEntry logs[MAX_LOGS];
uint8_t log_count = 0;

bool wifi_connected = false;
unsigned long lastWiFiReconnectAttempt = 0;
const unsigned long WIFI_RECONNECT_INTERVAL = 10000;

int lastAlarm1State = -1;
int lastAlarm2State = -1;
unsigned long lastAlarm1ChangeTime = 0;
unsigned long lastAlarm2ChangeTime = 0;

// ============================================
// SETUP & LOOP
// ============================================

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\n=== ESP32-S3 Chiller Monitoring Started ===\n");
  startup_millis = millis();
  
  initTFT();
  for (uint8_t i = 0; i <= 100; i += 10) {
    displayProgressBar(i);
    delay(200);
  }
  
  initRTC();
  initAlarmPins();
  initWiFi();
  
  delay(500);
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

void loop() {
  updateWiFiStatus();
  
  if (millis() - lastAlarm1ChangeTime >= STATE_CHANGE_LOCKOUT) {
    int alarm1 = debouncePin(ALARM1_PIN, lastAlarm1State);
    if (alarm1 != -1 && alarm1 != lastAlarm1State) {
      handleAlarmChange(1, alarm1);
      lastAlarm1State = alarm1;
      lastAlarm1ChangeTime = millis();
      updateDisplay();
    }
  }
  
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
// INITIALIZATION
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
    Serial.printf("[%s] Connecting to WiFi: \"%s\"\n", getTimestamp().c_str(), WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    lastWiFiReconnectAttempt = millis();
  }
}

void handleWiFiConnect() {
  Serial.printf("[%s] ✅ WiFi Connected!\n", getTimestamp().c_str());
  Serial.printf("    IP Address: %s\n", WiFi.localIP().toString().c_str());
  Serial.printf("    Signal Strength: %d dBm\n", WiFi.RSSI());
  addLog("WiFi: CON");
}

void handleWiFiDisconnect() {
  Serial.printf("[%s] ⚠️  WiFi Disconnected!\n", getTimestamp().c_str());
  Serial.printf("[%s] Alarm detection continues offline...\n", getTimestamp().c_str());
  addLog("WiFi: DIS");
}

// ============================================
// DEBOUNCING & EVENTS
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
  Serial.printf("[%s] %s\n", getTimestamp().c_str(), msg);
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
// DISPLAY
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
  tft.printf("Alarm 1: %s", (lastAlarm1State == LOW) ? "AKTIF" : "NORMAL");
  
  int alarm2Color = (lastAlarm2State == LOW) ? COLOR_RED : COLOR_GREEN;
  tft.setTextColor(alarm2Color);
  tft.setCursor(5, 30);
  tft.printf("Alarm 2: %s", (lastAlarm2State == LOW) ? "AKTIF" : "NORMAL");
  
  int wifiColor = wifi_connected ? COLOR_GREEN : COLOR_RED;
  tft.setTextColor(wifiColor);
  tft.setCursor(5, 42);
  tft.printf("WiFi: %s", wifi_connected ? "CON" : "DIS");
  
  tft.drawLine(0, 52, 128, 52, COLOR_WHITE);
  
  tft.setTextColor(COLOR_YELLOW);
  tft.setCursor(5, 57);
  tft.println("LOGS:");
}

void displayLogs() {
  tft.setTextColor(COLOR_WHITE);
  tft.setTextSize(1);
  
  uint8_t displayCount = (log_count < 5) ? log_count : 5;
  uint8_t startY = 67;
  uint8_t lineHeight = 10;
  
  // Tampilkan dari log tertua ke terbaru (newest di bawah)
  for (uint8_t i = 0; i < displayCount; i++) {
    uint8_t logIndex = log_count - displayCount + i;
    
    tft.setCursor(5, startY + (i * lineHeight));
    tft.printf("%s %s", logs[logIndex].timestamp, logs[logIndex].message);
  }
}
