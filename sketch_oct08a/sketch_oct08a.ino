#include <TJpg_Decoder.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <TFT_eSPI.h>

const char* ssid = "HONOR X6c";
const char* password = "SabrinoF";
const int websocket_port = 81;

WebSocketsServer webSocket = WebSocketsServer(websocket_port);
TFT_eSPI tft = TFT_eSPI();

bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap)
{
  if ( y >= tft.height() ) return false;

  // DIAGNOSTIC: Confirming the decoded image chunk is being pushed to the TFT hardware
  Serial.printf("[TFT] Pushing image chunk to TFT. X: %d, Y: %d, W: %d, H: %d\n", x, y, w, h);
  tft.pushImage(x, y, w, h, bitmap);

  return true;
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
        } else if (text.length() <= 80 && text.endsWith(" detected")) {
          Serial2.println(text);
          Serial.printf("[UART] Forwarded detection: %s\n", text.c_str());
        }
        break;
      }

    case WStype_BIN:
      if (expected_jpeg_size > 0 && jpeg_buffer_pos + length <= expected_jpeg_size) {
        memcpy(jpeg_buffer + jpeg_buffer_pos, payload, length);
        jpeg_buffer_pos += length;
      }

      if (expected_jpeg_size > 0 && jpeg_buffer_pos >= expected_jpeg_size) {
        // DIAGNOSTIC: Confirming the full frame buffer was assembled and passing to decoder
        Serial.printf("[WS] Full frame received (%d bytes). Handing over to TJpgDec...\n", expected_jpeg_size);
        
        uint32_t decode_start = millis();
        TJpgDec.drawJpg(0, 0, jpeg_buffer, expected_jpeg_size);
        // DIAGNOSTIC: Verifying how long decoding took
        Serial.printf("[TFT] TJpgDec.drawJpg execution completed in %d ms.\n", (millis() - decode_start));

        expected_jpeg_size = 0;
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

  tft.setCursor(10, 10);
  tft.setTextColor(TFT_WHITE);
  tft.setTextSize(2);
  tft.println("Waiting Wi-Fi...");

  // DIAGNOSTIC: Tracking Wi-Fi Connection progress
  Serial.printf("[WiFi] Attempting to connect to SSID: %s\n", ssid);
  WiFi.begin(ssid, password);
  int connection_attempts = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    connection_attempts++;
    if(connection_attempts > 30) { // ~15 seconds timeout
      Serial.println("\n[WiFi] Critical Error: Wi-Fi connection timed out!");
    }
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
  Serial.println("[Setup] Initialization complete. Loop is active.");
}

void loop() {
  webSocket.loop();
}
