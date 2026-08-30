# pfassberg's Custom ESPHome Components Collection

A centralized repository containing a modular collection of custom external components for ESPHome configurations. 

---

# 1. Wi-Fi Management Frame Sniffer (`wifi_sniffer`)

This component enables ESP32 microcontrollers (optimized for chips with native USB like the ESP32-S3, S2, C3, and C6) to drop standard network connections, enter **promiscuous mode**, and capture unencrypted 802.11 Wi-Fi management frames. It stream-parses Beacons, Probe Requests, Disassociations, and Deauthentication packets.

### Features
- **GUI-Driven Range Controls:** Adjust starting and ending channel hopping loops dynamically from the Home Assistant/ESPHome frontend.
- **Dual-Serial Output Split:** Standard ESPHome telemetry stays on the UART0 line, while pure raw frame hex arrays route to the Native Hardware USB (CDC/ACM) interface for seamless ingestion by tools like Node-RED.
- **Unified Remote Reset:** Send a simple text string `reset` over either serial connection at any time to instantly reboot the device back into normal Wi-Fi operational states.

### Repository Tree Structure
To add more items to this collection over time, ensure your folder hierarchy is maintained as follows:
```text
esphome-components/
├── README.md               # This documentation file
└── components/
    ├── wifi_sniffer/       # Component 1: Wi-Fi Packet Sniffer
    │   ├── __init__.py
    │   ├── button.py
    │   ├── number.py
    │   └── wifi_sniffer.h
    └── future_component/   # Component 2: Ready for your next idea!
        └── ...
```

### Installation & Example Configuration

To deploy the sniffer, reference this repository inside your local `.yaml` workspace:

```yaml
external_components:
  - source: github://pfassberg/esphome-components
    components: [ wifi_sniffer ]

wifi_sniffer:

# GUI Setup Fields
number:
  - platform: template
    name: "Start Channel"
    id: start_channel_input
    optimistic: true
    min_value: 1
    max_value: 14
    step: 1
    initial_value: 1

  - platform: template
    name: "End Channel"
    id: end_channel_input
    optimistic: true
    min_value: 1
    max_value: 14
    step: 1
    initial_value: 6

# GUI Implementation Trigger
button:
  - platform: template
    name: "Start Scan"
    id: start_scan_btn
    on_press:
      - lambda: |-
          // Must explicitly map using the esphome::wifi_sniffer namespace layout!
          esphome::wifi_sniffer::start_promiscuous_sniffer(
            (uint8_t)id(start_channel_input).state, 
            (uint8_t)id(end_channel_input).state
          );
```

### Stream Parser Specification
Incoming raw hex frame dumps route to your Native Hardware USB connection on a separate port instance, formatted with clear newline splitters (`\n`) for clean Node-RED array ingestion:

```text
[MGMT][LEN:128][CHAN:6]
48013A01AABBCC112233... (Full raw hexadecimal payload data array string)
```
