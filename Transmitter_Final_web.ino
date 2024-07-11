#include "WiFi.h"
#include "esp_now.h"
#include "driver/i2s.h"

uint8_t receiverMAC[] = {0x08, 0xD1, 0xF9, 0xED, 0xC7, 0x50}; //<- Replace this with the MAC of your other board!


void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_MODE_STA); // Wifi (prerequisite for ESP-Now).

    // Setup ESP-Now first, because I2S uses it.
    Serial.println("Setup ESP-Now...");
    if (ESP_OK != esp_now_init()) {
        Serial.println("esp_now_init: error");
        return;
    }
    esp_now_peer_info_t peerInfo = {0};
    memcpy(peerInfo.peer_addr, receiverMAC, sizeof(receiverMAC));
    // TODO encrypt, by setting peerInfo.lmk.
    if (ESP_OK != esp_now_add_peer(&peerInfo)) {
        Serial.println("esp_now_add_peer: error");
        return;
    }

    // I2S.
    Serial.println("Setup I2S...");
    i2s_config_t i2s_config = {
        .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
        .sample_rate = 11025,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // INMP441 is 24 bits, but it doesn't work if we set 24 bit here.
        .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
        .communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
        .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
        .dma_buf_count = 4,
        .dma_buf_len = ESP_NOW_MAX_DATA_LEN * 4, // * 4 for 32 bit.
        .use_apll = false,
        .tx_desc_auto_clear = false,
        .fixed_mclk = 0,
    };
    if (ESP_OK != i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL)) {
        Serial.println("i2s_driver_install: error");
    }
    i2s_pin_config_t pin_config = {
        .bck_io_num = 14,   // Bit Clock.
        .ws_io_num = 15,    // Word Select.
        .data_out_num = -1,
        .data_in_num = 34,  // Data-out of the mic.
    };
    if (ESP_OK != i2s_set_pin(I2S_NUM_0, &pin_config)) {
        Serial.println("i2s_set_pin: error");
    }
    i2s_zero_dma_buffer(I2S_NUM_0);

    // Print setup done.
    Serial.println("Setup done.");
}

// This is used to scale the audio when things get loud, and gradually increase sensitivity when things go quiet.
#define RESTING_SCALE 127
int32_t scale = RESTING_SCALE;

void loop() {
    // Read from the DAC. This comes in as signed data with an extra byte.
    size_t bytesRead = 0;
    uint8_t buffer32[ESP_NOW_MAX_DATA_LEN * 4] = {0};
    i2s_read(I2S_NUM_0, &buffer32, sizeof(buffer32), &bytesRead, 1000);
    int samplesRead = bytesRead / 4;

    // Convert to 16-bit signed.
    // It's actually 24-bit, but the lowest byte is just noise, even in a quiet room.
    // If we go to 16 bit we don't have to worry about extending a sign byte.
    // Quiet room seems to be values maxing around 7.
    // Max seems around 300 with me at 0.5m distance talking at normal loudness.
    int16_t buffer16[ESP_NOW_MAX_DATA_LEN] = {0};
    for (int i=0; i<samplesRead; i++) {
        // Offset + 0 is always E0 or 00, regardless of the sign of the other bytes,
        // because our mic is only 24-bits, so discard it.
        // Offset + 1 is the LSB of the sample, but is just fuzz, discard it.
        uint8_t mid = buffer32[i * 4 + 2];
        uint8_t msb = buffer32[i * 4 + 3];
        uint16_t raw = (((uint32_t)msb) << 8) + ((uint32_t)mid);
        memcpy(&buffer16[i], &raw, sizeof(raw)); // Copy so sign bits aren't interfered.
    }

    // Find the maximum scale.
    int16_t max = 0;
    for (int i=0; i<samplesRead; i++) {
        int16_t val = buffer16[i];
        if (val < 0) { val = -val; }
        if (val > max) { max = val; }
    }

    // Push up the scale if volume went up.
    if (max > scale) { scale = max; }
    // Gradually drop the scale when things are quiet.
    if (max < scale && scale > RESTING_SCALE) { scale -= 300; }
    if (scale < RESTING_SCALE) { scale = RESTING_SCALE; } // Dropped too far.

    // Scale it to int8s so we aren't transmitting too much data.
    int8_t buffer8[ESP_NOW_MAX_DATA_LEN] = {0};
    for (int i=0; i<samplesRead; i++) {
        int32_t scaled = ((int32_t)buffer16[i]) * 127 / scale;
        if (scaled <= -127) {
            buffer8[i] = -127;
        } else if (scaled >= 127) {
            buffer8[i] = 127;
        } else {
            buffer8[i] = scaled;
        }
    }

    // Send to the other ESP32.
    if (ESP_OK != esp_now_send(NULL, (uint8_t *)buffer8, samplesRead)) {
        Serial.println("Error: esp_now_send");
        delay(500);
    }

    //Print the scaled data for monitoring.
    for (int i = 0; i < samplesRead; i++) {
        Serial.println(buffer8[i]); // Print each sample value
    }
}
