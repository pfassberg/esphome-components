#pragma once

#include "esphome/core/component.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/usb_serial_jtag.h"

namespace esphome {
namespace wifi_sniffer {

static const char *TAG = "sniffer_core";

inline bool is_scanning = false;
inline uint8_t min_chan = 1;
inline uint8_t max_chan = 6;
inline uint8_t current_chan = 1;

#define MAX_FRAME_LEN 512
struct PacketData {
    uint16_t length;
    uint8_t channel;
    uint8_t payload[MAX_FRAME_LEN];
};

inline QueueHandle_t packet_queue = nullptr;
inline char command_rx_buffer[64]; // Explicitly sized command buffer
inline int command_rx_idx = 0;

class WifiSniffer : public Component {
 public:
  void setup() override {}
};

inline void wifi_packet_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT || packet_queue == nullptr) return;

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint16_t len = pkt->rx_ctrl.sig_len;
    if (len > MAX_FRAME_LEN) len = MAX_FRAME_LEN;

    PacketData data;
    data.length = len;
    data.channel = current_chan;
    memcpy(data.payload, pkt->payload, len);

    xQueueSendFromISR(packet_queue, &data, NULL);
}

inline void sniffer_worker_task(void *pvParameters) {
    TickType_t last_hop_time = xTaskGetTickCount();
    PacketData dequeued_pkt;
    
    // EXPLICITLY ALLOCATED FIXED MEMORY LIMITS TO PREVENT STACK CORRUPTION CRASHES
    char output_header[64]; 
    char hex_byte[4];

    while (is_scanning) {
        // 1. Channel Hopping Loop
        if (min_chan != max_chan && (xTaskGetTickCount() - last_hop_time) >= pdMS_TO_TICKS(300)) {
            current_chan++;
            if (current_chan > max_chan) current_chan = min_chan;
            esp_wifi_set_channel(current_chan, WIFI_SECOND_CHAN_NONE);
            last_hop_time = xTaskGetTickCount();
        }

        // 2. Safely Process Buffered Frames
        while (xQueueReceive(packet_queue, &dequeued_pkt, pdMS_TO_TICKS(1)) == pdTRUE) {
            int header_len = snprintf(output_header, sizeof(output_header), 
                                      "[MGMT][LEN:%d][CHAN:%d]\n", 
                                      dequeued_pkt.length, dequeued_pkt.channel);
            usb_serial_jtag_write_bytes((const uint8_t*)output_header, header_len, portMAX_DELAY);

            for (uint16_t i = 0; i < dequeued_pkt.length; i++) {
                snprintf(hex_byte, sizeof(hex_byte), "%02X", dequeued_pkt.payload[i]);
                usb_serial_jtag_write_bytes((const uint8_t*)hex_byte, 2, portMAX_DELAY);
            }
            usb_serial_jtag_write_bytes((const uint8_t*)"\n", 1, portMAX_DELAY);
        }

        // 3. Native USB Serial Input Command Controller
        uint8_t usb_byte;
        while (usb_serial_jtag_read_bytes(&usb_byte, 1, pdMS_TO_TICKS(1)) > 0) {
            if (usb_byte == '\n' || usb_byte == '\r') {
                command_rx_buffer[command_rx_idx] = '\0';
                if (strcmp(command_rx_buffer, "reset") == 0) {
                    esp_restart();
                }
                command_rx_idx = 0; 
            } else if (command_rx_idx < (int)sizeof(command_rx_buffer) - 1) {
                command_rx_buffer[command_rx_idx++] = usb_byte;
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

    packet_queue = xQueueCreate(15, sizeof(PacketData));

    // Put into pure passive listening states directly
    esp_wifi_set_promiscuous(true);
    wifi_promiscuous_filter_t filter = { .filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT };
    esp_wifi_set_promiscuous_filter(&filter);
    esp_wifi_set_promiscuous_rx_cb(&wifi_packet_cb);
    
    esp_wifi_set_channel(current_chan, WIFI_SECOND_CHAN_NONE);

    xTaskCreatePinnedToCore(sniffer_worker_task, "sniffer_worker", 4096, NULL, 5, NULL, 1);
}

} // namespace wifi_sniffer
} // namespace esphome
