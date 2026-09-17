# PedalOne BLE firmware update protocol

Firmware `2.1.0` adds application-image updates over the existing iPhone BLE
connection. The phone downloads and authenticates the image; PedalOne writes
it to the inactive ESP32 OTA partition and selects it only after validation.

The first OTA-enabled build must be installed over USB with the
`app3M_fat9M_16MB` partition scheme. That scheme provides two 3 MiB application
slots. OTA payloads must be ordinary Arduino application `.bin` files, not
merged flash images or filesystem images.

## UUIDs

| Item | UUID | Properties |
| --- | --- | --- |
| OTA service | `8E400010-F315-4F60-9FB8-838830DAEA50` | service |
| Control | `8E400011-F315-4F60-9FB8-838830DAEA50` | write |
| Data | `8E400012-F315-4F60-9FB8-838830DAEA50` | write, write without response |
| Status | `8E400013-F315-4F60-9FB8-838830DAEA50` | read, notify |

Control and status values are UTF-8. Control uses GATT write-with-response.
Firmware `2.1.14` and later uses write-without-response for Data with an 8 KiB
credit window; the device's `PROGRESS` notifications acknowledge durable bytes.

## Update sequence

1. Discover the OTA service and its three characteristics.
2. Enable Status notifications.
3. Write `INFO` to Control. Status returns
   `IDLE <version> <received>/<expected>` or the equivalent `BUSY` response.
4. Download the `.bin` over HTTPS. Verify its byte count and SHA-256 against a
   manifest fetched from a trusted HTTPS URL. Compute the binary's lowercase
   32-character MD5 for the ESP32 transfer check.
5. Write `BEGIN <byte-count> <md5>` to Control.
6. Wait for `READY 0`. `ERROR NOT_SAFE` means a ride is active, or a detected
   battery is below 25% and is not charging.
7. Write sequential binary chunks to Data, using
   `maximumWriteValueLength(for: .withoutResponse)`. Queue while
   `canSendWriteWithoutResponse` is true, but never place more than 8 KiB beyond
   the last device-acknowledged offset in flight.
8. Status reports `PROGRESS <received-bytes>` every 8 KiB and after the final
   chunk. Treat this as a credit grant and send the next window.
9. Write `END` to Control only after `PROGRESS` acknowledges the full image.
10. Wait for `OK REBOOTING`. The device restarts about 1.2 seconds later.

Write `ABORT` to cancel. A BLE disconnect automatically aborts an active write.
Updates do not resume: reconnect and restart from `BEGIN`.

Possible errors are `BAD_COMMAND`, `BAD_BEGIN`, `BUSY`, `NOT_SAFE`,
`NOT_STARTED`, `NO_OTA_SPACE`, `BAD_MD5`, `TOO_MUCH_DATA`, `FLASH_WRITE`,
`SIZE_MISMATCH`, `VERIFY_FAILED`, and `DISCONNECTED`.

## Suggested hosted manifest

```json
{
  "version": "2.1.14",
  "url": "https://github.com/OWNER/REPO/releases/download/v2.1.14/PedalOne-2.1.14.bin",
  "size": 937127,
  "sha256": "64 lowercase hexadecimal characters"
}
```

MD5 protects the device-side transfer against corruption; it does not
authenticate a release. The iOS app should accept only HTTPS redirects and
verify SHA-256 from a manifest URL controlled by the app publisher.
