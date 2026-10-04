// RescueMesh - Victim Node
// ESP32-S3, Arduino
// OLED: GPIO8=SDA, GPIO9=SCL (U8g2)
// Buttons: SCROLL=GPIO5, SELECT=GPIO6
// LEDs: RED=GPIO47, YELLOW=GPIO48
// Buzzer: GPIO46

#include <WiFi.h>
#include <ESP_NOW.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "esp_wifi.h"

// ---------- CONFIG ----------

#define NODE_ID 1  // Change this for each victim node (1,2,3...)
#define WIFI_CHANNEL 1  // Must match the gateway's AP channel

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

#define BTN_SCROLL 5
#define BTN_SELECT 6

#define LED_RED    47
#define LED_YELLOW 48
#define BUZZER_PIN 46

// Alert types
// 0:SOS, 1:Medical, 2:Fire, 3:Trapped, 4:Safe
#define ALERT_TYPES_COUNT 5
const char* alertLabels[ALERT_TYPES_COUNT] = {
  "SOS",
  "Medical",
  "Fire",
  "Trapped",
  "Safe"
};

// ---------- GLOBALS ----------

int currentIndex = 0;
bool pendingSend = false;
uint8_t pendingAlertType = 0;

bool lastSendOk = false;
volatile bool sendCallbackReceived = false;

// ---------- OLED ----------

void initOLED() {
  Wire.begin(OLED_SDA, OLED_SCL);
  u8g2.begin();
}

void drawMenu() {
  u8g2.clearBuffer();

  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "RescueMesh");

  String nodeLine = "Node: " + String(NODE_ID);
  u8g2.drawStr(0, 24, nodeLine.c_str());

  u8g2.drawLine(0, 28, 127, 28);

  u8g2.setCursor(0, 42);
  u8g2.print("Alert: ");
  u8g2.print(alertLabels[currentIndex]);

  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 55, "[Scroll] [Select]");

  u8g2.sendBuffer();
}

void drawSending(uint8_t type) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "Sending...");
  u8g2.drawStr(0, 28, alertLabels[type]);
  u8g2.sendBuffer();
}

void drawSentOk(uint8_t type) {
  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "Sent OK");
  u8g2.drawStr(0, 28, alertLabels[type]);
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

void OnDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  lastSendOk = (status == ESP_NOW_SEND_SUCCESS);
  sendCallbackReceived = true;
}

void initESPNow() {
  WiFi.mode(WIFI_MODE_STA);
  WiFi.disconnect();

  esp_wifi_set_channel(WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);

  if (esp_now_init() != ESP_OK) {
    u8g2.clearBuffer();
    u8g2.drawStr(0, 12, "ESP-NOW init failed");
    u8g2.sendBuffer();
    Serial.println("ESP-NOW init failed");
    while (1) delay(1000);
  }

  esp_now_register_send_cb(OnDataSent);

  uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, broadcastMac, 6);
  peerInfo.channel = WIFI_CHANNEL;
  peerInfo.encrypt = false;
  peerInfo.ifidx = WIFI_IF_STA;

  esp_err_t addResult = esp_now_add_peer(&peerInfo);
  if (addResult != ESP_OK) {
    Serial.print("Failed to add broadcast peer, error: ");
    Serial.println(addResult);
  } else {
    Serial.println("Broadcast peer added OK");
  }
}

bool sendAlert(uint8_t alert_type) {
  uint8_t out[2];
  out[0] = NODE_ID;
  out[1] = alert_type;

  uint8_t broadcastMac[6] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};

  lastSendOk = false;
  sendCallbackReceived = false;

  esp_err_t err = esp_now_send(broadcastMac, out, sizeof(out));
  if (err != ESP_OK) {
    Serial.print("esp_now_send() error: ");
    Serial.println(err);
    return false;
  }

  int waited = 0;
  while (!sendCallbackReceived && waited < 300) {
    delay(10);
    waited += 10;
  }

  return lastSendOk;
}

// ---------- BUTTONS ----------

bool readButton(int pin) {
  return (digitalRead(pin) == LOW);
}

void waitForRelease() {
  while (readButton(BTN_SCROLL) || readButton(BTN_SELECT)) {
    delay(50);
  }
  delay(50);
}

// ---------- SETUP & LOOP ----------

void setup() {
  Serial.begin(115200);
  delay(500);

  pinMode(BTN_SCROLL, INPUT_PULLUP);
  pinMode(BTN_SELECT, INPUT_PULLUP);

  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);

  digitalWrite(LED_RED, LOW);
  digitalWrite(LED_YELLOW, LOW);
  digitalWrite(BUZZER_PIN, LOW);

  initOLED();

  u8g2.clearBuffer();
  u8g2.setFont(u8g2_font_ncenB08_tr);
  u8g2.drawStr(0, 12, "RescueMesh");
  u8g2.drawStr(0, 28, "Victim Node");

  String idLine = "Node ID: " + String(NODE_ID);
  u8g2.drawStr(0, 42, idLine.c_str());

  u8g2.sendBuffer();

  delay(1500);

  initESPNow();

  blinkLED(LED_YELLOW, 3, 150, 150);
  beepBuzzer(100);

  drawMenu();
}

void loop() {
  if (readButton(BTN_SCROLL)) {
    currentIndex = (currentIndex + 1) % ALERT_TYPES_COUNT;
    drawMenu();
    waitForRelease();
  }

  if (readButton(BTN_SELECT)) {
    pendingAlertType = currentIndex;

    digitalWrite(LED_RED, HIGH);
    drawSending(pendingAlertType);

    bool ok = sendAlert(pendingAlertType);

    digitalWrite(LED_RED, LOW);

    if (ok) {
      beepBuzzer(150);   // beep only on confirmed successful send
      drawSentOk(pendingAlertType);
    } else {
      // Short double-beep for failure, distinguishable from success
      beepBuzzer(50);
      delay(100);
      beepBuzzer(50);

      u8g2.clearBuffer();
      u8g2.drawStr(0, 12, "Send failed");
      u8g2.drawStr(0, 28, "Will retry");
      u8g2.sendBuffer();
    }

    delay(1200);
    drawMenu();
    waitForRelease();
  }

  delay(50);
}
