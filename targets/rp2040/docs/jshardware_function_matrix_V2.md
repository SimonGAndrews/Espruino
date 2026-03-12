# RP2040 jshardware Function Matrix V2

This is the active human-readable RP2040 `jshardware` coverage document.

It combines:

- a short RP2040 implementation summary based on
  `targets/rp2040/jshardware.c`
- the generated `jshardware.h` function matrix snapshot below

The archived raw V1 snapshot lives at:

- `targets/rp2040/docs/archive/jshardware_function_matrix.md`

The CSV remains alongside this document as the raw companion export:

- `targets/rp2040/docs/jshardware_function_matrix.csv`

## Validation Approach

The RP2040 milestones are primarily about building the working Espruino
hardware interface under `src/jshardware.h`.

Validation should normally happen through Espruino APIs that use this
interface, not through a separate unit-test layer for each low-level function.
Direct low-level testing is still useful when debugging unclear or failing API
behaviour.

## RP2040 Status Summary

Milestone 1 remains focused on buildable firmware plus first USB CDC REPL
validation on real Pico hardware.

Already implemented or partly implemented for that stage:

- platform init, reset, idle, and reboot paths
- USB CDC receive/transmit plumbing for `EV_USBSERIAL`
- serial number and USB connection reporting
- system time helpers and microsecond delay
- interrupt off/on handling
- basic GPIO value/state handling
- watch slot bookkeeping and pin/event association helpers
- basic flash page lookup, flash read, and memory-map address translation
- simple watchdog, random number, and clock-reporting placeholders

Still stubbed, minimal, or not yet proven on hardware:

- ADC and fast analog read
- PWM / analog output
- SPI
- I2C
- non-USB UART support
- flash erase/write
- `save()` and Espruino `Storage`
- util timer behaviour
- full watch / interrupt semantics on real GPIO events
- low-power and deeper power-management behaviour
- full pin-function reporting and output-function routing

## Next Hardware Validation Targets

- USB CDC enumeration and Espruino Web IDE REPL
- `digitalWrite`, `digitalRead`, and `pinMode`
- basic `setWatch` behaviour once GPIO events are wired beyond bookkeeping
- flash layout and write safety before persistence work

## How To Use The RP2040 Tracking Columns

The matrix below now includes RP2040-specific tracking columns alongside the
generated cross-target reference data.

Use them as follows:

- `RP2040 Status`
  - set to `implemented`, `implemented (common)`, `partial`, `stubbed`, or
    `not reviewed`
  - use `implemented (common)` where the RP2040 target is currently satisfied by
    shared Espruino hardware-layer support such as `src/jshardware_common.c`
- `Milestone`
  - note the phase where the function matters most, for example `M1`, `M2`,
    `M3`, or `Later`
- `Primary API Test`
  - record the main Espruino API expected to exercise that function during
    validation
  - prefix with `?` where the mapping is a good inferred first guess rather
    than a fully confirmed one
- `Hardware Validated`
  - set to `Yes` only after the behaviour has been proven on real RP2040
    hardware through the intended Espruino-facing test path
  - use `No` until that proof exists
- `Notes`
  - keep this to a short RP2040-specific comment such as `USB CDC only`,
    `common helper`, `returns placeholder value`, or `out of current scope`

Rows can be updated incrementally as implementation and testing progress.
Unreviewed rows currently use placeholder values so this document can act as
the active tracking matrix without losing the generated reference content.

## Generated Matrix Snapshot

# jshardware Function Matrix

Generated from `src/jshardware.h`.

