#include <WiFi.h>
#include <WiFiUdp.h>
#include <driver/i2s.h>

#define RECEIVER_PORT 8080

#define I2S_WS 22 // LRC
#define I2S_SCK 23 // BCLK
#define I2S_SD 25 // DIN

#define BUFFER_LEN 512
int16_t rBuffer[BUFFER_LEN];

WiFiUDP udp;

void setup() {
  Serial.begin(115200);
  
  // Initialize I2S for audio output
  i2s_config_t i2s_config = {
    .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_TX), // Master mode, transmitting data
    .sample_rate = 44100,
    .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
    .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT, // Dual channel
    .communication_format = I2S_COMM_FORMAT_I2S_MSB,
    .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
    .dma_buf_count = 8,
    .dma_buf_len = 64,
    .use_apll = false
  };
  
  i2s_pin_config_t pin_config = {
    .bck_io_num = I2S_SCK,
    .ws_io_num = I2S_WS,
    .data_out_num = I2S_PIN_NO_CHANGE, // Output not needed for mono amp
    .data_in_num = I2S_SD
  };

  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);

  // Connect to WiFi
  connectToWiFi();

  // Start UDP server
  udp.begin(RECEIVER_PORT);

  Serial.println("Ready to receive audio data");
}

void loop() {
  int packetSize = udp.parsePacket();
  if (packetSize) {
    if (packetSize <= BUFFER_LEN * sizeof(int16_t)) {
      udp.readBytes((uint8_t*)rBuffer, packetSize);
      
      // Send the audio data to I2S
      size_t bytesWritten;
      i2s_write(I2S_NUM_0, rBuffer, packetSize, &bytesWritten, portMAX_DELAY);
    } else {
      Serial.println("Received packet too large for buffer");
    }
  }
}

void connectToWiFi() {
  const char* ssid = "YOUR_SSID";
  const char* password = "YOUR_PASSWORD";

  Serial.printf("Connecting to %s ", ssid);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println(" connected");
}