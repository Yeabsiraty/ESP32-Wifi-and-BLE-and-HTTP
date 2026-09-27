#include <WiFi.h>
#include <HTTPClient.h>
#include <Preferences.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>

#define SERVICE_UUID "12345678-1234-1234-1234-123456789001"
#define SSID_UUID "12345678-1234-1234-1234-123456789002"
#define PASSWORD_UUID "12345678-1234-1234-1234-123456789003"
#define CONNECT_UUID "12345678-1234-1234-1234-123456789004"
#define STATUS_UUID "12345678-1234-1234-1234-123456789005"

const char* BLE_NAME = "ESP32 Provisioning";

const uint8_t LED_PIN = 2;
const uint8_t BUTTON_PIN = 32;

const uint8_t CREATE_TEAM_BUTTON_PIN = 33;
const uint8_t YELLOW_LED_PIN = 25;
const uint8_t GREEN_LED_PIN = 26;
const uint8_t RED_LED_PIN = 27;

const char* TEAM_API_URL = ""; 
const char* API_BEARER_TOKEN = ""; 
 
const unsigned long WIFI_TIMEOUT = 15000; 
const unsigned long WIFI_RETRY_INTERVAL = 5000; 
const unsigned long WIFI_CHECK_INTERVAL = 250; 
const unsigned long BUTTON_DEBOUNCE = 50; 
const unsigned long TEAM_BUTTON_DEBOUNCE = 200; 
const unsigned long TEAM_HTTP_TIMEOUT = 10000; 
const unsigned long RESULT_LED_DURATION = 30000; 
 
String wifiSSID; 
String wifiPassword; 
String newWifiSSID; 
String newWifiPassword; 
 
Preferences preferences; 
 
BLEServer* bleServer = nullptr; 
BLECharacteristic* statusCharacteristic = nullptr; 
 
bool bleProvisioningStarted = false; 
bool bleClientConnected = false; 
bool bleShutdownRequested = false; 
bool bleDisconnectRequested = false; 
bool wifiConnected = false; 
bool wifiConnecting = false; 
bool testingNewWiFi = false; 
bool teamRequestRunning = false; 
 
unsigned long wifiStartTime = 0; 
unsigned long lastWifiRetry = 0; 
unsigned long lastWifiCheck = 0; 
unsigned long lastButtonTime = 0; 
unsigned long lastTeamButtonTime = 0; 
unsigned long resultLedStartTime = 0; 
 
bool resultLedActive = false; 
String lastStatus = ""; 
 
enum WifiConnectionSource { 
  WIFI_SOURCE_NONE, 
  WIFI_SOURCE_SAVED, 
  WIFI_SOURCE_BLE 
}; 
 
WifiConnectionSource connectionSource = WIFI_SOURCE_NONE; 
 
void startBLEProvisioning(); 
void requestBLEShutdown(); 
void handleBLEShutdown(); 
void stopBLEProvisioning(); 
void startWiFiConnection(WifiConnectionSource source); 
void handleWiFiConnection(); 
void handleButton(); 
void handleCreateTeamButton(); 
void sendStatus(const String& message); 
void saveWiFiCredentials(); 
void loadWiFiCredentials(); 
void createTeam(); 
void startResultLed(uint8_t ledPin); 
void handleResultLed(); 
void stopResultLed(); 
 
void sendStatus(const String& message) { 
  Serial.print("[STATUS] "); 
  Serial.println(message); 
 
  if (message == lastStatus) { 
    return; 
  } 
 
  lastStatus = message; 
 
  if (bleClientConnected && statusCharacteristic != nullptr) { 
    statusCharacteristic->setValue(message.c_str()); 
    statusCharacteristic->notify(); 
  } 
} 
 
void saveWiFiCredentials() { 
  preferences.begin("wifi", false); 
  preferences.putString("ssid", wifiSSID); 
  preferences.putString("password", wifiPassword); 
  preferences.end(); 
 
  Serial.println("[NVS] WiFi credentials saved."); 
} 
 
