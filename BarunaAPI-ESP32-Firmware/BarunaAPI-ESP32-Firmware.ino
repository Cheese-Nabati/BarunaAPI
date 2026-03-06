// ==========================================
// Firmware V1.4 - Fixing Connection Issue
// ==========================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>

const char* ssid     = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

String baseUrl      = "http://YOUR_SERVER_IP:3000";
String deviceID     = "ESP32 - PROTOTYPE"; //Ganti Sesuai Kebutuhan/Lokasi 
const String deviceToken = "YOUR_DEVICE_TOKEN_HERE";

#define RFID_SDA_PIN 14
#define RFID_RST_PIN 27
MFRC522 rfid(RFID_SDA_PIN, RFID_RST_PIN);
LiquidCrystal_I2C lcd(0x27, 16, 2);

unsigned long lastScrollTime = 0;
unsigned long lastServerCheck = 0;
const long serverCheckInterval = 5000; 
const String brandPrefix = "SMKS 1 Barunawati - SMK Bisa, SMK Hebat, Barunawati JAYA! - ";
String lastServerCustomText = "";
String msgScroll = "  " + brandPrefix + "Silahkan Tap Kartu RFID Anda Di Sini!  ";
int scrollPos = 0;


String currentMode = "READER"; 
String lastDrawnMode = ""; 
bool isDevicePowerOn = true;
bool isServerOnline = true;
bool offlineMessageShown = false;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  lcd.init();
  lcd.backlight();
  lcd.setCursor(0, 0); lcd.print("SMK1BRW-ABSENSI");
  lcd.setCursor(0, 1); lcd.print("Menghubungkan...");

  WiFi.setAutoReconnect(true);
  WiFi.persistent(true);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500); Serial.print(".");
  }

  Serial.println("\n--- SISTEM AKTIF ---");
  checkServerStatus();
  
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Selamat Datang!");
  lcd.setCursor(0, 1); lcd.print("  Menghubungkan  ");
  delay(2000);
  showIdleMessage();
  lastDrawnMode = currentMode;
}

void loop() {
  if (millis() - lastServerCheck > serverCheckInterval) {
    checkServerStatus();
    lastServerCheck = millis();
  }

  if (!isServerOnline) {
    displayOffline();
    lastDrawnMode = "OFFLINE";
    return;
  }

  if (!isDevicePowerOn) {
    if (lastDrawnMode != "SLEEP") {
      lcd.noBacklight();
      lcd.clear();
      lastDrawnMode = "SLEEP";
    }
    delay(1000);
    return;
  } else {
    lcd.backlight();
  }

  if (currentMode == "MAINTENANCE") {
    if (lastDrawnMode != currentMode) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(" SEDANG DALAM  ");
      lcd.setCursor(0, 1); lcd.print(" PERBAIKAN...  ");
      lastDrawnMode = currentMode;
    }
    return;
  }

  if (currentMode == "READER") {
    if (lastDrawnMode != currentMode) {
      showIdleMessage();
      lastDrawnMode = currentMode;
    }
    updateScroll();
  } else if (currentMode == "WRITER") {
    if (lastDrawnMode != currentMode) {
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print("MODE REGISTRASI ");
      lcd.setCursor(0, 1); lcd.print("Tempel Kartu... ");
      lastDrawnMode = currentMode;
    }
  }

  if (!rfid.PICC_IsNewCardPresent()) return;
  if (!rfid.PICC_ReadCardSerial()) return;

  String uidString = "";
  for (byte i = 0; i < rfid.uid.size; i++) {
    uidString += String(rfid.uid.uidByte[i] < 0x10 ? "0" : "");
    uidString += String(rfid.uid.uidByte[i], HEX);
  }
  uidString.toUpperCase();

  if (currentMode == "WRITER") {
    handleRegistration(uidString);
  } else {
    handleAttendance(uidString);
  }

  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();
}

void checkServerStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    HTTPClient http;
    http.begin(baseUrl + "/api/device/ping");
    http.addHeader("X-Device-Token", deviceToken);
    http.addHeader("X-Device-Id", deviceID);
    http.setTimeout(5000);
    
    int httpCode = http.GET();
    if (httpCode == 200) {
      String payload = http.getString();
      DynamicJsonDocument doc(512);
      deserializeJson(doc, payload);

      isServerOnline = true;
      offlineMessageShown = false;
      
      String serverMode = doc["mode"].as<String>();
      int serverPower = doc["power_status"] | 1;
      String serverText = doc["display_text"].as<String>();

      if (serverMode != currentMode) {
        Serial.println("Mode berubah ke: " + serverMode);
        currentMode = serverMode;
      }

      if (serverText != "" && serverText != "null") {
        if (serverText != lastServerCustomText) {
          lastServerCustomText = serverText;
          msgScroll = "  " + brandPrefix + serverText + "  ";
          scrollPos = 0;
          Serial.println("Running Text Updated: " + serverText);
        }
      }

      if (serverPower != (isDevicePowerOn ? 1 : 0)) {
        isDevicePowerOn = (serverPower == 1);
        Serial.println(isDevicePowerOn ? "Device Wake Up" : "Device Sleeping");
      }

    } else {
      isServerOnline = false;
      Serial.println("Server Error: " + String(httpCode));
    }
    http.end();
  } else {
    isServerOnline = false;
    WiFi.reconnect();
  }
}

void displayOffline() {
  if (!offlineMessageShown) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("MOHON MAAF");
    lcd.setCursor(0, 1); lcd.print("SISTEM OFFLINE");
    offlineMessageShown = true;
  }
}

void updateScroll() {
  if (millis() - lastScrollTime > 350) {
    lcd.setCursor(0, 1);
    String displayMsg = msgScroll.substring(scrollPos, scrollPos + 16);
    if (displayMsg.length() < 16) {
      displayMsg += msgScroll.substring(0, 16 - displayMsg.length());
    }
    lcd.print(displayMsg);
    scrollPos++;
    if (scrollPos >= msgScroll.length()) scrollPos = 0;
    lastScrollTime = millis();
  }
}

void handleAttendance(String uid) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("Memproses...");
  
  HTTPClient http;
  http.begin(baseUrl + "/api/absen");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", deviceToken);

  StaticJsonDocument<200> doc;
  doc["rfid_uid"] = uid;
  doc["device_id"] = deviceID;
  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  if (code > 0) {
    String res = http.getString();
    DynamicJsonDocument resDoc(512);
    deserializeJson(resDoc, res);

    lcd.clear();
    if (resDoc["success"] | false) {
      lcd.setCursor(0, 0); lcd.print("Selamat Datang!");
      lcd.setCursor(0, 1); lcd.print(resDoc["name"].as<String>());
    } else {
      String msg = resDoc["message"].as<String>();
      if (msg == "Sudah Absen!") {
        lcd.setCursor(0, 0); lcd.print("  Anda Sudah   ");
        lcd.setCursor(0, 1); lcd.print("Absensi Sblmnya");
      } else {
        lcd.setCursor(0, 0); lcd.print("Kartu");
        lcd.setCursor(0, 1); lcd.print("Tidak Dikenal");
      }
    }
  } else {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Gagal Koneksi");
    lcd.setCursor(0, 1); lcd.print("Error: " + String(code));
  }
  http.end();
  delay(3000);
  showIdleMessage();
}

void handleRegistration(String uid) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("UID Terdeteksi:");
  lcd.setCursor(0, 1); lcd.print(uid);
  
  HTTPClient http;
  http.begin(baseUrl + "/api/device/report-scan");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", deviceToken);

  StaticJsonDocument<200> doc;
  doc["rfid_uid"] = uid;
  doc["device_id"] = deviceID;
  String body;
  serializeJson(doc, body);

  int code = http.POST(body);
  if (code == 200) {
    String res = http.getString();
    DynamicJsonDocument resDoc(512);
    deserializeJson(resDoc, res);

    bool isRegistered = resDoc["is_registered"] | false;
    
    if (isRegistered) {
      String nama = resDoc["student"]["name"].as<String>();
      String kelas = resDoc["student"]["class"].as<String>();
      lcd.clear();
      lcd.setCursor(0, 0); lcd.print(nama.substring(0, 16));
      lcd.setCursor(0, 1); lcd.print(kelas);
    } else {
      lcd.setCursor(0, 1); lcd.print("Cek Dashboard...");
    }
  }
  http.end();
  delay(3000);
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("MODE REGISTRASI ");
  lcd.setCursor(0, 1); lcd.print("Tempel Kartu... ");
}

void showIdleMessage() {
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Selamat Datang!");
  scrollPos = 0;
}
