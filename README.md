# Overview
This is the code for the input device microcontroller created for my ESET Capstone team's project. Due to the numerous tasks taking place, this code makes use of the microcontroller's (an ESP32) Dual-core functionality. (All Wi-Fi related tasks are performed on Core 0 while all other tasks are performed on Core 1.)

The code for the microncontroller is thoroughly documented and thus I recommend reading through it to get a better understanding of what all is going on. A list of the overall goals of the code/this microcontroller is provided below.

One final thing to note is that in order to improve the robustness of the code, numerous redundancy checks and logging statements were added.

## Microcontroller Goals
- Handling Wi-Fi connection, disconnection, and reconnection
- Handling the timeout and resetting of oneshot timers for sending HTTP Requests
- Parsing data for body info in HTTP Requests
- Handling HTTP Requests and Responses
- Handling ESP-NOW RX and TX between ESP32s
- Handling UART RX and TX between the ESP32 and a touchscreen device
- Handling the ISR for a release button
- Toggling the input device's LED

## Notes
This project was programmed in VS Code with the ESP-IDF extension. The device utilized was an ESP32-S3-DevKitC-1-N8R2.