#include <WiFi.h>
#include <esp_now.h>
#include <HTTPClient.h>

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// =====================================================
// OLED CONFIGURATION
// =====================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define OLED_ADDRESS 0x3C

Adafruit_SSD1306 display(
  SCREEN_WIDTH,
  SCREEN_HEIGHT,
  &Wire,
  OLED_RESET
);

// =====================================================
// ESP32-S3 PIN CONNECTIONS
// =====================================================

// Buttons
const int BUTTON_SCROLL = 4;   // Scroll through the emergency symbols
const int BUTTON_SELECT = 5;   // Send currently selected symbol

// Outputs
const int BUZZER_PIN = 6;

const int ALERT_LED = 7;              // Red: alert transmission indicator
const int MESH_CONNECTED_LED = 15;    // Green: valid mesh peer detected
const int MESH_DISCONNECTED_LED = 16; // Yellow/red: no valid mesh peer

// =====================================================
// WI-FI + PARENT DASHBOARD SETTINGS
// =====================================================

// Wokwi's virtual Wi-Fi network:
const char* WIFI_SSID = "Wokwi-GUEST";
const char* WIFI_PASSWORD = "";

// Flask dashboard running on your Windows computer.
// Works only if:
// 1. `python app.py` is running,
// 2. `wokwigw.exe` is running,
// 3. Wokwi Private Gateway is enabled.
const char* DASHBOARD_URL =
  "http://host.wokwi.internal:5000/alert";

// =====================================================
// ESP-NOW SETTINGS
// =====================================================

// IMPORTANT:
// Replace this placeholder address with the MAC address
// of your second physical ESP32 board later.
//
// MAC AA:BB:CC:DD:EE:FF becomes:
// {0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF}
uint8_t peerAddress[] = {
  0x24, 0x6F, 0x28, 0x00, 0x00, 0x00
};

const unsigned long HEARTBEAT_INTERVAL = 3000;
const unsigned long PEER_TIMEOUT = 10000;

unsigned long lastHeartbeatSent = 0;
unsigned long lastPeerSeen = 0;

bool meshConnected = false;

// =====================================================
// MESH PACKET TYPES
// =====================================================

enum PacketType {
  PACKET_HEARTBEAT = 1,
  PACKET_ALERT = 2,
  PACKET_ACK = 3,
  PACKET_COORDINATION = 4
};

// =====================================================
// EMERGENCY ICON / ALERT IDs
// =====================================================

enum AlertType {
  SOS = 0,
  MEDICAL = 1,
  TRAPPED = 2,
  SAFE = 3,
  WATER = 4,
  FIRE = 5,
  BLOCKED = 6
};

const int TOTAL_ALERTS = 7;
int selectedAlert = SOS;

// This text is used only in Serial Monitor and dashboard.
// The OLED itself displays symbols only.
const char* alertText[] = {
  "SOS - Emergency assistance required",
  "MEDICAL - Medical assistance required",
  "TRAPPED - Person requires rescue",
  "SAFE - Person is safe",
  "WATER - Food or drinking water required",
  "FIRE - Fire hazard reported",
  "BLOCKED - Road or route is blocked"
};

// =====================================================
// PACKET STRUCTURE
// =====================================================

typedef struct __attribute__((packed)) {
  uint8_t packetType;
  uint8_t alertID;
  uint32_t sequence;
  uint32_t timestampMs;
  char sender[16];
  char message[64];
} MeshPacket;

MeshPacket outgoingPacket;
MeshPacket incomingPacket;

uint32_t packetSequence = 0;

// =====================================================
// BUTTON DEBOUNCE
// =====================================================

unsigned long lastButtonPress = 0;
const unsigned long debounceTime = 250;

// =====================================================
// OLED: MESH STATUS SYMBOLS
// =====================================================

void drawMeshConnectedIcon() {
  // Radio wave icon at top-right.
  display.fillCircle(118, 7, 2, SSD1306_WHITE);
  display.drawCircle(118, 7, 5, SSD1306_WHITE);
  display.drawCircle(118, 7, 8, SSD1306_WHITE);

  // Covers lower half to make it look like wireless waves.
  display.fillRect(108, 8, 20, 8, SSD1306_BLACK);
}

void drawMeshDisconnectedIcon() {
  drawMeshConnectedIcon();

  // Diagonal slash means unavailable/disconnected.
  display.drawLine(108, 0, 127, 15, SSD1306_WHITE);
  display.drawLine(109, 0, 127, 14, SSD1306_WHITE);
}

