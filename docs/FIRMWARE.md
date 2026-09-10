# PedalOS firmware

## Target present in the source

The current sketch identifies its target as the
**Waveshare ESP32-S3-Touch-AMOLED-1.75**, with a 466 × 466 CO5300 display,
CST92xx touch controller, and AXP2101 power-management chip.

## Source layout

The Arduino sketch is in `firmware/PedalOS/`. The main file is named
`PedalOS.ino` to match its sketch directory, as required by Arduino tooling.

## Dependencies visible in the source

- Arduino ESP32 core (`BLE`, `FFat`, `Preferences`, ESP sleep and GPIO APIs)
- Arduino GFX (`Arduino_DataBus`, `Arduino_Canvas`, `Arduino_ESP32QSPI`,
  `Arduino_CO5300`)
- Adafruit GFX
- a library providing `TouchDrvCST92xx.h`
- XPowersLib

The source project does not use a lockfile. The following combination was
compiled successfully on 2026-08-22:

| Component | Version |
| --- | --- |
| Arduino CLI | 1.2.2 |
| ESP32 Arduino core | 3.3.10 |
| Adafruit GFX Library | 1.12.4 |
| GFX Library for Arduino | 1.6.4 |
| SensorLib | 0.3.1 |
| XPowersLib | 0.3.3 |

## Build notes

1. Install an ESP32-S3-capable Arduino board package and the libraries above.
2. Select the generic ESP32-S3 profile with 16 MB flash, OPI PSRAM, hardware
   CDC USB mode, and the `app3M_fat9M_16MB` partition scheme. The Arduino CLI
   profile used for the verified build was:

   ```text
   esp32:esp32:esp32s3:FlashSize=16M,PartitionScheme=app3M_fat9M_16MB,PSRAM=opi,USBMode=hwcdc,CDCOnBoot=cdc,CPUFreq=160
   ```

3. Compile and upload `firmware/PedalOS/PedalOS.ino` over USB for the first
   OTA-enabled installation.

The verified v2.1.38 compile used 1,513,235 bytes of application space and
88,028 bytes of global RAM. Its OTA application image is 1,513,488 bytes,
leaving ample room in the 3 MiB application slot.

## BLE firmware update protocol

See [`OTA_PROTOCOL.md`](OTA_PROTOCOL.md) for the existing service UUIDs,
transfer sequence, validation behavior, and error responses.
