# PedalOne tools

Utilities and development tools for PedalOne.

## LD2451 radar portal

The browser-based LD2451 visualizer is in [`ld2451_web`](ld2451_web). It connects through Web Bluetooth, displays live targets, exposes the radar settings, and includes a BLE UART target terminal with a receive heartbeat.

Open the hosted portal at <https://pedalone.github.io/PedalOne/>. Use desktop Chrome or the Bluefy browser on iPhone.

The Python/Qt version and its protocol parser are in [`ld2451_visualizer`](ld2451_visualizer).