void drawHeader() {
  display.drawLine(0, 14, 127, 14, SSD1306_WHITE);

  if (meshConnected) {
    drawMeshConnectedIcon();
  } else {
    drawMeshDisconnectedIcon();
  }
}

// =====================================================
// OLED: EMERGENCY SYMBOLS
// =====================================================

void drawSOS() {
  // Warning triangle + exclamation mark
  display.drawTriangle(64, 18, 32, 55, 96, 55, SSD1306_WHITE);
  display.fillRect(61, 29, 6, 15, SSD1306_WHITE);
  display.fillCircle(64, 50, 3, SSD1306_WHITE);
}

void drawMedical() {
  // Medical plus icon
  display.fillRect(57, 19, 14, 38, SSD1306_WHITE);
  display.fillRect(40, 31, 48, 14, SSD1306_WHITE);
}

void drawTrapped() {
  // Person icon
  display.drawCircle(64, 26, 6, SSD1306_WHITE);
  display.drawLine(64, 32, 64, 48, SSD1306_WHITE);
  display.drawLine(64, 37, 52, 42, SSD1306_WHITE);
  display.drawLine(64, 37, 76, 42, SSD1306_WHITE);
  display.drawLine(64, 48, 56, 56, SSD1306_WHITE);
  display.drawLine(64, 48, 72, 56, SSD1306_WHITE);

  // Barrier/cage lines
  display.drawRect(38, 17, 52, 42, SSD1306_WHITE);
  display.drawLine(44, 18, 44, 58, SSD1306_WHITE);
  display.drawLine(50, 18, 50, 58, SSD1306_WHITE);
  display.drawLine(78, 18, 78, 58, SSD1306_WHITE);
  display.drawLine(84, 18, 84, 58, SSD1306_WHITE);
}

void drawSafe() {
  // Circle + checkmark
  display.drawCircle(64, 37, 23, SSD1306_WHITE);

  display.drawLine(47, 38, 59, 50, SSD1306_WHITE);
  display.drawLine(48, 39, 60, 51, SSD1306_WHITE);

  display.drawLine(59, 50, 81, 27, SSD1306_WHITE);
  display.drawLine(60, 51, 82, 28, SSD1306_WHITE);
}

void drawWater() {
  // Water droplet outline
  display.drawLine(64, 17, 47, 43, SSD1306_WHITE);
  display.drawLine(47, 43, 48, 50, SSD1306_WHITE);
  display.drawLine(48, 50, 55, 57, SSD1306_WHITE);
  display.drawLine(55, 57, 64, 59, SSD1306_WHITE);
  display.drawLine(64, 59, 73, 57, SSD1306_WHITE);
  display.drawLine(73, 57, 80, 50, SSD1306_WHITE);
  display.drawLine(80, 50, 81, 43, SSD1306_WHITE);
  display.drawLine(81, 43, 64, 17, SSD1306_WHITE);

  // Water wave
  display.drawLine(54, 46, 60, 48, SSD1306_WHITE);
  display.drawLine(60, 48, 66, 46, SSD1306_WHITE);
  display.drawLine(66, 46, 72, 48, SSD1306_WHITE);
}

void drawFire() {
  // Outer flame
  display.drawLine(64, 17, 54, 31, SSD1306_WHITE);
  display.drawLine(54, 31, 48, 44, SSD1306_WHITE);
  display.drawLine(48, 44, 52, 54, SSD1306_WHITE);
  display.drawLine(52, 54, 64, 59, SSD1306_WHITE);
  display.drawLine(64, 59, 76, 54, SSD1306_WHITE);
  display.drawLine(76, 54, 81, 44, SSD1306_WHITE);
  display.drawLine(81, 44, 75, 31, SSD1306_WHITE);
  display.drawLine(75, 31, 64, 17, SSD1306_WHITE);

  // Inner flame
  display.drawLine(64, 31, 58, 45, SSD1306_WHITE);
  display.drawLine(58, 45, 64, 53, SSD1306_WHITE);
  display.drawLine(64, 53, 70, 45, SSD1306_WHITE);
  display.drawLine(70, 45, 64, 31, SSD1306_WHITE);
}

