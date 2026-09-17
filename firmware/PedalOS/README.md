# PedalOne

PedalOne is an ESP32-S3 bike computer firmware for the 1.75-inch round AMOLED
display. It combines live ride metrics, onboard and phone GPS, GPX navigation,
ride history, and nearby-rider awareness in a glove-friendly interface.

![PedalOne firmware screen gallery](docs/screens/gallery.png)

[Browse every screen at its native 466 × 466 resolution](docs/screens/README.md).

The gallery is rendered directly from the firmware's drawing functions, fonts,
coordinates, and RGB565 colors. Dynamic fields use stable sample data so the
screens can be regenerated consistently with:

```sh
python3 tools/screen_export/export.py
```
