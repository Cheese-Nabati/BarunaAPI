// ==========================================
// Firmware V1.5 - Offline Buffer! (Insecure HTTPS Version)
// ==========================================

#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h> 
#include <SPI.h>
#include <MFRC522.h>
#include <LiquidCrystal_I2C.h>
#include <ArduinoJson.h>
#include <LittleFS.h>

const char* ssid     = "NabatiKeju-IoT";
const char* password = "NabatiKejuIOTProject";

String baseUrl      = "https://barunapidemo.up.railway.app";
String deviceID     = "ESP32 - PROTOTYPE"; //Ganti Sesuai Kebutuhan/Lokasi 
const String deviceToken = "DEMO_SECURE_TOKEN_2026";

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
bool wasOffline = false;

void setup() {
  Serial.begin(115200);
  SPI.begin();
  rfid.PCD_Init();

  if(!LittleFS.begin(true)){
    Serial.println("An Error has occurred while mounting LittleFS");
  }

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
  sendLog("SYSTEM_BOOT");
  checkServerStatus();
  sendLog("WIFI_CONNECTED - IP: " + WiFi.localIP().toString());
  
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

void saveToBuffer(String uid) {
  File file = LittleFS.open("/buffer.txt", FILE_APPEND);
  if(!file) {
    Serial.println("Gagal membuka file buffer!");
    return;
  }
  file.println(uid);
  file.close();
  Serial.println("Data disimpan ke buffer: " + uid);
}

void syncBuffer() {
  if (!LittleFS.exists("/buffer.txt")) return;

  File file = LittleFS.open("/buffer.txt", FILE_READ);
  if (!file) return;

  Serial.println("--- Sinkronisasi Buffer Dimulai ---");
  sendLog("SYNC_STARTED");
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("SINKRON DATA...");
  
  String unsyncedUids = "";
  int count = 0;

  while (file.available()) {
    String uid = file.readStringUntil('\n');
    uid.trim();
    if (uid.length() > 0) {
      WiFiClientSecure client;
      client.setInsecure();
      HTTPClient http;
      http.begin(client, baseUrl + "/api/absen");
      http.addHeader("Content-Type", "application/json");
      http.addHeader("X-Device-Token", deviceToken);

      StaticJsonDocument<200> doc;
      doc["rfid_uid"] = uid;
      doc["device_id"] = deviceID;
      String body;
      serializeJson(doc, body);

      int code = http.POST(body);
      if (code == 200 || code == 400) {
        count++;
      } else {
        unsyncedUids += uid + "\n";
      }
      http.end();
      delay(100);
    }
  }
  file.close();

  if (unsyncedUids.length() > 0) {
    File rewriteFile = LittleFS.open("/buffer.txt", FILE_WRITE);
    rewriteFile.print(unsyncedUids);
    rewriteFile.close();
  } else {
    LittleFS.remove("/buffer.txt");
  }

  Serial.println("Sinkronisasi Selesai. " + String(count) + " terkirim.");
  sendLog("SYNC_COMPLETED - Data: " + String(count));
  lcd.setCursor(0, 1); lcd.print(String(count) + " Data Terkirim");
  delay(2000);
  showIdleMessage();
}

void sendLog(String activity) {
  if (WiFi.status() != WL_CONNECTED) return;
  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, baseUrl + "/api/device/log");
  http.addHeader("Content-Type", "application/json");
  http.addHeader("X-Device-Token", deviceToken);
  StaticJsonDocument<200> doc;
  doc["device_id"] = deviceID;
  doc["activity"] = activity;
  String body;
  serializeJson(doc, body);
  http.POST(body);
  http.end();
}

void checkServerStatus() {
  if (WiFi.status() == WL_CONNECTED) {
    WiFiClientSecure client;
    client.setInsecure();
    HTTPClient http;
    http.begin(client, baseUrl + "/api/device/ping");
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
      
      if (wasOffline) {
        syncBuffer();
        wasOffline = false;
      }

      String serverMode = doc["mode"].as<String>();
      int serverPower = doc["power_status"] | 1;
      String serverText = doc["display_text"].as<String>();

      if (serverMode != currentMode) {
        currentMode = serverMode;
      }

      if (serverText != "" && serverText != "null") {
        if (serverText != lastServerCustomText) {
          lastServerCustomText = serverText;
          msgScroll = "  " + brandPrefix + serverText + "  ";
          scrollPos = 0;
        }
      } else if (lastServerCustomText != "") {
          // Reset to default if server clears custom text
          lastServerCustomText = "";
          msgScroll = "  " + brandPrefix + "Silahkan Tap Kartu RFID Anda Di Sini!  ";
          scrollPos = 0;
      }

      if (serverPower != (isDevicePowerOn ? 1 : 0)) {
        isDevicePowerOn = (serverPower == 1);
      }

    } else {
      isServerOnline = false;
      wasOffline = true;
      displayOffline();
      Serial.println("Server Offline (HTTP: " + String(httpCode) + ")");
    }
    http.end();
  } else {
    isServerOnline = false;
    wasOffline = true;
    displayOffline();
    WiFi.reconnect();
  }
}

void displayOffline() {
  if (!offlineMessageShown) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Selamat Datang!");
    // Kembalikan running text ke default saat offline
    msgScroll = "  " + brandPrefix + "  ";
    scrollPos = 0;
    offlineMessageShown = true;
  }
}

void updateScroll() {
  // Running text tetap berjalan baik online maupun offline selama dalam mode READER
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
  
  
  if (!isServerOnline) {
    saveToBuffer(uid);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("OFFLINE: Tersimpan");
    lcd.setCursor(0, 1); lcd.print(uid);
    delay(2000);
    showIdleMessage();
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, baseUrl + "/api/absen");
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
    saveToBuffer(uid);
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("Gagal Koneksi");
    lcd.setCursor(0, 1); lcd.print("Buffer: Disimpan");
  }
  http.end();
  delay(3000);
  showIdleMessage();
}

void handleRegistration(String uid) {
  lcd.clear();
  lcd.setCursor(0, 0); lcd.print("UID Terdeteksi:");
  lcd.setCursor(0, 1); lcd.print(uid);
  
  if (!isServerOnline) {
    lcd.clear();
    lcd.setCursor(0, 0); lcd.print("REGISTRASI GAGAL");
    lcd.setCursor(0, 1); lcd.print("Server Offline");
    delay(3000);
    return;
  }

  WiFiClientSecure client;
  client.setInsecure();
  HTTPClient http;
  http.begin(client, baseUrl + "/api/device/report-scan");
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
