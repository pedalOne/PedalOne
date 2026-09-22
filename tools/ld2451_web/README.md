# LD2451 Web Radar

A static Web Bluetooth visualizer for the Hi-Link LD2451. It runs in desktop Chrome and in the Bluefy browser on iPhone.

## Use it

The hosted page uses HTTPS, which Web Bluetooth requires. On iPhone, open the URL in Bluefy, tap **Connect HLK radar**, and select the module. If the radar does not advertise an HLK or LD2451 name, use **Show all BLE devices**.

The vendor HLKRadarTool must be fully disconnected first because the radar normally accepts one BLE central at a time. Power-cycle the radar if it remains absent from the device picker.

## Local desktop preview

From the `dist` directory:

```sh
python3 -m http.server 8765
```

Then open `http://localhost:8765` in Chrome. Browsers treat localhost as a secure development context.

## Supported data

The app decodes target angle, range, direction, speed, SNR, alarm state, detection settings, sensitivity settings, and firmware information. It shows all raw notification and acknowledgement bytes in the protocol log and can download target reports as CSV.

The LD2451 vendor demo uses the FFF0 service, FFF2 write characteristic, and FFF1 notification characteristic. AE00/AE01/AE02 and FFF3/FFF4 remain compatibility fallbacks for other firmware variants.