void drawBlocked() {
  // Road barrier
  display.drawRect(30, 28, 68, 18, SSD1306_WHITE);

  // Barrier warning stripes
  display.drawLine(32, 44, 47, 29, SSD1306_WHITE);
  display.drawLine(45, 44, 60, 29, SSD1306_WHITE);
  display.drawLine(58, 44, 73, 29, SSD1306_WHITE);
  display.drawLine(71, 44, 86, 29, SSD1306_WHITE);

  // Legs
  display.drawLine(40, 46, 35, 57, SSD1306_WHITE);
  display.drawLine(88, 46, 93, 57, SSD1306_WHITE);
}

void drawSelectedSymbol() {
  switch (selectedAlert) {
    case SOS:
      drawSOS();
      break;

    case MEDICAL:
      drawMedical();
      break;

    case TRAPPED:
      drawTrapped();
      break;

    case SAFE:
      drawSafe();
      break;

    case WATER:
      drawWater();
      break;

    case FIRE:
      drawFire();
      break;

    case BLOCKED:
      drawBlocked();
      break;
  }
}

// =====================================================
// OLED: SCREEN STATES
// =====================================================

void drawPageDots() {
  // Seven dots show selected emergency-symbol position.
  for (int i = 0; i < TOTAL_ALERTS; i++) {
    int x = 40 + (i * 8);

    if (i == selectedAlert) {
      display.fillCircle(x, 61, 2, SSD1306_WHITE);
    } else {
      display.drawCircle(x, 61, 2, SSD1306_WHITE);
    }
  }
}

void showMenu() {
  display.clearDisplay();

  drawHeader();

  // Left/right arrows show that user can scroll.
  display.fillTriangle(5, 36, 15, 30, 15, 42, SSD1306_WHITE);
  display.fillTriangle(123, 36, 113, 30, 113, 42, SSD1306_WHITE);

  drawSelectedSymbol();
  drawPageDots();

  display.display();
}

void showAlertSentScreen() {
  display.clearDisplay();

  // Circle and check mark = alert submitted.
  display.drawCircle(64, 31, 24, SSD1306_WHITE);

  display.drawLine(48, 32, 59, 43, SSD1306_WHITE);
  display.drawLine(49, 33, 60, 44, SSD1306_WHITE);

  display.drawLine(59, 43, 81, 20, SSD1306_WHITE);
  display.drawLine(60, 44, 82, 21, SSD1306_WHITE);

  display.display();
}

void showCoordinationReceived() {
  display.clearDisplay();

  // Speech bubble
  display.drawRoundRect(24, 18, 80, 32, 4, SSD1306_WHITE);
  display.fillTriangle(44, 50, 54, 50, 44, 58, SSD1306_WHITE);

  // Checkmark inside means instruction received
  display.drawLine(53, 34, 61, 42, SSD1306_WHITE);
  display.drawLine(61, 42, 77, 26, SSD1306_WHITE);

  display.display();
}

// =====================================================
// HARDWARE STATUS
// =====================================================

void setMeshStatus(bool connected) {
  meshConnected = connected;

  digitalWrite(MESH_CONNECTED_LED, connected ? HIGH : LOW);
  digitalWrite(MESH_DISCONNECTED_LED, connected ? LOW : HIGH);
}

// =====================================================
// WI-FI + HTTP DASHBOARD FUNCTIONS
// =====================================================

void connectToDashboardWiFi() {
  Serial.println();
  Serial.println("[WIFI] Connecting to Wokwi virtual Wi-Fi...");

  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int attempts = 0;

  while (WiFi.status() != WL_CONNECTED && attempts < 30) {
    delay(500);
    Serial.print(".");
    attempts++;
  }

  Serial.println();

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("[WIFI] Connected.");
    Serial.print("[WIFI] ESP32 IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("[WIFI] Connection failed.");
  }
}

String jsonEscape(String text) {
  text.replace("\\", "\\\\");
  text.replace("\"", "\\\"");
  text.replace("\n", "\\n");
  return text;
}

void sendAlertToDashboard() {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("[DASHBOARD] Wi-Fi unavailable. Alert is not posted.");
    return;
  }

  HTTPClient http;

  String jsonPayload = "{";
  jsonPayload += "\"node\":\"NODE_A\",";
  jsonPayload += "\"alert_id\":" + String(selectedAlert) + ",";
  jsonPayload += "\"alert_text\":\"";
  jsonPayload += jsonEscape(String(alertText[selectedAlert]));
  jsonPayload += "\",";
  jsonPayload += "\"mesh_status\":\"";
  jsonPayload += (meshConnected ? "CONNECTED" : "DISCONNECTED");
  jsonPayload += "\"";
  jsonPayload += "}";

  Serial.println("[DASHBOARD] Posting alert to parent computer:");
  Serial.println(jsonPayload);

  http.begin(DASHBOARD_URL);
  http.addHeader("Content-Type", "application/json");

  int httpCode = http.POST(jsonPayload);

  if (httpCode > 0) {
    Serial.print("[DASHBOARD] HTTP response: ");
    Serial.println(httpCode);

    Serial.print("[DASHBOARD] Server reply: ");
    Serial.println(http.getString());
  } else {
    Serial.print("[DASHBOARD] POST failed: ");
    Serial.println(http.errorToString(httpCode).c_str());
  }

  http.end();
}

