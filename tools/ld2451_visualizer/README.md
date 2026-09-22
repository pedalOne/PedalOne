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

The tool first looks for the AE01 write and AE02 notify characteristics used by the vendor Android source, then falls back to FFF3/FFF4 and finally to discovered writable/notifiable characteristics. It logs the complete GATT inventory after connection.

Hi-Link documents the UART framing and configuration commands, but has not published a complete BLE transport/authentication protocol. Some firmware advertises the AE service and accepts notification subscription yet sends no frames until an undocumented authorization/start exchange occurs. If that happens, the log will say that the tool is waiting for notifications. Capturing the official app's first AE01 write on that specific firmware is necessary; the raw log and GATT inventory are included to make that diagnosis clear rather than guessing a potentially destructive command.

Protocol reference: https://make.net.za/wp-content/datasheets/HLK%20LD2451%20Serial%20Communication%20Protocol%20v1.03.pdf
