# Pedal One iOS

The app sends Core Location speed/GPS data plus Core Motion relative barometric
altitude to the RAC9000. Protocol v2 packets are 28 bytes. When the phone lacks a
barometer, the firmware automatically falls back to GPS altitude.

Open `PedalOne.xcodeproj`, select the development team, and run on a physical
iPhone. Build number is 2 for the next TestFlight upload.

## Ride history BLE protocol

The app checks for saved rides whenever it discovers the optional ride-control
(`8E400004-F315-4F60-9FB8-838830DAEA50`) and ride-history
(`8E400005-F315-4F60-9FB8-838830DAEA50`) characteristics. Control is write with
response; history is notify. Protocol v1 uses little-endian binary frames:

- App commands: `0x01` request index, `0x02` request ride from byte offset,
  `0x03` acknowledge chunk and next offset.
- RAC9000 events: `0x81` index begin, `0x82` ride summary, `0x83` index end,
  `0x84` ride data, `0x85` ride end, `0xFF` error.
- Each 16-byte log point contains elapsed seconds (`u16`), latitude E7 (`i32`),
  longitude E7 (`i32`), altitude decimeters (`i16`), speed cm/s (`u16`), and
  course degrees x100 (`u16`).
- Every summary supplies its UUID, start time, statistics, point count, byte
  count, and CRC-32. Downloads resume from the last acknowledged byte.

Older RAC9000 firmware without these optional characteristics remains compatible
with live location relay, but cannot provide firmware-only saved rides.

## Device names

The Connect tab's device menu discovers RAC9000 units, switches the active unit,
and renames the connected unit. Names are cached by the Core Bluetooth peripheral
identifier so they survive app launches. Firmware supporting the optional device-
name characteristic (`8E400006-F315-4F60-9FB8-838830DAEA50`) also stores the UTF-8
name in device nonvolatile storage and exposes it with read/write/notify. Names are
1–24 UTF-8 bytes and cannot contain control characters. Older firmware continues
to work with an app-local nickname.

## Ride deletion

The Ride tab supports swipe-to-delete for one ride. Tap **Select** to choose
multiple rides, then use **Delete Selected** to remove them together after a
confirmation prompt.

## Firmware updates

Firmware 2.1.0 and newer can be updated over the existing BLE connection. On
the Connect tab, open the bicycle menu, choose **Firmware Update**, and select
the ordinary Arduino application `.bin` file. The app validates the ESP32 image
type and 3 MiB slot limit, then transfers each chunk with acknowledgement and
shows device-reported errors and progress. Normal GPS writes pause during the
update and resume after the RAC9000 restarts.

The first OTA-capable firmware must be installed over USB using the
`app3M_fat9M_16MB` partition scheme. Do not choose a merged flash image or a
filesystem image.

For verified online updates, set `RAC9000FirmwareManifestURL` in `Info.plist` to
an HTTPS JSON manifest containing `version`, `url`, `size`, and lowercase
`sha256`. The app rejects non-HTTPS downloads and redirects, mismatched byte
counts, invalid ESP32 images, and SHA-256 failures before starting BLE transfer.
