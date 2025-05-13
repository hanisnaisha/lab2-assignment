// ESP32 WiFi + OLED Display + Firebase Realtime Database 

// --- Include Libraries ---
#include <WiFi.h>
#include <WebServer.h>
#include <EEPROM.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Firebase_ESP_Client.h>

// --- Constants ---
#define EEPROM_SIZE 512
#define FIREBASE_API_KEY "YOUR_FIREBASE_API_KEY"
#define FIREBASE_URL "https://YOUR_PROJECT_ID.firebaseio.com/"
#define FIREBASE_PATH "/oled_text"
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define CONFIG_BUTTON 0
#define STATUS_LED 2

// --- Default WiFi credentials (optional) ---
#define DEFAULT_SSID "YourDefaultSSID"
#define DEFAULT_PASSWORD "YourDefaultPassword"

// --- Globals ---
FirebaseData fbdo;
FirebaseAuth auth;
FirebaseConfig config;
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

String ssid = DEFAULT_SSID;
String pass = DEFAULT_PASSWORD;
String deviceId = "ESP32_001";
bool apmode = false;
WebServer server(80);
bool wifiConnected = false;
bool firebaseConnected = false;

// --- EEPROM Address Offsets ---
#define SSID_ADDR 0
#define PASS_ADDR 32
#define DEVICE_ID_ADDR 96

// --- Setup ---
void setup() {
  Serial.begin(115200);
  pinMode(CONFIG_BUTTON, INPUT_PULLUP);
  pinMode(STATUS_LED, OUTPUT);
  EEPROM.begin(EEPROM_SIZE);

  // Initialize OLED
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED init failed");
    return;
  }
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.display();

  // Load credentials and connect
  readCredentials();
  if (!connectToWiFi()) {
    setupAPMode();
  } else {
    setupFirebase();
  }
}

// --- Display Helper ---
void displayMessage(const char* message, int delayTime = 2000) {
  display.clearDisplay();
  display.setCursor(0, 0);
  display.setTextSize(2);
  display.println(message);
  display.display();
  delay(delayTime);
}

// --- Connect to WiFi ---
bool connectToWiFi() {
  Serial.print("Connecting to WiFi: ");
  Serial.println(ssid);
  WiFi.begin(ssid.c_str(), pass.c_str());

  int tries = 0;
  while (WiFi.status() != WL_CONNECTED && tries < 20) {
    delay(500);
    Serial.print(".");
    digitalWrite(STATUS_LED, !digitalRead(STATUS_LED));
    tries++;
  }
  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    digitalWrite(STATUS_LED, HIGH);
    wifiConnected = true;
    Serial.println("Connected!");
    Serial.println(WiFi.localIP());
    displayMessage("WiFi OK");
    return true;
  }

  Serial.println("WiFi Failed");
  digitalWrite(STATUS_LED, LOW);
  return false;
}

// --- Setup Firebase ---
void setupFirebase() {
  if (apmode) return;

  config.api_key = FIREBASE_API_KEY;
  config.database_url = FIREBASE_URL;

  // Only use if you're using Email/Password authentication
  auth.user.email = "YOUR_EMAIL_HERE";
  auth.user.password = "YOUR_PASSWORD_HERE";

  Firebase.begin(&config, &auth);
  Firebase.reconnectWiFi(true);

  if (Firebase.ready()) {
    firebaseConnected = true;
    displayMessage("Firebase OK");
    Firebase.RTDB.setString(&fbdo, FIREBASE_PATH, "ESP32 Ready!");
  } else {
    firebaseConnected = false;
    displayMessage("FB Fail");
  }
}

// --- Access Point Mode & Config Page ---
void setupAPMode() {
  apmode = true;
  WiFi.softAP("ESP32_ConfigAP");
  IPAddress IP = WiFi.softAPIP();
  displayMessage("AP Mode ON");
  displayMessage(IP.toString().c_str());

  int n = WiFi.scanNetworks();
  String networks = "<table>";
  for (int i = 0; i < n; ++i) {
    String enc = (WiFi.encryptionType(i) == WIFI_AUTH_OPEN) ? "Open" : "Secured";
    networks += "<tr><td>" + WiFi.SSID(i) + "</td><td>" + String(WiFi.RSSI(i)) + " dBm</td><td>" + enc + "</td></tr>";
  }
  networks += "</table>";

  server.on("/", HTTP_GET, [networks]() {
    String html = "<html><head><title>ESP32 Setup</title></head><body>";
    html += "<h2>WiFi Config</h2>";
    html += "<form action='/save'>";
    html += "Device ID: <input name='deviceid' value='" + deviceId + "'><br>";
    html += "SSID: <input name='ssid' value='" + ssid + "'><br>";
    html += "Password: <input name='pass' type='password'><br>";
    html += "<input type='submit' value='Save'>";
    html += "</form><hr>";
    html += "<h3>Available Networks</h3>";
    html += networks;
    html += "</body></html>";
    server.send(200, "text/html", html);
  });

  server.on("/save", HTTP_GET, []() {
    deviceId = server.arg("deviceid");
    ssid = server.arg("ssid");
    pass = server.arg("pass");
    writeCredentials();
    server.send(200, "text/html", "<html><body><h2>Saved. Restarting...</h2></body></html>");
    delay(2000);
    ESP.restart();
  });

  server.begin();
}

// --- EEPROM Handling ---
void writeCredentials() {
  writeStringToEEPROM(SSID_ADDR, ssid, 32);
  writeStringToEEPROM(PASS_ADDR, pass, 64);
  writeStringToEEPROM(DEVICE_ID_ADDR, deviceId, 32);
  EEPROM.commit();
}

void readCredentials() {
  ssid = readStringFromEEPROM(SSID_ADDR, 32);
  pass = readStringFromEEPROM(PASS_ADDR, 64);
  deviceId = readStringFromEEPROM(DEVICE_ID_ADDR, 32);
  if (deviceId.length() == 0) deviceId = "ESP32_001";
}

String readStringFromEEPROM(int addr, int len) {
  String value = "";
  for (int i = 0; i < len; i++) {
    char c = EEPROM.read(addr + i);
    if (c == 0 || c == 255) break;
    value += c;
  }
  return value;
}

void writeStringToEEPROM(int addr, String data, int maxlen) {
  int len = data.length();
  for (int i = 0; i < maxlen; i++) {
    EEPROM.write(addr + i, i < len ? data[i] : 0);
  }
}

// --- Main Loop ---
void loop() {
  if (apmode) {
    server.handleClient();
  } else if (wifiConnected && firebaseConnected) {
    if (Firebase.ready() && Firebase.RTDB.getString(&fbdo, FIREBASE_PATH)) {
      String msg = fbdo.stringData();
      displayMessage(msg.c_str(), 2000);
    }
  }
}