void loadWiFiCredentials() { 
  preferences.begin("wifi", true); 
  wifiSSID = preferences.getString("ssid", ""); 
  wifiPassword = preferences.getString("password", ""); 
  preferences.end(); 
 
  if (wifiSSID.length() > 0) { 
    Serial.print("[NVS] Saved SSID: "); 
    Serial.println(wifiSSID); 
  } else { 
    Serial.println("[NVS] No saved WiFi."); 
  } 
} 
 
void startWiFiConnection(WifiConnectionSource source) { 
  connectionSource = source; 
  wifiConnecting = true; 
  wifiConnected = false; 
  wifiStartTime = millis(); 
 
  if (source == WIFI_SOURCE_BLE) { 
    testingNewWiFi = true; 
 
    Serial.println(); 
    Serial.println("--------------------------------"); 
    Serial.println("Testing NEW WiFi"); 
    Serial.println("--------------------------------"); 
    Serial.print("SSID: "); 
    Serial.println(newWifiSSID); 
 
    sendStatus("Testing new WiFi..."); 
 
    WiFi.begin(newWifiSSID.c_str(), newWifiPassword.c_str()); 
    return; 
  } 
 
  if (source == WIFI_SOURCE_SAVED) { 
    testingNewWiFi = false; 
 
    Serial.println(); 
    Serial.println("--------------------------------"); 
    Serial.println("Connecting to SAVED WiFi"); 
    Serial.println("--------------------------------"); 
    Serial.print("SSID: "); 
    Serial.println(wifiSSID); 
 
    sendStatus("Connecting to saved WiFi..."); 
 
    WiFi.begin(wifiSSID.c_str(), wifiPassword.c_str()); 
    return; 
  } 
} 
 
void handleWiFiConnection() { 
  const unsigned long now = millis(); 
 
  if (now - lastWifiCheck < WIFI_CHECK_INTERVAL) { 
    return; 
  } 
 
  lastWifiCheck = now; 
 
  if (WiFi.status() == WL_CONNECTED) { 
    digitalWrite(LED_PIN, HIGH); 
 
    if (!wifiConnected) { 
      wifiConnected = true; 
      wifiConnecting = false; 
 
      Serial.println(); 
      Serial.println("================================"); 
      Serial.println("WIFI CONNECTED"); 
      Serial.println("================================"); 
      Serial.print("SSID: "); 
      Serial.println(WiFi.SSID()); 
      Serial.print("IP: "); 
      Serial.println(WiFi.localIP()); 
 
      if (testingNewWiFi) { 
        Serial.println("[WiFi] New WiFi SUCCESS."); 
 
        wifiSSID = newWifiSSID; 
        wifiPassword = newWifiPassword; 
 
        saveWiFiCredentials(); 
 
        newWifiSSID = ""; 
        newWifiPassword = ""; 
        testingNewWiFi = false; 
 
        sendStatus("New WiFi connected"); 
        requestBLEShutdown(); 
      } else { 
        sendStatus("WiFi connected"); 
      } 
    } 
 
    return; 
  } 
 
  wifiConnected = false; 
  digitalWrite(LED_PIN, LOW); 
 
  if (wifiConnecting) { 
    if (now - wifiStartTime >= WIFI_TIMEOUT) { 
      wifiConnecting = false; 
 
      Serial.println(); 
      Serial.println("[WiFi] Connection timeout."); 
 
      WiFi.disconnect(false); 
 
      if (testingNewWiFi) { 
        Serial.println("[WiFi] New WiFi FAILED."); 
 
        newWifiSSID = ""; 
        newWifiPassword = ""; 
        testingNewWiFi = false; 
 
        sendStatus("New WiFi failed"); 
        requestBLEShutdown(); 
 
        if (wifiSSID.length() > 0 && wifiPassword.length() > 0) { 
          Serial.println("[WiFi] Returning to saved WiFi."); 
 
          startWiFiConnection(WIFI_SOURCE_SAVED); 
        } else { 
          Serial.println("[WiFi] No saved WiFi available."); 
        } 
 
        return; 
      } 
 
      Serial.println("[WiFi] Saved WiFi unavailable."); 
      sendStatus("Saved WiFi unavailable"); 
      return; 
    } 
  } 
 
  if (!wifiConnecting) { 
    if (now - lastWifiRetry >= WIFI_RETRY_INTERVAL) { 
      lastWifiRetry = now; 
 
      if (wifiSSID.length() > 0 && wifiPassword.length() > 0) { 
        startWiFiConnection(WIFI_SOURCE_SAVED); 
      } 
    } 
  } 
} 
 
