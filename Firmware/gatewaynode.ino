// RescueMesh - Gateway Node
// ESP32-S3, Arduino
// OLED: GPIO8=SDA, GPIO9=SCL (U8g2)
// LEDs: RED=GPIO47, YELLOW=GPIO48
// BUZZER: GPIO46

#include <WiFi.h>
#include <WebServer.h>
#include <ESP_NOW.h>
#include <Preferences.h>
#include <Wire.h>
#include <U8g2lib.h>

// ---------- CONFIG ----------

#define OLED_SDA 8
#define OLED_SCL 9

// For SSD1306 128x64 I2C (software I2C on GPIO8/9):
U8G2_SSD1306_128X64_NONAME_F_SW_I2C u8g2(
  U8G2_R0,
  /* clock=*/ OLED_SCL,
  /* data=*/ OLED_SDA,
  /* reset=*/ U8X8_PIN_NONE
);

// If your display is SH1106, use:
// U8G2_SH1106_128X64_NONAME_F_SW_I2C u8g2(
//   U8G2_R0,
//   OLED_SCL,
//   OLED_SDA,
//   U8X8_PIN_NONE
// );

#define LED_RED    47
#define LED_YELLOW 48
#define BUZZER_PIN 46

// Wi-Fi AP settings for the web interface
const char* AP_SSID = "RescueMesh-GW";
const char* AP_PASS = "rescue123";
#define WIFI_CHANNEL 1  // Must match the victim node's channel

// ---------- GLOBALS ----------

WebServer server(80);
Preferences prefs;

struct Alert {
  uint64_t timestamp;
  uint8_t  node_id;
  uint8_t  alert_type; // 0:SOS, 1:Medical, 2:Fire, 3:Trapped, 4:Safe
  bool     forwarded;
};

#define MAX_ALERTS 40
Alert alerts[MAX_ALERTS];
int alertCount = 0;

// Severity: higher = more urgent
int getSeverity(uint8_t type) {
  switch (type) {
    case 0: return 5; // SOS
    case 2: return 4; // Fire
    case 3: return 3; // Trapped
    case 1: return 2; // Medical
    case 4: return 1; // Safe
    default: return 0;
  }
}

// ---------- STORAGE (Preferences) ----------

void loadAlerts() {
  prefs.begin("alerts", true);
  alertCount = prefs.getInt("count", 0);
  if (alertCount > MAX_ALERTS) alertCount = MAX_ALERTS;

  for (int i = 0; i < alertCount; i++) {
    char key[16];
    sprintf(key, "a%d", i);
    alerts[i].timestamp  = prefs.getLong((String(key) + "t").c_str(), 0);
    alerts[i].node_id    = prefs.getUChar((String(key) + "n").c_str(), 0);
    alerts[i].alert_type = prefs.getUChar((String(key) + "a").c_str(), 0);
    alerts[i].forwarded  = prefs.getBool((String(key) + "f").c_str(), false);
  }
  prefs.end();
}

void saveAlerts() {
  prefs.begin("alerts", false);
  prefs.putInt("count", alertCount);

  for (int i = 0; i < alertCount; i++) {
    char key[16];
    sprintf(key, "a%d", i);
    prefs.putLong((String(key) + "t").c_str(), alerts[i].timestamp);
    prefs.putUChar((String(key) + "n").c_str(), alerts[i].node_id);
    prefs.putUChar((String(key) + "a").c_str(), alerts[i].alert_type);
    prefs.putBool((String(key) + "f").c_str(), alerts[i].forwarded);
  }
  prefs.end();
}

void appendAlert(const Alert &a) {
  if (alertCount >= MAX_ALERTS) {
    for (int i = 0; i < MAX_ALERTS - 1; i++) {
      alerts[i] = alerts[i + 1];
    }
    alertCount = MAX_ALERTS - 1;
  }
  alerts[alertCount++] = a;
  saveAlerts();
}

// ---------- OLED ----------

void initOLED() {
  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
}

void showOnOLED(const char *line1, const char *line2 = "") {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 15, line1);
  if (line2 && line2[0]) {
    u8g2.drawStr(0, 35, line2);
  }
  u8g2.sendBuffer();
}

// ---------- LED & BUZZER ----------

void blinkLED(int pin, int times, int msOn, int msOff) {
  for (int i = 0; i < times; i++) {
    digitalWrite(pin, HIGH);
    delay(msOn);
    digitalWrite(pin, LOW);
    delay(msOff);
  }
}

void beepBuzzer(int msOn = 150) {
  digitalWrite(BUZZER_PIN, HIGH);
  delay(msOn);
  digitalWrite(BUZZER_PIN, LOW);
}

// ---------- ESP-NOW ----------

