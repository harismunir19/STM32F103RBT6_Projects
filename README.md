# STM32F103RBT6 Dual-Slot Firmware Update Project

A CubeIDE-based STM32F103RBT6 project containing a custom UART bootloader and two application images (FW1 and FW2). This repository is the firmware-side foundation for a host-side firmware management tool.

> **Hardware target:** STM32F103RBT6 (128 KiB Flash, 20 KiB SRAM)  
> **Boot transport:** USART2, 115200 baud, 8-N-1  
> **Application layout:** two 56 KiB slots  
> **Current application clock:** HSI, 8 MHz

## Repository contents

- `Custom_Bootloader/` — STM32CubeIDE bootloader project.
- `FW1_LED/` — application image linked for slot 1.
- `FW2_LED/` — application image linked for slot 2.
- `docs/BOOTLOADER_DESIGN.md` — memory map, boot policy, update transaction and safety requirements.
- `docs/VALIDATION_PLAN.md` — build, protocol, fault-injection and hardware acceptance tests.
- `docs/FIRMWARE_PACKAGE_SPEC.md` — proposed host-side manifest and release package contract.

## Flash memory map

| Region | Start | End (exclusive) | Size |
|---|---:|---:|---:|
| Bootloader | `0x08000000` | `0x08004000` | 16 KiB |
| FW1 slot | `0x08004000` | `0x08012000` | 56 KiB |
| FW2 slot | `0x08012000` | `0x08020000` | 56 KiB |

The two application linker scripts and vector-table offsets are aligned with this map. Do not change the slot boundaries independently in the linker scripts, bootloader, and host tool.

## Current update protocol (implementation baseline)

- USART2 at 115200, 8-N-1.
- Command byte `U` selects FW1; `V` selects FW2.
- Metadata: 4-byte little-endian image length followed by 4-byte little-endian CRC-32.
- CRC convention: reflected CRC-32/ISO-HDLC, compatible with Python `zlib.crc32`.
- ACK `0x06`; NACK `0x15`.
- Data block size: 128 bytes.
- The current transfer is two-pass: receive and verify the stream, then erase/program and verify flash.

See the design and validation documents before changing the protocol or flash layout.

## Important safety status

The existing FW1 and FW2 images are already installed and reported working. Preserve them during development. New metadata-based image validation must support a legacy transition path; do not erase either slot simply to initialize metadata.

The current firmware is a development baseline, not yet a production-qualified secure bootloader. CRC detects accidental corruption; it does **not** authenticate firmware. A production deployment should add signed-image verification, robust power-loss recovery, explicit image confirmation, and a hardware-tested boot handover.

## Development workflow

1. Build all three projects with the same STM32CubeIDE/toolchain configuration.
2. Verify the linker memory map and generated map files.
3. Run host-side protocol unit tests.
4. Run the hardware acceptance tests in `docs/VALIDATION_PLAN.md`.
5. Only deploy to hardware after the bootloader and both application images pass the complete test sequence.

Never test an unverified bootloader change on the only device containing the working firmware images. Keep a known-good ST-Link recovery path available.
