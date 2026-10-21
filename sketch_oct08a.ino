#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <TFT_eSPI.h>
#include <WebServer.h>

const char* ssid = "HONOR X6c";
const char* password = "SabrinoF";
const int websocket_port = 81;
const int http_port = 80;

WebSocketsServer webSocket = WebSocketsServer(websocket_port);
WebServer httpServer(http_port);
TFT_eSPI tft = TFT_eSPI();
String lastDetection = "No detection received";
uint32_t detectionsReceived = 0;
uint32_t lastDetectionMillis = 0;
uint32_t fpsWindowStart = 0;
uint16_t framesInWindow = 0;

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  if ( y >= tft.height() ) return false;

  tft.pushImage(x, y, w, h, bitmap);

  return true;
}

void handleStatus() {
  String response = "{\"last\":\"" + lastDetection +
                    "\",\"count\":" + String(detectionsReceived) +
                    ",\"seconds_ago\":" +
                    String((millis() - lastDetectionMillis) / 1000) + "}";
  httpServer.send(200, "application/json", response);
}

void handleHome() {
  httpServer.send(200, "text/html",
    "<!doctype html><html><head><meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>ESP32 Detection Status</title></head><body>"
    "<h2>ESP32 Detection Status</h2><p>Last message: <strong id='last'>Loading...</strong></p>"
    "<p>Messages received: <span id='count'>0</span></p>"
    "<p>Last received: <span id='age'>-</span> seconds ago</p>"
    "<script>async function update(){try{const r=await fetch('/status');const d=await r.json();"
    "document.getElementById('last').textContent=d.last;"
    "document.getElementById('count').textContent=d.count;"
    "document.getElementById('age').textContent=d.count===0?'never':d.seconds_ago;"
    "}catch(e){document.getElementById('last').textContent='ESP32 unavailable';}}"
    "update();setInterval(update,1000);</script></body></html>");
}

#define JPEG_BUFFER_SIZE 50 * 1024
uint8_t* jpeg_buffer;

uint32_t jpeg_buffer_pos = 0;
uint32_t expected_jpeg_size = 0;

void webSocketEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
  switch (type) {
    case WStype_DISCONNECTED:
      Serial.printf("[%u] Connection lost!\n", num);
      break;

    case WStype_CONNECTED:
      // DIAGNOSTIC: Confirming a client has successfully linked up over WebSockets
      Serial.printf("[%u] New WebSocket client connected!\n", num);
      break;

    case WStype_TEXT:
      {
        String text;
        text.reserve(length);
        for (size_t i = 0; i < length; i++) {
          text += (char)payload[i];
        }
        if (text.startsWith("JPEG_FRAME_SIZE:")) {
          expected_jpeg_size = text.substring(16).toInt();
          jpeg_buffer_pos = 0;
          // DIAGNOSTIC: Tracking frame header reception
          Serial.printf("[WS] Incoming frame size header received: %d bytes\n", expected_jpeg_size);
          
          if (expected_jpeg_size > JPEG_BUFFER_SIZE) {
            Serial.printf("Error: Came JPEG size (%d) buffer (%d) over!\n", expected_jpeg_size, JPEG_BUFFER_SIZE);
            expected_jpeg_size = 0;
          }
        } else if (text == "person a detected" || text == "person a not detected" ||
                   text == "person b detected" || text == "person b not detected" ||
                   text == "intruder detected" || text == "intruder not detected" ||
                   text == "fire detected" || text == "fire not detected") {
          lastDetection = text;
          detectionsReceived++;
          lastDetectionMillis = millis();
          Serial.printf("[WS] Received: %s\n", text.c_str());
          Serial2.println(text);
          Serial.printf("[UART] Sent to Arduino: %s\n", text.c_str());
        }
        break;
      }

    case WStype_BIN:
      if (expected_jpeg_size > 0 && jpeg_buffer_pos + length <= expected_jpeg_size) {
        memcpy(jpeg_buffer + jpeg_buffer_pos, payload, length);
        jpeg_buffer_pos += length;
      }

      if (expected_jpeg_size > 0 && jpeg_buffer_pos >= expected_jpeg_size) {
        TJpgDec.drawJpg(0, 0, jpeg_buffer, expected_jpeg_size);
        expected_jpeg_size = 0;
        framesInWindow++;
        uint32_t now = millis();
        if (now - fpsWindowStart >= 1000) {
          Serial.printf("[TFT] Received/displayed FPS: %u\n", framesInWindow);
          framesInWindow = 0;
          fpsWindowStart = now;
        }
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial2.begin(9600, SERIAL_8N1, 16, 17);
  delay(1000); // Give the Serial Monitor time to connect
  Serial.println("\n--- ESP32 TFT Diagnostic Boot ---");

  // DIAGNOSTIC: Checking if PSRAM allocation works
  Serial.println("[Setup] Allocating JPEG buffer memory using internal SRAM...");
  jpeg_buffer = (uint8_t*)malloc(JPEG_BUFFER_SIZE);
  if (jpeg_buffer == NULL) {
    Serial.println("Error: Not defined area for JPEG buffer! Ensure PSRAM is enabled in IDE.");
    while (1);
  }
  Serial.println("[Setup] JPEG buffer memory allocated successfully.");

  // DIAGNOSTIC: Initializing TFT Display
  Serial.println("[Setup] Initializing TFT display hardware...");
  tft.init();
  tft.setRotation(1); 
  tft.fillScreen(TFT_BLACK);
  Serial.println("[Setup] TFT hardware command sent.");

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);
  tft.invertDisplay(false);

  tft.setCursor(10, 10);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.println("Waiting Wi-Fi...");

  // DIAGNOSTIC: Tracking Wi-Fi Connection progress
  Serial.printf("[WiFi] Attempting to connect to SSID: %s\n", ssid);
  WiFi.begin(ssid, password);
  int connection_attempts = 0;
  while (WiFi.status() != WL_CONNECTED && connection_attempts < 30) {
    delay(500);
    Serial.print(".");
    connection_attempts++;
  }
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("\n[WiFi] Connection timed out. Check the SSID and password.");
    while (true) delay(1000);
  }

  Serial.println("\n[WiFi] Connected successfully!");
  Serial.print("[WiFi] IP Address assigned: ");
  Serial.println(WiFi.localIP());

  tft.fillScreen(TFT_BLACK);
  tft.setCursor(10, 10);
  tft.println("Connection Succes!");
  tft.print("IP: ");
  tft.println(WiFi.localIP());
  tft.println("\nConnection");
  tft.println("waiting...");

  // DIAGNOSTIC: Initializing WebSocket Server
  Serial.printf("[WS] Starting WebSocket server on port %d...\n", websocket_port);
  webSocket.begin();
  webSocket.onEvent(webSocketEvent);
  httpServer.on("/", HTTP_GET, handleHome);
  httpServer.on("/status", HTTP_GET, handleStatus);
  httpServer.begin();
  fpsWindowStart = millis();
  Serial.printf("[HTTP] Detection status page: http://%s/\n", WiFi.localIP().toString().c_str());
  Serial.println("[Setup] Initialization complete. Loop is active.");
}

void loop() {
  webSocket.loop();
  httpServer.handleClient();
}
