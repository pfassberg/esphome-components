# ESPHome Components by pfassberg

A collection of custom, external components for ESPHome.

## Components Included

### 1. Wi-Fi Sniffer (`wifi_sniffer`)
An ESP32 promiscuous mode Wi-Fi management frame sniffer that captures probe requests and switches channels dynamically based on sliders in your Home Assistant GUI.

#### Usage Example
Add this to your ESPHome YAML:

```yaml
external_components:
  - source: github://pfassberg/esphome-components
    components: [ wifi_sniffer ]

wifi_sniffer:

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

button:
  - platform: template
    name: "Start Scan"
    on_press:
      - lambda: |-
          start_promiscuous_sniffer((uint8_t)id(start_channel_input).state, (uint8_t)id(end_channel_input).state);
```
