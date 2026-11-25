# Bootloader Architecture and Safety Design

**Target:** STM32F103RBT6  
**Status:** Engineering design baseline for review; not a claim that every requirement is already implemented in firmware.

## 1. Design goals

1. Keep the bootloader in a protected 16 KiB region.
2. Maintain two independent 56 KiB application slots.
3. Update the inactive slot so the currently selected application remains available during transfer.
4. Boot the last confirmed application automatically after reset.
5. Recover to the bootloader if no valid application is available.
6. Reject malformed, oversized, truncated, or corrupted images.
7. Make every destructive flash operation occur only after the incoming image has passed its stream-integrity check.

## 2. Flash map

STM32F103RBT6 has 128 KiB internal Flash and 1 KiB erase pages.

| Region | Start address | End exclusive | Length | Purpose |
|---|---:|---:|---:|---|
| Bootloader | 0x08000000 | 0x08004000 | 0x4000 (16 KiB) | Boot/update logic |
| FW1 | 0x08004000 | 0x08012000 | 0xE000 (56 KiB) | Application slot 1 |
| FW2 | 0x08012000 | 0x08020000 | 0xE000 (56 KiB) | Application slot 2 |

All address calculations must use an exclusive end address. The maximum accepted application size is 0xE000 bytes. An image must not cross its slot boundary.

Application project requirements:
- FW1 linker FLASH origin: 0x08004000, length: 56K.
- FW2 linker FLASH origin: 0x08012000, length: 56K.
- FW1 vector-table offset: 0x00004000.
- FW2 vector-table offset: 0x00012000.
- SRAM range: 0x20000000 through 0x20004FFF (20 KiB).

## 3. Boot policy

The intended policy is **automatic boot of the last confirmed image**. There is no user-button-based alternating slot selection.

At reset:

1. Initialize the backup-register access path.
2. Wait for a bounded host connection window (target 2–3 seconds; host and device timeout values must be defined together).
3. If a valid update command is received, service the update transaction.
4. Otherwise, inspect the recorded active slot.
5. Validate the selected image metadata when present, image bounds, vector-table stack pointer, and reset-handler address.
6. Boot the selected valid image.
7. If the selected image is invalid, validate the other slot and boot it if valid.
8. If neither slot is valid, remain in the bootloader and accept a recovery update.

The host connection window is required because an immediate jump can race the desktop application opening the serial port. A future protocol should use a framed request/response handshake rather than relying only on a fixed delay.

### Legacy image migration

Both existing application slots predate persistent per-slot CRC metadata. Do not erase them to force metadata initialization.

During migration:
- Treat a slot with no metadata as a **legacy image**.
- Validate its vector table and address bounds before allowing a jump.
- Do not report it as CRC-verified.
- When a slot is next updated, write its new metadata only after the programmed Flash has been read back and verified.
- If the active-slot record is unset or invalid, use FW1 as the deterministic legacy default, then try FW2 if FW1 fails validation.

This transition policy preserves installed images but is weaker than validating an image against a stored CRC. It is temporary until both slots have been refreshed through the update process.

## 4. Update transaction

The update target must be the inactive slot. The bootloader must reject an update request that targets the currently active slot.

Recommended transaction:

1. **Identify:** host opens USART2 and requests bootloader status.
2. **Select:** host requests FW1 or FW2; bootloader confirms that the target is inactive.
3. **Receive header:** image length, CRC-32, protocol version, and image identifier.
4. **Validate header:** reject zero length, length above 0xE000, unsupported protocol, and invalid target.
5. **Pre-erase verification:** receive the complete image stream and verify CRC before erasing the target slot.
6. **Invalidate target record:** clear the target slot's valid metadata before erase/programming begins.
7. **Erase:** erase only the target slot pages required by the image and ensure stale tail data cannot be treated as part of the image.
8. **Program:** receive the image again and program aligned halfwords. For an odd final byte, pad the final halfword with 0xFF.
9. **Read-back verification:** calculate CRC from the programmed Flash and compare it with the expected CRC.
10. **Commit metadata:** persist image length and CRC, then write the slot-valid marker.
11. **Activate:** update the active-slot record only after successful verification and metadata commit.
12. **Reboot:** report success and reset or wait for an explicit reboot command.

