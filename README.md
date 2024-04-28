# Overview
This is the code for the input device microcontroller created for my ESET Capstone team's project. Due to the numerous tasks taking place, this code makes use of the microcontroller's (an ESP32) Dual-core functionality. (All Wi-Fi related tasks are performed on Core 0 while all other tasks are performed on Core 1.)

# Microcontroller Goals
- Handling Wi-Fi connection, disconnection, and reconnection
- Handling the timeout and resetting of oneshot timers for sending HTTP Requests
- Data Parsing for info in HTTP Requests
- Handling HTTP Requests and Responses
- Handling ESP-NOW RX and TX between ESP32s
- UART RX and TX between the ESP32 and a touchscreen device
- Handling the ISR for a release button
- Toggling the input device's LED

In order to improve the robustness of the code, numerous redundancies and logging statements were added.

## Notes
This project was programmed in VS Code with the ESP-IDF extension. The device utilized was an ESP32-S3-DevKitC-1-N8R2.