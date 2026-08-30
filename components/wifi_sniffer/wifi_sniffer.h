#pragma once
#include "esphome/core/component.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"

static const char *TAG = "sniffer_core";
extern bool is_scanning;
extern uint8_t min_chan;
extern uint8_t max_chan;
extern uint8_t current_chan;

inline bool is_scanning = false;
inline uint8_t min_chan = 1;
inline uint8_t max_chan = 6;
inline uint8_t current_chan = 1;
inline char rx_buffer[32];
inline int rx_idx = 0;

inline void wifi_packet_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;
    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint8_t *payload = pkt->payload;
    uint8_t frame_subtype = (payload[0] & 0xF0) >> 4;

    if (frame_subtype == 0x04) {
        ESP_LOGI(TAG, "[Ch %d] Probe Req from: %02X:%02X:%02X:%02X:%02X:%02X", 
                 current_chan, payload[10], payload[11], payload[12], payload[13], payload[14], payload[15]);
    }
}

inline void sniffer_worker_task(void *pvParameters) {
    TickType_t last_hop_time = xTaskGetTickCount();
    uart_config_t uart_config = {
        .baud_rate = 115200,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE
    };
    uart_param_config(UART_NUM_0, &uart_config);

    while (is_scanning) {
        if (min_chan != max_chan && (xTaskGetTickCount() - last_hop_time) >= pdMS_TO_TICKS(300)) {
            current_chan++;
            if (current_chan > max_chan) current_chan = min_chan;
            esp_wifi_set_channel(current_chan, WIFI_SECOND_CHAN_NONE);
            last_hop_time = xTaskGetTickCount();
        }

        uint8_t data;
        while (uart_read_bytes(UART_NUM_0, &data, 1, pdMS_TO_TICKS(10)) > 0) {
            if (data == '\n' || data == '\r') {
                rx_buffer[rx_idx] = '\0';
                if (strcmp(rx_buffer, "reset") == 0) {
                    ESP_LOGW(TAG, "Reset command received via Serial! Rebooting board...");
                    vTaskDelay(pdMS_TO_TICKS(500));
                    esp_restart(); 
                }
                rx_idx = 0; 
            } else if (rx_idx < (int)sizeof(rx_buffer) - 1) {
                rx_buffer[rx_idx++] = data;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

inline void start_promiscuous_sniffer(uint8_t s_chan, uint8_t e_chan) {
    if (is_scanning) return;
    if (s_chan > e_chan) { uint8_t temp = s_chan; s_chan = e_chan; e_chan = temp; }

    min_chan = s_chan;
    max_chan = e_chan;
    current_chan = s_chan;
    is_scanning = true;

    ESP_LOGW(TAG, "Disconnecting Wi-Fi. Entering Sniffer mode [Channels %d to %d]...", min_chan, max_chan);
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