| Function | Uses | STM32 | NRF5x | ESP32 | RP2040 Status | Milestone | Primary API Test | Hardware Validated | Notes |
|---|---:|---|---|---|---|---|---|---|---|
| `jshReset` | 10 | yes | yes | yes | implemented | M1 | `reset()/boot` | No | core lifecycle |
| `jshIdle` | 9 | yes | yes | yes | implemented | M1 | `USB REPL / Web IDE` | No | USB polling path |
| `jshBusyIdle` | 5 |  | yes |  | implemented | M1 | `USB REPL / Web IDE` | No | USB polling path |
| `jshSleep` | 10 | yes | yes | yes | implemented | M1 | `?setDeepSleep / idle` | No | basic sleep only |
| `jshKill` | 15 | yes | yes | yes | implemented | M1 | `?E.kill` | No | no-op cleanup |
| `jshGetSerialNumber` | 9 | yes | yes | yes | implemented | M1 | `?getSerial()` | No | board unique ID |
| `jshIsUSBSERIALConnected` | 13 | yes | yes | yes | implemented | M1 | `USB REPL / Web IDE` | No | TinyUSB CDC state |
| `jshGetSystemTime` | 32 | yes | yes | yes | implemented | M1 | `getTime` | No | uses time_us_64 |
| `jshSetSystemTime` | 10 | yes | yes | yes | implemented | M1 | `setTime` | No | offset from time_us_64 |
| `jshGetTimeFromMilliseconds` | 32 | yes | yes | yes | implemented | M1 | `setTimeout / setInterval` | No | microsecond units |
| `jshGetMillisecondsFromTime` | 19 | yes | yes | yes | implemented | M1 | `getTime / setTimeout` | No | microsecond units |
| `jshInterruptOff` | 20 | yes | yes | yes | implemented | M1 | `setWatch` | No | stacked IRQ state |
| `jshInterruptOn` | 20 | yes | yes | yes | implemented | M1 | `setWatch` | No | stacked IRQ state |
| `jshIsInInterrupt` | 12 | yes | yes | yes | partial | M1 | `setWatch` | No | always false |
| `jshDelayMicroseconds` | 32 | yes | yes | yes | implemented | M1 | `?digitalPulse` | No | busy wait |
| `jshPinSetValue` | 34 | yes | yes | yes | implemented | M1 | `digitalWrite` | No | basic GPIO only |
| `jshPinGetValue` | 29 | yes | yes | yes | implemented | M1 | `digitalRead` | No | basic GPIO only |
| `jshPinSetState` | 29 | yes | yes | yes | implemented | M1 | `pinMode` | No | basic pin modes only |
| `jshPinGetState` | 13 | yes | yes | yes | implemented | M1 | `pinMode` | No | tracked in software |
| `jshIsPinStateDefault` | 5 |  |  | yes | implemented (common) | TBD | `?pinMode` | No | common helper |
| `jshPinAnalog` | 15 | yes | yes | yes | stubbed | M2 | `analogRead` | No | not wired yet |
| `jshPinAnalogFast` | 9 | yes | yes | yes | stubbed | M2 | `?analogRead` | No | not wired yet |
| `jshPinAnalogOutput` | 13 | yes | yes | yes | stubbed | M2 | `analogWrite` | No | not wired yet |
| `jshCanWatch` | 9 | yes | yes | yes | implemented | M1 | `setWatch` | No | bookkeeping only |
| `jshPinWatch` | 15 | yes | yes | yes | implemented | M1 | `setWatch` | No | bookkeeping only |
| `jshGetCurrentPinFunction` | 9 | yes | yes | yes | stubbed | M2 | `?analogWrite / SPI.setup` | No | always JSH_NOTHING |
| `jshSetOutputValue` | 9 | yes | yes | yes | stubbed | M2 | `?analogWrite` | No | no output backend |
| `jshEnableWatchDog` | 10 | yes | yes | yes | partial | TBD | `E.enableWatchdog` | No | no-op placeholder |
| `jshKickWatchDog` | 16 | yes | yes | yes | partial | TBD | `E.kickWatchdog` | No | no-op placeholder |
| `jshKickSoftWatchDog` | 5 |  | yes |  | not reviewed | TBD | `?E.kickWatchdog` | No | review later |
| `jshGetWatchedPinState` | 9 | yes | yes | yes | implemented | M1 | `setWatch` | No | bookkeeping only |
| `jshIsEventForPin` | 10 | yes | yes | yes | implemented | M1 | `setWatch` | No | bookkeeping only |
| `jshIsDeviceInitialised` | 15 | yes | yes | yes | partial | M1 | `USB REPL / Serial/SPI/I2C` | No | USB only |
| `jshUSARTInitInfo` | 8 | yes | yes |  | implemented (common) | M1 | `Serial.setup` | No | common helper |
| `jshUSARTSetup` | 12 | yes | yes | yes | partial | M1 | `Serial.setup` | No | USB path only |
| `jshUSARTUnSetup` | 6 | yes | yes |  | partial | M1 | `?Serial.unsetup` | No | weak default |
| `jshUSARTKick` | 11 | yes | yes | yes | implemented | M1 | `USB REPL / Serial.print` | No | USB CDC only |
| `jshSPIInitInfo` | 13 |  |  |  | implemented (common) | M2 | `SPI.setup` | No | common helper |
| `jshSPISetup` | 19 | yes | yes | yes | stubbed | M2 | `SPI.setup` | No | backend not wired |
| `jshSPISend` | 16 | yes | yes | yes | stubbed | M2 | `SPI.send` | No | returns error |
| `jshSPISend16` | 10 | yes | yes | yes | stubbed | M2 | `?SPI.send` | No | backend not wired |
| `jshSPISet16` | 11 | yes | yes | yes | stubbed | M2 | `?SPI.setup` | No | backend not wired |
| `jshSPISetReceive` | 12 | yes | yes | yes | stubbed | M2 | `?SPI.send` | No | backend not wired |
| `jshSPIWait` | 14 | yes | yes | yes | stubbed | M2 | `SPI.send` | No | backend not wired |
| `jshI2CInitInfo` | 7 |  |  |  | implemented (common) | M2 | `I2C.setup` | No | common helper |
| `jshI2CSetup` | 12 | yes | yes | yes | stubbed | M2 | `I2C.setup` | No | backend not wired |
| `jshI2CUnSetup` | 4 | yes |  |  | partial | M2 | `?I2C.unsetup` | No | weak default |
| `jshI2CWrite` | 12 | yes | yes | yes | stubbed | M2 | `I2C.writeTo` | No | backend not wired |
| `jshI2CRead` | 12 | yes | yes | yes | stubbed | M2 | `I2C.readFrom` | No | fills zeros only |
| `jshFlashGetPage` | 11 | yes | yes | yes | implemented | M1 | `Flash.getPage` | No | 4 KiB page model |
| `jshFlashGetFree` | 10 | yes | yes | yes | partial | M2 | `Flash.getFree` | No | placeholder empty array |
| `jshFlashErasePage` | 13 | yes | yes | yes | stubbed | M2 | `Flash.erasePage` | No | no erase backend |
| `jshFlashErasePages` | 4 |  | yes |  | stubbed | M2 | `?Flash.erasePage` | No | no erase backend |
| `jshFlashRead` | 16 | yes | yes | yes | implemented | M1 | `Flash.read / Storage.read` | No | XIP flash read |
| `jshFlashWrite` | 12 | yes | yes | yes | stubbed | M2 | `Flash.write / Storage.write` | No | no write backend |
| `jshFlashWriteAligned` | 4 |  |  |  | stubbed | M2 | `?Flash.write` | No | depends on stubbed write |
| `jshFlashGetMemMapAddress` | 11 | yes | yes | yes | implemented | M1 | `Flash.read / Storage.read` | No | XIP mapping only |
| `jshUtilTimerStart` | 9 | yes | yes | yes | stubbed | M2 | `setInterval / setTimeout` | No | no util timer backend |
| `jshUtilTimerReschedule` | 9 | yes | yes | yes | stubbed | M2 | `setInterval / setTimeout` | No | no util timer backend |
| `jshUtilTimerDisable` | 10 | yes | yes | yes | stubbed | M2 | `clearInterval / clearTimeout` | No | no util timer backend |
| `jshGetPinAddress` | 5 | yes |  |  | stubbed | Later | `?peek / poke` | No | no RP2040 mapping |
| `jshSetupRTCPrescalerValue` | 3 | yes |  |  | not reviewed | Later | `?E.setClock` | No | out of current scope |
| `jshGetRTCPrescalerValue` | 3 | yes |  |  | not reviewed | Later | `?E.getClock` | No | out of current scope |
| `jshResetRTCTimer` | 3 | yes |  |  | not reviewed | Later | `?E.setClock` | No | out of current scope |
| `jshClearUSBIdleTimeout` | 4 | yes |  |  | not reviewed | Later | `?USB REPL idle` | No | likely unnecessary |
| `jshReadVRef` | 13 | yes | yes | yes | partial | M2 | `E.getAnalogVRef` | No | fixed 3.3V |
| `jshReadVDDH` | 3 |  | yes |  | stubbed | Later | `?E.getBattery` | No | unsupported on RP2040 |
| `jshGetRandomNumber` | 9 | yes | yes | yes | partial | TBD | `Math.random` | No | uses rand() |
| `jshSetSystemClock` | 10 | yes | yes | yes | partial | M1 | `E.setClock` | No | reports current clock |
| `jshGetSystemClock` | 4 | yes |  |  | partial | M1 | `E.getClock` | No | no detailed clock data |
| `jshReboot` | 12 | yes | yes | yes | implemented | M1 | `reset()` | No | watchdog reset |
| `jshVirtualPinSetValue` | 4 |  | yes |  | not reviewed | Later | `?virtual pins` | No | out of current scope |
| `jshVirtualPinGetValue` | 4 |  | yes |  | not reviewed | Later | `?virtual pins` | No | out of current scope |
| `jshVirtualPinGetAnalogValue` | 4 |  | yes |  | not reviewed | Later | `?virtual pins` | No | out of current scope |
| `jshVirtualPinSetState` | 4 |  | yes |  | not reviewed | Later | `?virtual pins` | No | out of current scope |
| `jshVirtualPinGetState` | 4 |  | yes |  | not reviewed | Later | `?virtual pins` | No | out of current scope |

