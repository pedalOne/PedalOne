# LD2451 Radar Console

A Qt desktop visualizer and configuration panel for the Hi-Link HLK-LD2451. The left side draws the radar's ±20° field of view with the rider at the top and up to three live targets below. Each circle shows speed, distance, angle, direction, and SNR. The right side reads and writes the documented detection and sensitivity settings and shows raw BLE traffic. It uses PySide6 and does not require Python's optional Tk module.

## Run

Python 3.10 or newer is recommended.

```sh
cd tools/ld2451_visualizer
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python ld2451_visualizer.py
```

On macOS, allow Bluetooth access when prompted. Power the radar, click **Scan**, select `LD2451_XXXX`, and connect. Only one app can normally hold the BLE connection, so disconnect HLKRadarTool first.

If the radar is absent from the list, force-quit HLKRadarTool and temporarily turn off Bluetooth on nearby phones that have used it, power-cycle the LD2451, and scan again. In macOS **System Settings → Privacy & Security → Bluetooth**, make sure Python or the terminal application used to launch it is enabled. The scan runs for 12 seconds and shows all advertisements; a star marks named LD2451 devices and unnamed devices advertising an AE00/AE30 service.

For a raw scan that bypasses every UI filter:

```sh
.venv/bin/python ble_scan_debug.py --seconds 20
```

Preview the interface without hardware:

```sh
python3 ld2451_visualizer.py --demo
```

Run the protocol checks:

```sh
python3 -m unittest -v test_protocol.py
```

## Data available

Every target report contains a global approach alarm and up to three targets. Each target provides:

- angle, -128° to +127° encoded; the module's specified field is ±20°;
- distance, 0–100 m;
- direction, approaching or moving away;
- speed, 0–120 km/h;
- SNR, 0–255.

The command protocol can read or change maximum range, direction filtering, minimum detected speed, no-target delay, consecutive trigger count, and SNR threshold. It can also read firmware information, change UART baud rate, restore factory settings, and restart the module. This UI deliberately omits baud-rate, factory-reset, and restart buttons because they are easy to trigger accidentally and do not improve normal BLE visualization.

The **Start CSV log** button records timestamped target rows for later analysis.

## BLE compatibility note

The LD2451 vendor demo uses the FFF0 service with FFF2 for writes and FFF1 for notifications. The tool prefers that channel, then falls back to the AE01/AE02 OTA channel and finally to discovered writable/notifiable characteristics. It logs the complete GATT inventory after connection.

The FFF1/FFF2 channel carries the documented UART frames directly. AE00/AE01/AE02 may also appear in the GATT inventory, but Hi-Link's official source assigns that channel to the LD2450/OTA path rather than normal LD2451 target data.

Protocol reference: https://make.net.za/wp-content/datasheets/HLK%20LD2451%20Serial%20Communication%20Protocol%20v1.03.pdf
