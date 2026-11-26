# Build, Test and Hardware Acceptance Plan

This document defines the evidence required before a bootloader or firmware-manager release is called production-ready.

## 1. Build reproducibility

Record the following for every release:
- STM32CubeIDE version.
- GNU Arm Embedded toolchain version.
- STM32CubeF1 HAL/CMSIS package version.
- Build configuration (Debug/Release).
- Compiler/linker flags.
- Git commit SHA.
- Generated ELF, HEX, BIN, MAP, and size report.

Acceptance:
- Clean build of bootloader, FW1, and FW2 succeeds.
- No compiler warnings introduced by project code; every retained warning is reviewed.
- Linker map confirms bootloader and applications occupy only their assigned Flash regions.
- FW1 and FW2 vector-table offsets match their linker origins.
- Maximum application size is enforced at both host and bootloader.

## 2. Host protocol unit tests

The existing host UART protocol suite has previously passed 12 unit tests in simulation. Re-run it after every protocol change. Simulation is not hardware proof.

Required cases:
- Valid connection and configuration.
- Serial port open failure and disconnect.
- Duplicate connection handling.
- Empty image and image larger than 56 KiB.
- Invalid slot identifier.
- Odd and even image lengths.
- NACK response.
- ACK timeout.
- Interrupted transfer.
- Correct CRC and deliberately corrupted CRC.
- Short writes and serial exceptions.
- Device reset/disconnect during each transaction phase.

## 3. Bootloader validation on hardware

Use a current-limited bench supply and keep ST-Link SWD recovery available.

| Test | Procedure | Expected result |
|---|---|---|
| Cold boot | Power-cycle with both known-good apps installed | Last recorded valid app starts |
| Reset boot | Press reset repeatedly | Same active app starts; no slot alternation |
| Host window | Open manager during boot window | Bootloader responds reliably |
| No host | Do not connect manager | Device boots active app after timeout |
| FW1 update | Update inactive FW1 while FW2 is active | FW2 remains bootable until FW1 is verified and activated |
| FW2 update | Update inactive FW2 while FW1 is active | FW1 remains bootable until FW2 is verified and activated |
| Active-slot protection | Request update of active slot | Request rejected before erase |
| Bad CRC | Send image with incorrect CRC | Target is not activated |
| Oversized image | Send length greater than 0xE000 | Rejected before erase |
| Zero-length image | Send size zero | Rejected |
| Truncated transfer | Stop sending during pass 1 | Existing active app remains intact |
| Disconnect during programming | Disconnect after target erase begins | Existing active app remains bootable |
| Read-back mismatch | Inject/induce verification failure | Target metadata remains invalid; old app stays active |
| Invalid vector table | Corrupt test slot vector words | Slot rejected; valid alternate slot used |
| No valid slots | In a controlled recovery test, invalidate both test images | Bootloader remains available over UART |
| Power interruption | Interrupt power at each update phase on a sacrificial test unit | Old active image remains selected unless commit completed |
| Repeated updates | Alternate updates for at least 20 cycles | No slot boundary corruption or metadata drift |

Do not perform destructive tests on the only board containing the user's working FW1/FW2 images. Use a recoverable test unit or retain verified binaries and an ST-Link recovery procedure.

## 4. Application handover tests

- Verify VTOR equals the selected slot base after handover.
- Verify SysTick does not cause unexpected bootloader callbacks.
- Verify USART2 is released and can be initialized by the application if needed.
- Verify all NVIC pending/enabled states are appropriate.
- Verify the application starts with expected clock configuration.
- Verify no interrupt fires using the bootloader's stale vector table.
- Test both FW1 and FW2 repeatedly after cold boot and software reset.

## 5. Reliability and fault injection

Run at least:
- 100 successful update cycles across both slots.
- 100 reset/power-cycle boots.
- 20 interrupted transfers at randomized points.
- 20 invalid/corrupt package attempts.
- 20 host disconnect/reconnect sequences.

Capture UART logs, reset cause, active slot, image size, expected CRC, calculated CRC, and final status for each run.

## 6. Security checks

Before production or remote deployment:
- Verify firmware package signatures, not just CRC.
- Keep signing private keys off the device and out of the repository.
- Define key rotation and revocation.
- Authenticate remote device-management sessions.
- Use TLS for network downloads and APIs.
- Restrict firmware source/release permissions.
- Record audit events for who deployed which image to which device.
- Define anti-rollback requirements and version policy.

## 7. Release gate

A release is accepted only when all of these are true:
- [ ] Clean builds and memory-map checks pass.
- [ ] Host protocol unit tests pass.
- [ ] Hardware boot/update/fallback tests pass.
- [ ] Power-loss tests pass on a sacrificial test unit.
- [ ] Both applications pass repeated boot and runtime checks.
- [ ] Recovery procedure is documented and demonstrated.
- [ ] Release artifacts are versioned and checksummed.
- [ ] Known limitations and test evidence are attached to the release.

A successful unit-test run alone is not sufficient to call the bootloader production-ready.
