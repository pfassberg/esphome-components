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

inline bool is_scanning = false;
inline uint8_t min_chan = 1;
inline uint8_t max_chan = 6;
inline uint8_t current_chan = 1;
inline char rx_buffer[32]; // Properly allocated fixed array size
inline int rx_idx = 0;

class WifiSniffer : public Component {
 public:
  void setup() override {}
};

inline void wifi_packet_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint16_t len = pkt->rx_ctrl.sig_len; 
    uint8_t *payload = pkt->payload;

    char header[64]; // Allocated array size buffer
    int header_len = snprintf(header, sizeof(header), "[MGMT][LEN:%d][CHAN:%d]\n", len, current_chan);
    usb_serial_jtag_write_bytes((const uint8_t*)header, header_len, portMAX_DELAY);

    for (uint16_t i = 0; i < len; i++) {
        char hex[3]; // Allocated array size for byte hex representation + null terminator
        snprintf(hex, sizeof(hex), "%02X", payload[i]);
        usb_serial_jtag_write_bytes((const uint8_t*)hex, 2, portMAX_DELAY);
    }
    usb_serial_jtag_write_bytes((const uint8_t*)"\n", 1, portMAX_DELAY);
}

inline void sniffer_worker_task(void *pvParameters) {
    TickType_t last_hop_time = xTaskGetTickCount();
    
    while (is_scanning) {
        if (min_chan != max_chan && (xTaskGetTickCount() - last_hop_time) >= pdMS_TO_TICKS(300)) {
            current_chan++;
            if (current_chan > max_chan) current_chan = min_chan;
            esp_wifi_set_channel(current_chan, WIFI_SECOND_CHAN_NONE);
            last_hop_time = xTaskGetTickCount();
        }

        uint8_t usb_data;
        while (usb_serial_jtag_read_bytes(&usb_data, 1, pdMS_TO_TICKS(1)) > 0) {
            if (usb_data == '\n' || usb_data == '\r') {
                rx_buffer[rx_idx] = '\0';
                if (strcmp(rx_buffer, "reset") == 0) {
                    esp_restart();
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

    // Disconnect from the current AP without destroying the core driver
    esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Configure and turn on promiscuous mode directly on the active interface
    wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&wifi_packet_cb);
    esp_wifi_set_promiscuous(true);
    
    esp_wifi_set_channel(current_chan, WIFI_SECOND_CHAN_NONE);

    xTaskCreatePinnedToCore(sniffer_worker_task, "sniffer_worker", 4096, NULL, 5, NULL, 1);
}

} // namespace wifi_sniffer
} // namespace esphome