If any step fails before activation, the old active slot remains selected. A failed target slot remains invalid and must never be selected automatically.

### Power-loss behavior

| Failure point | Required outcome |
|---|---|
| Before target invalidation | Existing active slot boots |
| During target erase/program | Existing active slot boots; target is invalid |
| After programming, before metadata commit | Existing active slot boots |
| After metadata commit, before activation | Existing active slot boots; new slot can be validated |
| After activation | New slot is selected; fallback remains available |

The metadata commit order must be designed so a partially written record is never interpreted as valid. Include a version, magic value, and integrity check in the record. Do not rely on a single unprotected flag.

## 5. Backup-register metadata proposal

STM32F1 backup data registers are 16-bit. A possible allocation, subject to checking the exact MCU reference manual and project header definitions, is:

| Register | Proposed use |
|---|---|
| DR1 | Active slot identifier |
| DR2 | FW1 image length |
| DR3–DR4 | FW1 CRC-32 low/high halves |
| DR5 | FW2 image length |
| DR6–DR7 | FW2 CRC-32 low/high halves |
| DR8 | Metadata magic/schema marker |
| DR9–DR10 | Reserved for future record/version or integrity fields |

This allocation is a design proposal, not a validated implementation. Backup-domain reset or loss of backup power can clear these registers. Therefore:
- invalid/missing metadata must trigger safe legacy vector validation, not an unsafe jump;
- the bootloader must never infer that an image is CRC-verified when the record is missing;
- consider storing redundant, sequence-numbered metadata records in a dedicated Flash page for stronger power-loss resilience if backup-domain persistence is insufficient.

## 6. Image validation

For an image in slot `base` with declared `size`:

- Require `1 <= size <= 0xE000`.
- Require `base + size <= slot_end` using overflow-safe arithmetic.
- Read initial MSP from `base + 0`.
- Read reset vector from `base + 4`.
- Require MSP to be within the valid SRAM range and suitably aligned.
- Require the reset vector Thumb bit (bit 0) to be set.
- Clear bit 0 only for the address-range comparison.
- Require the resulting reset-handler address to be inside the selected slot and inside the declared image extent.
- If metadata exists, verify the stored length and CRC against Flash before boot.
- Never jump based only on a non-erased first word.

A CRC is an integrity check, not authenticity. For deployments where malicious firmware is in scope, verify a digital signature using a public key embedded in the bootloader. Do not store a signing private key on the device.

## 7. Safe application handover

The bootloader must hand over control as if the application were starting from reset. Review all of the following in the final implementation:

- Stop SysTick and clear its pending state.
- Disable interrupts during teardown and clear pending NVIC interrupts.
- Disable/deinitialize bootloader-used peripherals and DMA.
- Restore/reset clock configuration to the documented application startup expectation.
- Set SCB->VTOR to the selected application vector-table address.
- Execute data/instruction barriers where required.
- Load the application's initial MSP.
- Branch to the reset handler without returning.
- Do not re-enable interrupts between setting MSP and branching to the reset handler; the application's startup code should establish its own interrupt state.

The handover must be reviewed against the exact Cortex-M3 and STM32F1 CMSIS/HAL implementation. It must be compiled and tested on hardware before release.

## 8. Protocol baseline and known limitations

Current protocol baseline:
- USART2, 115200 baud, 8-N-1.
- `U` = FW1; `V` = FW2.
- 4-byte little-endian size followed by 4-byte little-endian CRC-32.
- 128-byte data blocks.
- ACK = 0x06; NACK = 0x15.
- Two-pass transfer: stream CRC check, then erase/program/read-back CRC.

Known engineering limitations to close before production:
- Add a framed protocol with magic, version, command, sequence number, payload length, and packet CRC.
- Add explicit status/error codes and bounded retries.
- Add command timeout and host/device synchronization.
- Add image identity/version and anti-rollback policy if required.
- Add signed-image authentication if security matters.
- Add trial boot and application confirmation if rollback from a firmware that boots but fails at runtime is required.
- Add watchdog strategy only after defining ownership and refresh behavior across bootloader/application handover.

## 9. Non-goals

This design does not authorize or perform a firmware transfer to physical hardware. Hardware programming requires a separate explicit operator action and a recovery plan.
