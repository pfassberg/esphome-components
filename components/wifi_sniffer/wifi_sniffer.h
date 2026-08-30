#pragma once

#include "esphome/core/component.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/usb_serial_jtag.h"

namespace esphome {
namespace wifi_sniffer {

static const char *TAG = "sniffer_core";
extern bool is_scanning;
extern uint8_t min_chan;
extern uint8_t max_chan;
extern uint8_t current_chan;
extern char rx_buffer[32];
extern int rx_idx;

// Declare the class that ESPHome is looking for
class WifiSniffer : public Component {
 public:
  void setup() override {
    // Left empty intentionally since initialization triggers from the button
  }
};

// Raw Frame Packet Handler Callback
inline void wifi_packet_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint16_t len = pkt->rx_ctrl.sig_len; 
    uint8_t *payload = pkt->payload;

    // 1. Stream structured metadata header over Native USB
    char header[64];
    int header_len = snprintf(header, sizeof(header), "[MGMT][LEN:%d][CHAN:%d]\n", len, current_chan);
    usb_serial_jtag_write_bytes((const uint8_t*)header, header_len, portMAX_DELAY);

    // 2. Stream the raw payload as an uncut hex string
    for (uint16_t i = 0; i < len; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", payload[i]);
        usb_serial_jtag_write_bytes((const uint8_t*)hex, 2, portMAX_DELAY);
    }
    usb_serial_jtag_write_bytes((const uint8_t*)"\n", 1, portMAX_DELAY);
}

// Thread-Safe Task handling Channel Hopping & Native USB Serial monitoring
inline void sniffer_worker_task(void *pvParameters) {
    TickType_t last_hop_time = xTaskGetTickCount();
    
    while (is_scanning) {
        // 1. Channel Hopping (Every 300ms)
        if (min_chan != max_chan && (xTaskGetTickCount() - last_hop_time) >= pdMS_TO_TICKS(300)) {
            current_chan++;
            if (current_chan > max_chan) current_chan = min_chan;
            
            esp_wifi_set_channel(current_chan, WIFI_SECOND_CHAN_NONE);
            last_hop_time = xTaskGetTickCount();
        }

        // 2. Listen ONLY on Native USB interface for the "reset" command
        uint8_t usb_data;
        while (usb_serial_jtag_read_bytes(&usb_data, 1, pdMS_TO_TICKS(1)) > 0) {
            if (usb_data == '\n' || usb_data == '\r') {
                rx_buffer[rx_idx] = '\0';
                if (strcmp(rx_buffer, "reset") == 0) {
                    esp_restart(); // Reboots cleanly back to ESPHome Wi-Fi state
                }
                rx_idx = 0; 
            } else if (rx_idx < (int)sizeof(rx_buffer) - 1) {
                rx_buffer[rx_idx++] = usb_data;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    vTaskDelete(NULL);
}

inline void start_promiscuous_sniffer(uint8_t s_chan, uint8_t e_chan) {
    if (is_scanning) return;

    if (s_chan > e_chan) { uint8_t t = s_chan; s_chan = e_chan; e_chan = t; }

    min_chan = s_chan;
    max_chan = e_chan;
    current_chan = s_chan;
    is_scanning = true;

    // Halt ESPHome's normal connected Wi-Fi layers cleanly
    esp_wifi_stop();
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();

    wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&wifi_packet_cb);
    esp_wifi_set_promiscuous(true);
    
    esp_wifi_set_channel(current_chan, WIFI_SECOND_CHAN_NONE);

    xTaskCreatePinnedToCore(sniffer_worker_task, "sniffer_worker", 4096, NULL, 5, NULL, 1);
}

// Instantiate variables safely within context boundary
#ifdef SNIFFER_CORE_MAIN
  bool is_scanning = false;
  uint8_t min_chan = 1;
  uint8_t max_chan = 6;
  uint8_t current_chan = 1;
  char rx_buffer[32];
  int rx_idx = 0;
#else
  // Fallbacks if parsed asynchronously
  #define SNIFFER_CORE_MAIN
  bool is_scanning = false;
  uint8_t min_chan = 1;
  uint8_t max_chan = 6;
  uint8_t current_chan = 1;
  char rx_buffer[32];
  int rx_idx = 0;
#endif

} // namespace wifi_sniffer
} // namespace esphome