## Full Prototypes

- `void jshReset();`
- `void jshIdle();`
- `void jshBusyIdle();`
- `bool jshSleep(JsSysTime timeUntilWake);`
- `void jshKill();`
- `int jshGetSerialNumber(unsigned char *data, int maxChars);`
- `bool jshIsUSBSERIALConnected();`
- `JsSysTime jshGetSystemTime();`
- `void jshSetSystemTime(JsSysTime time);`
- `JsSysTime jshGetTimeFromMilliseconds(JsVarFloat ms);`
- `JsVarFloat jshGetMillisecondsFromTime(JsSysTime time);`
- `void jshInterruptOff();`
- `void jshInterruptOn();`
- `bool jshIsInInterrupt();`
- `void jshDelayMicroseconds(int microsec);`
- `void jshPinSetValue(Pin pin, bool value);`
- `bool jshPinGetValue(Pin pin);`
- `void jshPinSetState(Pin pin, JshPinState state);`
- `JshPinState jshPinGetState(Pin pin);`
- `bool jshIsPinStateDefault(Pin pin, JshPinState state);`
- `JsVarFloat jshPinAnalog(Pin pin);`
- `int jshPinAnalogFast(Pin pin);`
- `JshPinFunction jshPinAnalogOutput(Pin pin, JsVarFloat value, JsVarFloat freq, JshAnalogOutputFlags flags);`
- `bool jshCanWatch(Pin pin);`
- `IOEventFlags jshPinWatch(Pin pin, bool shouldWatch, JshPinWatchFlags flags);`
- `JshPinFunction jshGetCurrentPinFunction(Pin pin);`
- `void jshSetOutputValue(JshPinFunction func, int value);`
- `void jshEnableWatchDog(JsVarFloat timeout);`
- `void jshKickWatchDog();`
- `void jshKickSoftWatchDog();`
- `bool jshGetWatchedPinState(IOEventFlags device);`
- `bool jshIsEventForPin(IOEventFlags eventFlags, Pin pin);`
- `bool jshIsDeviceInitialised(IOEventFlags device);`
- `void jshUSARTInitInfo(JshUSARTInfo *inf);`
- `void jshUSARTSetup(IOEventFlags device, JshUSARTInfo *inf);`
- `void jshUSARTUnSetup(IOEventFlags device);`
- `void jshUSARTKick(IOEventFlags device);`
- `void jshSPIInitInfo(JshSPIInfo *inf);`
- `void jshSPISetup(IOEventFlags device, JshSPIInfo *inf);`
- `int jshSPISend(IOEventFlags device, int data);`
- `void jshSPISend16(IOEventFlags device, int data);`
- `void jshSPISet16(IOEventFlags device, bool is16);`
- `void jshSPISetReceive(IOEventFlags device, bool isReceive);`
- `void jshSPIWait(IOEventFlags device);`
- `void jshI2CInitInfo(JshI2CInfo *inf);`
- `void jshI2CSetup(IOEventFlags device, JshI2CInfo *inf);`
- `void jshI2CUnSetup(IOEventFlags device);`
- `void jshI2CWrite(IOEventFlags device, unsigned char address, int nBytes, const unsigned char *data, bool sendStop);`
- `void jshI2CRead(IOEventFlags device, unsigned char address, int nBytes, unsigned char *data, bool sendStop);`
- `bool jshFlashGetPage(uint32_t addr, uint32_t *startAddr, uint32_t *pageSize);`
- `JsVar *jshFlashGetFree();`
- `void jshFlashErasePage(uint32_t addr);`
- `bool jshFlashErasePages(uint32_t addr, uint32_t byteLength);`
- `void jshFlashRead(void *buf, uint32_t addr, uint32_t len);`
- `void jshFlashWrite(void *buf, uint32_t addr, uint32_t len);`
- `void jshFlashWriteAligned(void *buf, uint32_t addr, uint32_t len);`
- `size_t jshFlashGetMemMapAddress(size_t ptr);`
- `void jshUtilTimerStart(JsSysTime period);`
- `void jshUtilTimerReschedule(JsSysTime period);`
- `void jshUtilTimerDisable();`
- `volatile uint32_t *jshGetPinAddress(Pin pin, JshGetPinAddressFlags flags);`
- `void jshSetupRTCPrescalerValue(unsigned int prescale);`
- `int jshGetRTCPrescalerValue(bool calibrate);`
- `void jshResetRTCTimer();`
- `void jshClearUSBIdleTimeout();`
- `JsVarFloat jshReadVRef();`
- `JsVarFloat jshReadVDDH();`
- `unsigned int jshGetRandomNumber();`
- `unsigned int jshSetSystemClock(JsVar *options);`
- `JsVar *jshGetSystemClock();`
- `void jshReboot();`
- `void jshVirtualPinSetValue(Pin pin, bool state);`
- `bool jshVirtualPinGetValue(Pin pin);`
- `JsVarFloat jshVirtualPinGetAnalogValue(Pin pin);`
- `void jshVirtualPinSetState(Pin pin, JshPinState state);`
- `JshPinState jshVirtualPinGetState(Pin pin);`
