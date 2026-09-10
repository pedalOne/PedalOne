# Pedal One

Pedal One is an open-source bike-computer project. **PedalOS** is the firmware
that powers it.

This repository currently contains the existing Arduino firmware for the
Waveshare ESP32-S3-Touch-AMOLED-1.75. Hardware design files, enclosure files,
and a companion app have not yet been added.

## Current firmware

The code presently includes:

- a round touch-display interface;
- ride speed, distance, elevation, grade, trip, and odometer views;
- local saved-ride storage on FFat;
- battery status and display brightness controls;
- Bluetooth Low Energy transport for phone-provided location/status data;
- Apple Notification Center Service handling for navigation notifications;
- application-image updates over BLE.

The current firmware is PedalOS v2.1.38. It includes the animated PedalOne
startup identity, filtered ANCS navigation, onboard-GPS and phone-relay support,
GPX route navigation, saved rides, odometer persistence, and BLE
application-image updates.

The matching OTA application image and checksum manifest are in
[`release/v2.1.38/`](release/v2.1.38/).

## Repository layout

```text
PedalOne/
├── firmware/   PedalOS Arduino source
├── hardware/   reserved for hardware design files
├── app/        reserved for companion-app source
├── enclosure/  reserved for enclosure design files
├── docs/       firmware and protocol documentation
├── LICENSE     MIT License
└── README.md
```

## Firmware setup

Open [`firmware/PedalOS/PedalOS.ino`](firmware/PedalOS/PedalOS.ino) in the
Arduino IDE, or compile the same sketch directory with Arduino CLI. Required
headers, build assumptions, and the currently known partition requirement are
listed in [`docs/FIRMWARE.md`](docs/FIRMWARE.md).

The original project did not record exact board-package or library versions.
A locally verified toolchain and board profile are recorded in the firmware
notes as a reproducible starting point.

## Repository hygiene

The public copy intentionally excludes local build caches, editor leftovers,
cloud backup copies, machine-specific Arduino configuration, staging sketches,
and an embedded recovery payload containing private ride/location data.
The firmware directory includes the bitmap-font assets used by the device UI.

Do not commit credentials or private ride exports. The included `.gitignore`
covers common local secret files and embedded build artifacts.

## License

Pedal One's repository code and documentation are released under the
[MIT License](LICENSE). Third-party libraries remain under their respective
licenses.