class MyServerCallbacks : public BLEServerCallbacks { 
  void onConnect(BLEServer* pServer) override { 
    bleClientConnected = true; 
 
    Serial.println(); 
    Serial.println("[BLE] Client connected."); 
 
    sendStatus("BLE connected"); 
  } 
 
  void onDisconnect(BLEServer* pServer) override { 
    bleClientConnected = false; 
 
    Serial.println(); 
    Serial.println("[BLE] Client disconnected."); 
 
    if (bleProvisioningStarted && !bleShutdownRequested) { 
      pServer->startAdvertising(); 
      Serial.println("[BLE] Advertising restarted."); 
    } 
  } 
}; 
 
class SSIDCallback : public BLECharacteristicCallbacks { 
  void onWrite(BLECharacteristic* characteristic) override { 
    String value = String(characteristic->getValue().c_str()); 
 
    if (value.length() == 0) { 
      return; 
    } 
 
    newWifiSSID = value; 
 
    Serial.println(); 
    Serial.print("[BLE] New SSID: "); 
    Serial.println(newWifiSSID); 
 
    sendStatus("SSID received"); 
  } 
}; 
 
class PasswordCallback : public BLECharacteristicCallbacks { 
  void onWrite(BLECharacteristic* characteristic) override { 
    String value = String(characteristic->getValue().c_str()); 
 
    newWifiPassword = value; 
 
    Serial.println(); 
    Serial.println("[BLE] New WiFi password received."); 
 
    sendStatus("Password received"); 
  } 
}; 
 
class ConnectCallback : public BLECharacteristicCallbacks { 
  void onWrite(BLECharacteristic* characteristic) override { 
    String value = String(characteristic->getValue().c_str()); 
 
    value.trim(); 
 
    Serial.print("[BLE] CONNECT command: "); 
    Serial.println(value); 
 
    if (value != "connect" && value != "CONNECT" && value != "1") { 
      return; 
    } 
 
    if (newWifiSSID.length() == 0) { 
      sendStatus("SSID required"); 
      return; 
    } 
 
    Serial.println(); 
    Serial.println("[BLE] Starting new WiFi test."); 
 
    WiFi.disconnect(false); 
 
    wifiConnecting = false; 
    wifiConnected = false; 
 
    startWiFiConnection(WIFI_SOURCE_BLE); 
  } 
}; 
 
void startBLEProvisioning() { 
  if (bleProvisioningStarted) { 
    Serial.println("[BLE] Provisioning already active."); 
    return; 
  } 
 
  Serial.println(); 
  Serial.println("================================"); 
  Serial.println("START BLE PROVISIONING"); 
  Serial.println("================================"); 
 
  bleShutdownRequested = false; 
  bleDisconnectRequested = false; 
 
  newWifiSSID = ""; 
  newWifiPassword = ""; 
 
  BLEDevice::init(BLE_NAME); 
 
  bleServer = BLEDevice::createServer(); 
 
  bleServer->setCallbacks(new MyServerCallbacks()); 
 
  BLEService* service = bleServer->createService(SERVICE_UUID); 
 
  BLECharacteristic* ssidCharacteristic = 
    service->createCharacteristic( 
      SSID_UUID, 
      BLECharacteristic::PROPERTY_WRITE 
    ); 
 
  ssidCharacteristic->setCallbacks(new SSIDCallback()); 
 
  BLECharacteristic* passwordCharacteristic = 
    service->createCharacteristic( 
      PASSWORD_UUID, 
      BLECharacteristic::PROPERTY_WRITE 
    ); 
 
  passwordCharacteristic->setCallbacks(new PasswordCallback()); 
 
  BLECharacteristic* connectCharacteristic = 
    service->createCharacteristic( 
      CONNECT_UUID, 
      BLECharacteristic::PROPERTY_WRITE 
    ); 
 
  connectCharacteristic->setCallbacks(new ConnectCallback()); 
 
  statusCharacteristic = 
    service->createCharacteristic( 
      STATUS_UUID, 
      BLECharacteristic::PROPERTY_READ | 
      BLECharacteristic::PROPERTY_NOTIFY 
    ); 
 
  statusCharacteristic->addDescriptor(new BLE2902()); 
 
  service->start(); 
 
  BLEAdvertising* advertising = BLEDevice::getAdvertising(); 
 
  advertising->addServiceUUID(SERVICE_UUID); 
  advertising->setScanResponse(true); 
  advertising->start(); 
 
  bleProvisioningStarted = true; 
 
  Serial.println("[BLE] Provisioning started."); 
 
  sendStatus("BLE ready"); 
} 
 