// =====================================================
// ESP-NOW: PACKET SENDING
// =====================================================

void sendPacket(uint8_t packetType, uint8_t alertID, const char* message) {
  memset(&outgoingPacket, 0, sizeof(outgoingPacket));

  outgoingPacket.packetType = packetType;
  outgoingPacket.alertID = alertID;
  outgoingPacket.sequence = ++packetSequence;
  outgoingPacket.timestampMs = millis();

  strncpy(outgoingPacket.sender, "NODE_A",
          sizeof(outgoingPacket.sender) - 1);

  strncpy(outgoingPacket.message, message,
          sizeof(outgoingPacket.message) - 1);

  esp_err_t result = esp_now_send(
    peerAddress,
    (uint8_t*)&outgoingPacket,
    sizeof(outgoingPacket)
  );

  if (result == ESP_OK) {
    Serial.println("[ESP-NOW] Packet queued for sending.");
  } else {
    Serial.print("[ESP-NOW] Send error code: ");
    Serial.println(result);
  }
}

// =====================================================
// ESP-NOW: CALLBACKS
// =====================================================

// Arduino-ESP32 Core 3.x compatible callback
void onDataSent(const wifi_tx_info_t* info,
                esp_now_send_status_t status) {
  if (status == ESP_NOW_SEND_SUCCESS) {
    Serial.println("[ESP-NOW] MAC-layer delivery: SUCCESS.");
  } else {
    Serial.println("[ESP-NOW] MAC-layer delivery: FAILED.");
  }
}

// Arduino-ESP32 Core 3.x compatible callback
void onDataReceived(const esp_now_recv_info_t* info,
                    const uint8_t* incomingData,
                    int len) {
  if (len != sizeof(MeshPacket)) {
    Serial.println("[ESP-NOW] Invalid packet discarded.");
    return;
  }

  memcpy(&incomingPacket, incomingData, sizeof(incomingPacket));

  // A valid packet proves that a peer is currently active.
  lastPeerSeen = millis();
  setMeshStatus(true);

  Serial.println();
  Serial.println("======================================");
  Serial.println("ESP-NOW PACKET RECEIVED");
  Serial.print("From Node    : ");
  Serial.println(incomingPacket.sender);
  Serial.print("Packet Type  : ");
  Serial.println(incomingPacket.packetType);
  Serial.print("Alert ID     : ");
  Serial.println(incomingPacket.alertID);
  Serial.print("Message      : ");
  Serial.println(incomingPacket.message);
  Serial.println("======================================");

  // Peer heartbeat gets an application-level ACK.
  if (incomingPacket.packetType == PACKET_HEARTBEAT) {
    sendPacket(PACKET_ACK, 0, "HEARTBEAT_ACK");
  }

  // Peer emergency alert gets an application-level ACK.
  if (incomingPacket.packetType == PACKET_ALERT) {
    sendPacket(PACKET_ACK,
               incomingPacket.alertID,
               "ALERT_ACK");
  }

  // A coordination instruction is shown locally.
  if (incomingPacket.packetType == PACKET_COORDINATION) {
    tone(BUZZER_PIN, 1200, 250);

    showCoordinationReceived();

    delay(1000);

    showMenu();
  }
}

// =====================================================
// ESP-NOW INITIALIZATION
// =====================================================

bool initialiseEspNow() {
  // Station mode allows ESP-NOW + normal Wi-Fi operation.
  WiFi.mode(WIFI_STA);

  Serial.print("[ESP-NOW] This node MAC: ");
  Serial.println(WiFi.macAddress());

  if (esp_now_init() != ESP_OK) {
    Serial.println("[ESP-NOW] Initialization failed.");
    return false;
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataReceived);

  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, peerAddress, 6);

  // 0 = use current Wi-Fi channel.
  peerInfo.channel = 0;

  // Keep false during first test. Enable encryption later.
  peerInfo.encrypt = false;

  if (!esp_now_is_peer_exist(peerAddress)) {
    if (esp_now_add_peer(&peerInfo) != ESP_OK) {
      Serial.println("[ESP-NOW] Could not add peer.");
      return false;
    }
  }

  Serial.println("[ESP-NOW] Ready.");
  return true;
}