void onDataReceived(const esp_now_recv_info *info, const uint8_t *incomingData, int len) {
  if (len < 2) return;

  uint8_t node_id = incomingData[0];
  uint8_t alert_type = incomingData[1];

  Alert a;
  a.timestamp = millis();
  a.node_id = node_id;
  a.alert_type = alert_type;
  a.forwarded = false;

  appendAlert(a);

  digitalWrite(LED_RED, HIGH);
  beepBuzzer(150);
  digitalWrite(LED_RED, LOW);

  static char line2Buf[64];
  snprintf(line2Buf, sizeof(line2Buf), "Node:%d Type:%d", node_id, alert_type);
  showOnOLED("Alert received", line2Buf);

  Serial.print("Alert from node ");
  Serial.print(node_id);
  Serial.print(", type ");
  Serial.println(alert_type);
}

void initESPNow() {
  WiFi.mode(WIFI_MODE_AP);
  WiFi.softAP(AP_SSID, AP_PASS, WIFI_CHANNEL);

  if (esp_now_init() != ESP_OK) {
    showOnOLED("ESP-NOW init failed", "");
    Serial.println("ESP-NOW init failed");
    while (1) delay(1000);
  }

  esp_now_register_recv_cb(onDataReceived);
}

// ---------- WEB SERVER ----------

String getAlertTypeStr(uint8_t t) {
  switch (t) {
    case 0: return "SOS";
    case 1: return "Medical";
    case 2: return "Fire";
    case 3: return "Trapped";
    case 4: return "Safe";
    default: return "Unknown";
  }
}

void handleRoot() {
  int idx[MAX_ALERTS];
  for (int i = 0; i < alertCount; i++) idx[i] = i;

  for (int i = 0; i < alertCount - 1; i++) {
    for (int j = 0; j < alertCount - i - 1; j++) {
      int s1 = getSeverity(alerts[idx[j]].alert_type);
      int s2 = getSeverity(alerts[idx[j+1]].alert_type);
      if (s1 < s2) {
        int tmp = idx[j];
        idx[j] = idx[j+1];
        idx[j+1] = tmp;
      }
    }
  }

  String html =
    "<!DOCTYPE html><html><head><meta charset='utf-8'>"
    "<title>RescueMesh Gateway</title>"
    "<style>"
    "body{font-family:Arial;margin:20px;}"
    "table{border-collapse:collapse;width:100%;}"
    "th,td{border:1px solid #ccc;padding:8px;text-align:left;}"
    "th{background:#f0f0f0;}"
    ".sev5{color:#d00;font-weight:bold;}"
    ".sev4{color:#f60;}"
    ".sev3{color:#c90;}"
    ".sev2{color:#09c;}"
    ".sev1{color:#090;}"
    "</style></head><body>"
    "<h1>RescueMesh Gateway</h1>"
    "<h2>Alerts (sorted by severity)</h2>"
    "<table><tr><th>#</th><th>Node</th><th>Type</th><th>Severity</th><th>Time (ms)</th></tr>";

  for (int i = 0; i < alertCount; i++) {
    int j = idx[i];
    const Alert &a = alerts[j];
    int sev = getSeverity(a.alert_type);
    String sevClass = "sev" + String(sev);

    html +=
      "<tr>"
      "<td>" + String(i + 1) + "</td>"
      "<td>" + String(a.node_id) + "</td>"
      "<td>" + getAlertTypeStr(a.alert_type) + "</td>"
      "<td class='" + sevClass + "'>" + String(sev) + "</td>"
      "<td>" + String(a.timestamp) + "</td>"
      "</tr>";
  }

  html += "</table>"
          "<p><a href='/clear'>Clear all alerts</a></p>"
          "</body></html>";

  server.send(200, "text/html", html);
}

void handleClear() {
  prefs.begin("alerts", false);
  prefs.clear();
  prefs.end();
  alertCount = 0;
  server.sendHeader("Location", "/");
  server.send(303);
}

void initWebServer() {
  server.on("/", handleRoot);
  server.on("/clear", handleClear);
  server.begin();
}

// ---------- SETUP & LOOP ----------

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  initOLED();
  showOnOLED("RescueMesh GW", "Booting...");

  loadAlerts();

  initESPNow();  // sets up WiFi AP with fixed channel + ESP-NOW

  String ip = WiFi.softAPIP().toString();

  static char line1[64];
  static char line2[64];
  snprintf(line1, sizeof(line1), "AP: %s", AP_SSID);
  snprintf(line2, sizeof(line2), "IP: %s", ip.c_str());
  showOnOLED(line1, line2);

  Serial.println("RescueMesh Gateway");
  Serial.print("Connect to WiFi: ");
  Serial.println(AP_SSID);
  Serial.print("Password: ");
  Serial.println(AP_PASS);
  Serial.print("Channel: ");
  Serial.println(WIFI_CHANNEL);
  Serial.print("Then open http://");
  Serial.println(ip);

  initWebServer();

  blinkLED(LED_YELLOW, 3, 150, 150);
  beepBuzzer(100);
}

void loop() {
  server.handleClient();
  delay(10);
}