void requestBLEShutdown() { 
  if (!bleProvisioningStarted) { 
    return; 
  } 
 
  bleShutdownRequested = true; 
 
  Serial.println("[BLE] Shutdown requested."); 
} 
 
void handleBLEShutdown() { 
  if (!bleShutdownRequested) { 
    return; 
  } 
 
  BLEAdvertising* advertising = BLEDevice::getAdvertising(); 
 
  if (advertising != nullptr) { 
    advertising->stop(); 
 
    Serial.println("[BLE] Advertising stopped."); 
  } 
 
  if (bleClientConnected && bleServer != nullptr) { 
    if (!bleDisconnectRequested) { 
      bleDisconnectRequested = true; 
 
      Serial.println("[BLE] Disconnecting client."); 
 
      bleServer->disconnect(bleServer->getConnId()); 
      return; 
    } 
 
    digitalWrite(LED_PIN, LOW); 
    return; 
  } 
 
  stopBLEProvisioning(); 
} 
 
void stopBLEProvisioning() { 
  if (!bleProvisioningStarted) { 
    return; 
  } 
 
  if (bleClientConnected) { 
    return; 
  } 
 
  Serial.println(); 
  Serial.println("================================"); 
  Serial.println("STOP BLE PROVISIONING"); 
  Serial.println("================================"); 
 
  BLEAdvertising* advertising = BLEDevice::getAdvertising(); 
 
  if (advertising != nullptr) { 
    advertising->stop(); 
  } 
 
  bleProvisioningStarted = false; 
  bleShutdownRequested = false; 
  bleDisconnectRequested = false; 
 
  BLEDevice::deinit(false); 
 
  bleServer = nullptr; 
  statusCharacteristic = nullptr; 
  lastStatus = ""; 
 
  Serial.println("[BLE] BLE completely stopped."); 
} 
 
void startResultLed(uint8_t ledPin) { 
  digitalWrite(YELLOW_LED_PIN, LOW); 
  digitalWrite(GREEN_LED_PIN, LOW); 
  digitalWrite(RED_LED_PIN, LOW); 
  digitalWrite(ledPin, HIGH); 
 
  resultLedStartTime = millis(); 
  resultLedActive = true; 
} 
 
void stopResultLed() { 
  digitalWrite(YELLOW_LED_PIN, LOW); 
  digitalWrite(GREEN_LED_PIN, LOW); 
  digitalWrite(RED_LED_PIN, LOW); 
 
  resultLedActive = false; 
} 
 
void handleResultLed() { 
  if (!resultLedActive) { 
    return; 
  } 
 
  if (millis() - resultLedStartTime >= RESULT_LED_DURATION) { 
    stopResultLed(); 
 
    Serial.println("[TEAM] Result LED finished."); 
  } 
} 
 