// =====================================================
// ALERT TRANSMISSION
// =====================================================

void transmitAlert() {
  // Text information is shown only on Serial Monitor/dashboard.
  // OLED remains universal-symbol only.
  Serial.println();
  Serial.println("======================================");
  Serial.println("EMERGENCY MESH ALERT GENERATED");
  Serial.println("--------------------------------------");
  Serial.println("Node ID      : NODE_A");
  Serial.print("Alert ID     : ");
  Serial.println(selectedAlert);
  Serial.print("Alert Type   : ");
  Serial.println(alertText[selectedAlert]);
  Serial.print("Time (ms)    : ");
  Serial.println(millis());
  Serial.print("Mesh Status  : ");
  Serial.println(meshConnected ? "CONNECTED" : "DISCONNECTED");
  Serial.println("======================================");

  // Local alert feedback
  digitalWrite(ALERT_LED, HIGH);
  tone(BUZZER_PIN, 1800, 350);
  showAlertSentScreen();

  // 1. Send peer-to-peer mesh message.
  sendPacket(
    PACKET_ALERT,
    selectedAlert,
    alertText[selectedAlert]
  );

  // 2. Send same alert to the parent computer dashboard.
  sendAlertToDashboard();

  delay(1200);

  digitalWrite(ALERT_LED, LOW);

  showMenu();
}

// =====================================================
// SETUP
// =====================================================

void setup() {
  Serial.begin(115200);

  pinMode(BUTTON_SCROLL, INPUT_PULLUP);
  pinMode(BUTTON_SELECT, INPUT_PULLUP);

  pinMode(BUZZER_PIN, OUTPUT);

  pinMode(ALERT_LED, OUTPUT);
  pinMode(MESH_CONNECTED_LED, OUTPUT);
  pinMode(MESH_DISCONNECTED_LED, OUTPUT);

  digitalWrite(ALERT_LED, LOW);

  // Starts as offline until valid ESP-NOW traffic arrives.
  setMeshStatus(false);

  // OLED wiring:
  // SDA -> GPIO 8
  // SCL -> GPIO 9
  Wire.begin(8, 9);

  if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
    Serial.println("ERROR: OLED initialization failed.");

    while (true) {
      digitalWrite(MESH_CONNECTED_LED, LOW);
      digitalWrite(MESH_DISCONNECTED_LED, HIGH);
      delay(250);

      digitalWrite(MESH_DISCONNECTED_LED, LOW);
      delay(250);
    }
  }

  showMenu();

  // Connect to Wokwi Wi-Fi for Flask parent dashboard.
  connectToDashboardWiFi();

  // Start ESP-NOW for future physical peer-node communication.
  if (!initialiseEspNow()) {
    Serial.println("[ESP-NOW] Unavailable: mesh state remains offline.");
    setMeshStatus(false);
    showMenu();
  }
}

// =====================================================
// MAIN LOOP
// =====================================================

void loop() {
  unsigned long now = millis();

  // Sends a heartbeat every 3 seconds.
  if (now - lastHeartbeatSent >= HEARTBEAT_INTERVAL) {
    lastHeartbeatSent = now;

    sendPacket(
      PACKET_HEARTBEAT,
      0,
      "HEARTBEAT"
    );
  }

  // If no valid packet arrives for 10 seconds,
  // mark the node as disconnected.
  if (lastPeerSeen == 0 ||
      now - lastPeerSeen > PEER_TIMEOUT) {

    if (meshConnected) {
      Serial.println("[MESH] Peer heartbeat timed out.");

      setMeshStatus(false);
      showMenu();
    }
  }

  // Scroll button: choose next emergency symbol.
  if (digitalRead(BUTTON_SCROLL) == LOW &&
      now - lastButtonPress > debounceTime) {

    selectedAlert = (selectedAlert + 1) % TOTAL_ALERTS;

    showMenu();

    lastButtonPress = now;
  }

  // Select button: send selected alert.
  if (digitalRead(BUTTON_SELECT) == LOW &&
      now - lastButtonPress > debounceTime) {

    transmitAlert();

    lastButtonPress = millis();
  }
}