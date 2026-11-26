# Firmware Release Package Contract

**Purpose:** define a stable interface between a firmware repository/release and the Python firmware-management application.

This is a proposed version-1 host-side package format. It does not change the existing UART wire protocol by itself.

## 1. Package layout

Recommended release directory:

```text
release/
  firmware.json
  firmware/
    stm32f103rbt6/
      fw1.bin
      fw2.bin
  checksums/
    SHA256SUMS
  release-notes.md
```

The package should be immutable once published. Use a GitHub Release or a versioned artifact store rather than downloading arbitrary files from a moving branch in production.

## 2. Manifest example

```json
{
  "manifest_version": 1,
  "product": "STM32F103RBT6",
  "release": {
    "version": "1.0.0",
    "channel": "stable",
    "release_date": "2026-11-25",
    "minimum_bootloader_version": "1.0.0"
  },
  "images": [
    {
      "id": "fw1",
      "slot": "FW1",
      "file": "firmware/stm32f103rbt6/fw1.bin",
      "size": 32768,
      "crc32": "89ABCDEF",
      "sha256": "replace-with-64-hex-character-sha256"
    },
    {
      "id": "fw2",
      "slot": "FW2",
      "file": "firmware/stm32f103rbt6/fw2.bin",
      "size": 32768,
      "crc32": "01234567",
      "sha256": "replace-with-64-hex-character-sha256"
    }
  ]
}
```

The example values are illustrative, not real firmware checksums.

## 3. Validation rules

The host application must reject a package unless:
- `manifest_version` is supported.
- Product/MCU identifier matches the connected device profile.
- Release version and channel are valid.
- Slot is exactly FW1 or FW2.
- File path is relative to the package root and cannot escape it.
- File exists and is a regular file.
- File size is greater than zero and no larger than 0xE000 bytes.
- Manifest size equals actual file size.
- CRC-32 equals the bootloader's CRC-32 convention.
- SHA-256 equals the downloaded file's calculated digest.
- The image is not being targeted to the currently active slot.
- Minimum bootloader version is satisfied.

A CRC protects against accidental corruption. SHA-256 identifies the downloaded artifact. Neither proves publisher authenticity without a trusted digital signature.

## 4. Host deployment transaction

1. Resolve a release from an approved repository/channel.
2. Download the manifest and selected image to a temporary workspace.
3. Validate schema, path, size, CRC-32, and SHA-256.
4. Record the release URL, repository, commit/tag, and artifact digest in the deployment log.
5. Connect to the target and query bootloader status.
6. Determine active and inactive slots.
7. Confirm that the requested image is compatible with the target MCU and bootloader.
8. Transfer only to the inactive slot.
9. Track progress and bounded retries.
10. Wait for device-side Flash read-back verification and final success response.
11. Re-query device status and confirm active slot/version.
12. Save an audit record with timestamp, device identifier, package version, image digest, result, and error details.

Do not mark a deployment successful merely because all bytes were written by the host. The device must confirm its own Flash verification and activation result.

## 5. Future protocol evolution

The current UART baseline sends command, size, CRC, then data. A future protocol version should add:
- packet magic and protocol version;
- command and response type;
- sequence number;
- explicit payload length;
- packet CRC;
- device identity and bootloader version response;
- begin/data/finish/abort commands;
- per-block acknowledgement and retry semantics;
- final image verification and activation status.

The host and bootloader must negotiate protocol version before transfer. Do not silently change the existing byte sequence while only one side has been updated.
