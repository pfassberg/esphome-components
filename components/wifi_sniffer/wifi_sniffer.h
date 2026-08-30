#pragma once
#include "esphome/core/component.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#include "driver/usb_serial_jtag.h"

namespace esphome {
namespace wifi_sniffer {

static const char *TAG = "sniffer_core";
bool is_scanning = false;
uint8_t min_chan = 1;
uint8_t max_chan = 6;
uint8_t current_chan = 1;

char rx_buffer[32];
int rx_idx = 0;

void wifi_packet_cb(void *buf, wifi_promiscuous_pkt_type_t type) {
    if (type != WIFI_PKT_MGMT) return;

    wifi_promiscuous_pkt_t *pkt = (wifi_promiscuous_pkt_t *)buf;
    uint16_t len = pkt->rx_ctrl.sig_len;
    uint8_t *payload = pkt->payload;

    // Format output over Native USB CDC-ACM
    // Header format: [MGMT][LEN:X][CHAN:Y]\n
    char header[32];
    int header_len = snprintf(header, sizeof(header), "[MGMT][LEN:%d][CHAN:%d]\n", len, current_chan);
    usb_serial_jtag_write_bytes((const uint8_t*)header, header_len, portMAX_DELAY);

    // Stream the raw payload bytes as hexadecimal string
    for (uint16_t i = 0; i < len; i++) {
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", payload[i]);
        usb_serial_jtag_write_bytes((const uint8_t*)hex, 2, portMAX_DELAY);
    }
    // End the frame payload entry line
    usb_serial_jtag_write_bytes((const uint8_t*)"\n", 1, portMAX_DELAY);
}

void sniffer_worker_task(void *pvParameters) {
    TickType_t last_hop_time = xTaskGetTickCount();
    
    // Setup regular hardware UART0 configuration to listen for the "reset" command
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
            } else if (rx_idx < sizeof(rx_buffer) - 1) {
                rx_buffer[rx_idx++] = data;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    vTaskDelete(NULL);
}

void start_promiscuous_sniffer(uint8_t s_chan, uint8_t e_chan) {
    if (is_scanning) return;

    if (s_chan > e_chan) {
        uint8_t temp = s_chan; s_chan = e_chan; e_chan = temp;
    }

    min_chan = s_chan;
    max_chan = e_chan;
    current_chan = s_chan;
    is_scanning = true;

    // Initialize native hardware USB Serial JTAG peripheral driver
    usb_serial_jtag_driver_config_t usb_config = {
        .tx_buffer_size = 1024,
        .rx_buffer_size = 512
    };
    usb_serial_jtag_driver_install(&usb_config);

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

class WifiSniffer : public Component {
 public:
  void setup() override {}
};

} // namespace wifi_sniffer
} // namespace esphome