void createTeam() { 
  if (teamRequestRunning) { 
    return; 
  } 
 
  if (resultLedActive) { 
    Serial.println("[TEAM] Result LED is still active."); 
    return; 
  } 
 
  if (WiFi.status() != WL_CONNECTED) { 
    Serial.println("[TEAM] WiFi is not connected."); 
 
    startResultLed(RED_LED_PIN); 
    return; 
  } 
 
  if ( 
    API_BEARER_TOKEN == nullptr || 
    strlen(API_BEARER_TOKEN) == 0 || 
    String(API_BEARER_TOKEN) == "Session required!" 
  ) { 
    Serial.println("[TEAM] API token is not configured."); 
 
    startResultLed(RED_LED_PIN); 
    return; 
  } 
 
  teamRequestRunning = true; 
 
  digitalWrite(YELLOW_LED_PIN, HIGH); 
  digitalWrite(GREEN_LED_PIN, LOW); 
  digitalWrite(RED_LED_PIN, LOW); 
 
  Serial.println(); 
  Serial.println("================================"); 
  Serial.println("CREATE TEAM"); 
  Serial.println("================================"); 
 
  Serial.println("[TEAM] WiFi is connected."); 
  Serial.print("[TEAM] IP: "); 
  Serial.println(WiFi.localIP()); 
 
  sendStatus("Creating team..."); 
 
  String boundary = "----ESP32TeamBoundary"; 
  String body; 
 
  body.reserve(1200); 
 
  body += "--"; 
  body += boundary; 
  body += "\r\n"; 
  body += "Content-Disposition: form-data; name=\"name\"\r\n\r\n"; 
  body += "ESP 32\r\n"; 
 
  body += "--"; 
  body += boundary; 
  body += "\r\n"; 
  body += "Content-Disposition: form-data; name=\"sport\"\r\n\r\n"; 
  body += "football\r\n"; 
 
  body += "--"; 
  body += boundary; 
  body += "\r\n"; 
  body += "Content-Disposition: form-data; name=\"area\"\r\n\r\n"; 
  body += "Lancha\r\n"; 
 
  body += "--"; 
  body += boundary; 
  body += "\r\n"; 
  body += "Content-Disposition: form-data; name=\"latitude\"\r\n\r\n"; 
  body += "8.988791955996284\r\n"; 
 
  body += "--"; 
  body += boundary; 
  body += "\r\n"; 
  body += "Content-Disposition: form-data; name=\"longitude\"\r\n\r\n"; 
  body += "38.75776296597615\r\n"; 
 
  body += "--"; 
  body += boundary; 
  body += "\r\n"; 
  body += "Content-Disposition: form-data; name=\"skill_level\"\r\n\r\n"; 
  body += "beginner\r\n"; 
 
  body += "--"; 
  body += boundary; 
  body += "\r\n"; 
  body += "Content-Disposition: form-data; name=\"preferred_days\"\r\n\r\n"; 
  body += "[\"wed\"]\r\n"; 
 
  body += "--"; 
  body += boundary; 
  body += "\r\n"; 
  body += "Content-Disposition: form-data; name=\"age_category\"\r\n\r\n"; 
  body += "u18\r\n"; 
 
  body += "--"; 
  body += boundary; 
  body += "\r\n"; 
  body += "Content-Disposition: form-data; name=\"max_roster_size\"\r\n\r\n"; 
  body += "20\r\n"; 
 
  body += "--"; 
  body += boundary; 
  body += "\r\n"; 
  body += "Content-Disposition: form-data; name=\"visibility\"\r\n\r\n"; 
  body += "public\r\n"; 
 
  body += "--"; 
  body += boundary; 
  body += "--\r\n"; 
 
  WiFiClient client; 
  HTTPClient http; 
 
  http.setTimeout(TEAM_HTTP_TIMEOUT); 
 
  bool beginResult = http.begin(client, TEAM_API_URL); 
 
  if (!beginResult) { 
    Serial.println("[TEAM] HTTP begin failed."); 
 
    teamRequestRunning = false; 
 
    stopResultLed(); 
    startResultLed(RED_LED_PIN); 
 
    sendStatus("Team request failed"); 
    return; 
  } 
 
  String contentType = "multipart/form-data; boundary=" + boundary; 
 
  http.addHeader( 
    "Authorization", 
    String("Bearer ") + API_BEARER_TOKEN 
  ); 
 
  http.addHeader("Accept", "application/json"); 
  http.addHeader("Content-Type", contentType); 
 
  Serial.println("[TEAM] Sending POST request..."); 
 
  int httpCode = http.POST(body); 
 
  Serial.print("[TEAM] HTTP status: "); 
  Serial.println(httpCode); 
 
  String responseBody = http.getString(); 
 
  if (responseBody.length() > 0) { 
    Serial.println("[TEAM] Response:"); 
    Serial.println(responseBody); 
  } 
 
  http.end(); 
 
  teamRequestRunning = false; 
 
  digitalWrite(YELLOW_LED_PIN, LOW); 
 
  if (httpCode == 201) { 
    Serial.println(); 
    Serial.println("[TEAM] TEAM CREATED SUCCESSFULLY."); 
 
    startResultLed(GREEN_LED_PIN); 
    sendStatus("Team created"); 
  } else { 
    Serial.println(); 
    Serial.println("[TEAM] TEAM CREATION FAILED."); 
    Serial.print("[TEAM] HTTP error/status: "); 
    Serial.println(httpCode); 
 
    startResultLed(RED_LED_PIN); 
    sendStatus("Team creation failed"); 
  } 
} 
 
void handleButton() { 
  const unsigned long now = millis(); 
 
  if (digitalRead(BUTTON_PIN) != LOW) { 
    return; 
  } 
 
  if (now - lastButtonTime < BUTTON_DEBOUNCE) { 
    return; 
  } 
 
  lastButtonTime = now; 
 
  Serial.println(); 
  Serial.println("[BUTTON] Provisioning requested."); 
 
  startBLEProvisioning(); 
} 
 
void handleCreateTeamButton() { 
  const unsigned long now = millis(); 
 
  if (digitalRead(CREATE_TEAM_BUTTON_PIN) != LOW) { 
    return; 
  } 
 
  if (now - lastTeamButtonTime < TEAM_BUTTON_DEBOUNCE) { 
    return; 
  } 
 
  lastTeamButtonTime = now; 
 
  Serial.println(); 
  Serial.println("[BUTTON] Create Team requested."); 
 
  if (teamRequestRunning) { 
    Serial.println("[TEAM] Request already running."); 
    return; 
  } 
 
  if (resultLedActive) { 
    Serial.println("[TEAM] Previous result is still active."); 
    return; 
  } 
 
  createTeam(); 
} 
 
void setup() { 
  Serial.begin(115200); 
 
  pinMode(LED_PIN, OUTPUT); 
  digitalWrite(LED_PIN, LOW); 
 
  pinMode(BUTTON_PIN, INPUT_PULLUP); 
  pinMode(CREATE_TEAM_BUTTON_PIN, INPUT_PULLUP); 
 
  pinMode(YELLOW_LED_PIN, OUTPUT); 
  pinMode(GREEN_LED_PIN, OUTPUT); 
  pinMode(RED_LED_PIN, OUTPUT); 
 
  digitalWrite(YELLOW_LED_PIN, LOW); 
  digitalWrite(GREEN_LED_PIN, LOW); 
  digitalWrite(RED_LED_PIN, LOW); 
 
  WiFi.mode(WIFI_STA); 
  WiFi.setAutoReconnect(false); 
 
  loadWiFiCredentials(); 
 
  if (wifiSSID.length() > 0 && wifiPassword.length() > 0) { 
    Serial.println(); 
    Serial.println("[BOOT] Connecting to saved WiFi."); 
 
    startWiFiConnection(WIFI_SOURCE_SAVED); 
  } else { 
    Serial.println(); 
    Serial.println("[BOOT] No saved WiFi."); 
 
    startBLEProvisioning(); 
  } 
} 
 
void loop() { 
  handleWiFiConnection(); 
  handleButton(); 
  handleCreateTeamButton(); 
  handleBLEShutdown(); 
  handleResultLed(); 
} 
