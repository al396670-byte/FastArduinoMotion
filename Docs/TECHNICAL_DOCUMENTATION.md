# FastArduinoMotion — Technical Documentation

## 1. Purpose and scope

This document describes the internal architecture and implementation of the FastArduinoMotion system.

The project integrates a custom Arduino-based stepper-motor controller with CODESYS SoftMotion.

The implementation is divided into the following principal components:

1. CODESYS library;
2. CODESYS device descriptions;
3. SoftMotion axis references;
4. shared communication controller;
5. binary communication protocol;
6. Arduino three-axis firmware;
7. deterministic step-generation subsystem.

The current Arduino implementation is:

```text
FastMotionNode3Axis 2.0.1
```

and the CODESYS integration is:

```text
FastArduinoMotion 0.6.3.2
```

The system controls three 28BYJ-48 stepper motors through ULN2003 driver boards.

---

# 2. Global architecture

The complete architecture is:

```text
CODESYS
│
├── Device
│   │
│   ├── Arduino Motion Communication
│   │      │
│   │      └── FB_Arduino3AxisController
│   │
│   └── Application
│
└── SoftMotion General Axis Pool
       ├── Axis1
       ├── Axis2
       └── Axis3

                │
                │ Binary protocol v2
                ▼

              Arduino
              │
              └── FastMotionNode3Axis
                  │
                  ├── Axis 1
                  ├── Axis 2
                  └── Axis 3
```

The communication device and SoftMotion axes are deliberately separated.

`Arduino Motion Communication` is not the parent device of Axis1, Axis2 or Axis3.

The three axes remain independent SoftMotion devices and share the same physical Arduino communication node.

---

# 3. Main software components

The system contains two software domains.

## 3.1 CODESYS domain

The CODESYS domain contains:

```text
FastArduinoMotion library
Device descriptions
SoftMotion axis descriptions
Communication controller
Application program
```

The principal communication block is:

```text
FB_Arduino3AxisController
```

The principal device-level wrapper is:

```text
DEV_ArduinoMotionCommunication
```

## 3.2 Arduino domain

The Arduino domain contains:

```text
FastMotionNode3Axis.h
FastMotionNode3Axis.cpp
FastMotionProtocol3Axis.h
```

and two examples:

```text
01_ThreeAxes_AutonomousTest.ino
02_ThreeAxes_BinaryNode.ino
```

---

# 4. Arduino hardware model

The firmware targets the:

```text
ATmega328P
```

used by Arduino UNO and classic Arduino Nano boards.

Three 28BYJ-48 motors are connected through ULN2003 driver boards.

The fixed pin allocation is:

| Axis | IN1 | IN2 | IN3 | IN4 |
|---|---|---|---|---|
| Axis 1 | D2 | D3 | D4 | D5 |
| Axis 2 | D6 | D7 | D8 | D9 |
| Axis 3 | D10 | D11 | D12 | D13 |

D0 and D1 are reserved for USB serial communication.

The motor drivers should be supplied from an appropriate external 5 V source, with common ground between the Arduino and motor supply.

---

# 5. FastMotionNode3Axis.h

`FastMotionNode3Axis.h` defines the public interface of the three-axis firmware.

The controller exposes:

```text
AXIS_COUNT = 3
TIMER_TICK_HZ = 20000
```

The public interface includes:

```cpp
beginLocal()
beginBinary()
poll()

setEnabled()
setQuickStop()
setVelocityStepsPerSecond()
resetPosition()
stopAll()

positionSteps()
appliedVelocityStepsPerSecond()
axisStatus()
axisError()

communicationActive()
watchdogOk()
acknowledgedSequence()
validCommandCount()
crcErrorCount()
formatErrorCount()
```

The header also defines the runtime configuration.

---

# 6. Configuration structure

The principal configuration parameters are:

```text
baudRate
maxAbsStepRate
watchdogMs
statusPeriodUs
releaseCoilsWhenDisabled
```

The default configuration is:

```text
baudRate                 = 115200
maxAbsStepRate            = 4000
watchdogMs                = 250 ms
statusPeriodUs            = 10000 µs
releaseCoilsWhenDisabled  = true
```

The resulting status transmission period is:

```text
10000 µs = 10 ms
```

or approximately:

```text
100 Hz
```

The watchdog valid range is:

```text
50 ... 5000 ms
```

The maximum absolute velocity is:

```text
4000 steps/s
```

---

# 7. Configuration validation

`beginLocal()` validates the configuration before enabling the controller.

The relevant limits are:

```text
50 ms ≤ watchdog ≤ 5000 ms
0 < maxAbsStepRate ≤ firmware-supported limit
```

The controller then initializes the axis states, outputs and Timer1.

---

# 8. Axis runtime state

Each axis maintains runtime information including:

```text
enabled
quickStop
velocity
appliedVelocity
position
phaseAccumulator
phaseIndex
running
```

The state is maintained independently for all three axes.

The communication subsystem can update these states while the Timer1 interrupt consumes them for deterministic motion generation.

---

# 9. Half-step sequence

The motor excitation uses eight half-step states:

```text
0b0001
0b0011
0b0010
0b0110
0b0100
0b1100
0b1000
0b1001
```

The phase index advances cyclically.

The direction determines whether the phase index advances or decrements.

This provides the electrical sequence required by the 28BYJ-48 motors in half-step operation.

---

# 10. Fast pin handling

The firmware initializes the motor pins and stores the corresponding AVR port registers and masks.

The step-generation ISR uses direct port manipulation rather than relying on high-level Arduino digital I/O calls for every step.

This reduces execution overhead inside the timer interrupt.

The pin configuration is fixed at compile time:

```text
Axis 1 → D2-D5
Axis 2 → D6-D9
Axis 3 → D10-D13
```

---

# 11. Initialization

`beginLocal()` performs the local controller initialization.

The main sequence is:

```text
1. Validate configuration.
2. Reset axis runtime states.
3. Stop all axes.
4. Configure motor pins.
5. Configure Timer1.
6. Initialize communication state.
```

`beginBinary()` extends this initialization by enabling binary communication and starting the serial interface.

The binary serial interface uses:

```text
115200 baud
```

---

# 12. Timer1 configuration

Timer1 is configured in CTC mode with a prescaler of 8.

The resulting interrupt frequency is:

```text
20 kHz
```

Therefore the interrupt period is:

```text
1 / 20000 = 50 µs
```

Every timer interrupt services all three axes.

Conceptually:

```text
Timer1 ISR
│
├── service Axis 1
├── service Axis 2
└── service Axis 3
```

---

# 13. Real-time motion generation

The firmware does not generate one motor step on every timer interrupt.

Instead, each axis uses a phase accumulator.

For a requested velocity:

```text
v [steps/s]
```

the accumulator is updated at:

```text
20000 updates/s
```

A step is generated whenever the accumulated magnitude reaches the timer frequency threshold.

This allows fractional step rates while maintaining a deterministic timer-driven process.

The resulting motor timing is therefore independent of the execution time of:

```text
loop()
Serial processing
USB communication
CODESYS task scheduling
```

---

# 14. Enable state

An axis must be enabled to produce motion.

When disabled:

```text
velocity = 0
running = false
phase accumulator = 0
```

If:

```text
releaseCoilsWhenDisabled = true
```

the motor outputs are released.

When the axis is enabled again, the initial excitation state is established before motion resumes.

---

# 15. Quick-stop state

Quick-stop forces the axis to stop motion.

The firmware maintains the distinction between:

```text
enabled
quickStop
running
```

Therefore an axis can be:

```text
enabled + stopped
```

without necessarily releasing its coils.

This permits a stopped motor to retain holding torque.

---

# 16. Velocity limiting

The firmware applies an absolute velocity limit.

The default limit is:

```text
±4000 steps/s
```

If a command requests a value outside this range, the value is clamped.

The controller records the condition as:

```text
VELOCITY_CLAMPED
```

This condition is not equivalent to a communication fault.

It indicates that the requested velocity was outside the configured firmware range.

---

# 17. Position handling

The firmware maintains an integer step position for each axis.

The position is updated whenever a half-step transition occurs.

The position is therefore an estimated motor position derived from the commanded step sequence rather than from a physical encoder.

This is represented in the status information by:

```text
POSITION_ESTIMATED
```

There is no physical position sensor in the described hardware.

---

# 18. Atomic access

The Arduino firmware shares axis state between:

```text
main execution context
Timer1 ISR
```

Access to shared state is therefore protected using AVR atomic sections where necessary.

This prevents inconsistent multi-byte reads and writes while the timer interrupt can modify the same state.

---

# 19. FastMotionProtocol3Axis.h

`FastMotionProtocol3Axis.h` defines the binary protocol constants and serialization functions.

The protocol version is:

```text
2
```

The frame types are:

```text
Command = 1
Status  = 2
```

The frame sizes are:

```text
Command = 28 bytes
Status  = 48 bytes
```

The synchronization bytes are:

```text
Command: A5 5A
Status:  5A A5
```

The protocol uses:

```text
little-endian
CRC16/Modbus
```

---

# 20. Protocol frame structure

Two fixed-size frames are exchanged.

```text
PLC ────────────────> Arduino
        28 bytes

Arduino ────────────> PLC
        48 bytes
```

The fixed sizes simplify synchronization and validation.

A frame is not accepted merely because its header is correct.

The implementation also checks:

```text
protocol version
frame type
CRC
```

---

# 21. Command frame

The PLC-to-Arduino command frame is:

| Offset | Field | Type |
|---|---|---|
| 0-1 | Header | bytes |
| 2 | Version | BYTE |
| 3 | Type | BYTE |
| 4-5 | Sequence | UINT |
| 6-7 | Watchdog | UINT |
| 8-9 | Enable mask | WORD |
| 10-11 | Quick-stop mask | WORD |
| 12-13 | Reset-position mask | WORD |
| 14-17 | Axis 1 velocity | DINT |
| 18-21 | Axis 2 velocity | DINT |
| 22-25 | Axis 3 velocity | DINT |
| 26-27 | CRC | UINT |

The CRC covers:

```text
bytes 0 ... 25
```

---

# 22. Status frame

The Arduino-to-PLC status frame is:

| Offset | Field | Type |
|---|---|---|
| 0-1 | Header | bytes |
| 2 | Version | BYTE |
| 3 | Type | BYTE |
| 4-5 | Acknowledged sequence | UINT |
| 6-7 | Node statusword | WORD |
| 8-9 | Node error | UINT |
| 10-11 | Ready mask | WORD |
| 12-13 | Error mask | WORD |
| 14-17 | Axis 1 position | DINT |
| 18-21 | Axis 2 position | DINT |
| 22-25 | Axis 3 position | DINT |
| 26-29 | Axis 1 applied velocity | DINT |
| 30-33 | Axis 2 applied velocity | DINT |
| 34-37 | Axis 3 applied velocity | DINT |
| 38-39 | Axis 1 statusword | WORD |
| 40-41 | Axis 2 statusword | WORD |
| 42-43 | Axis 3 statusword | WORD |
| 44-45 | Command age | UINT |
| 46-47 | CRC | UINT |

The CRC covers:

```text
bytes 0 ... 45
```

---

# 23. Endianness and CRC

Multi-byte integer values are serialized little-endian.

The protocol provides helpers for:

```text
readU16LE
writeU16LE
readI32LE
writeI32LE
```

The CRC algorithm is:

```text
CRC16/Modbus
```

The CRC is calculated over the complete frame excluding its final two CRC bytes.

---

# 24. Axis masks

The protocol defines:

```text
Axis 1 = 0x0001
Axis 2 = 0x0002
Axis 3 = 0x0004
```

The valid combined mask is:

```text
0x0007
```

The masks are used for:

```text
enable
quick-stop
reset-position
```

An invalid or reserved mask can generate:

```text
Error code 6
```

---

# 25. Node status

The node statusword defines:

```text
LINK_ACTIVE        = 0x0001
WATCHDOG_OK        = 0x0002
PROTOCOL_READY     = 0x0004
ANY_AXIS_RUNNING   = 0x0008
POSITION_ESTIMATED = 0x0010
```

These flags describe the global state of the Arduino node.

---

# 26. Axis statusword

Each axis has its own statusword.

The defined flags are:

```text
ENABLED            = 0x0001
RUNNING            = 0x0002
POSITION_ESTIMATED = 0x0004
VELOCITY_CLAMPED   = 0x0008
QUICK_STOP         = 0x0010
FAULT              = 0x0020
```

The velocity-clamped state is not intrinsically a fault.

The implementation distinguishes a velocity-limit warning from a genuine axis fault.

---

# 27. Error codes

The protocol defines:

| Code | Meaning |
|---|---|
| 0 | No error |
| 1 | Bad CRC |
| 2 | Bad protocol version |
| 3 | Bad frame type |
| 4 | Watchdog |
| 5 | Velocity clamped to ±4000 steps/s |
| 6 | Invalid/reserved mask |

Codes 4 and 5 must not be interpreted identically to a malformed communication frame.

In particular, a watchdog timeout represents a temporary communication-safety condition.

---

# 28. Serial parser

The Arduino parser searches for:

```text
A5 5A
```

as the command synchronization sequence.

Once synchronized, it collects the remaining bytes required to form a 28-byte frame.

The frame is only accepted after the complete frame has been received and validated.

This prevents arbitrary serial data from being interpreted as a command.

---

# 29. Command validation

`acceptCommandFrame()` validates:

1. CRC;
2. protocol version;
3. frame type;
4. sequence information;
5. watchdog;
6. axis masks;
7. velocity values.

Only a valid command updates the operational communication state.

For a valid frame, the firmware:

```text
updates command sequence
updates watchdog state
applies axis commands
clears the current node communication error
marks status as pending
increments valid-command counter
```

---

# 30. Sequence tracking

Each command contains a sequence number.

The Arduino returns the acknowledged sequence in the status frame.

This allows the communication controller to associate received status with transmitted commands.

The sequence mechanism is separate from the watchdog mechanism.

The watchdog indicates whether commands continue to arrive within the expected time.

The sequence number identifies the command being acknowledged.

---

# 31. Watchdog implementation

The default watchdog is:

```text
250 ms
```

The firmware accepts watchdog values within:

```text
50 ... 5000 ms
```

The watchdog is refreshed by valid command frames.

If no valid command arrives before the watchdog expires:

```text
watchdogOk = false
node error = WATCHDOG
```

and a safe stop is performed.

---

# 32. Watchdog safe stop

When the watchdog expires, the firmware executes a safe stop.

For every axis:

```text
velocity = 0
appliedVelocity = 0
running = false
quickStop = true
phaseAccumulator = 0
```

The outputs are disabled according to the configured coil-release behavior.

The important design characteristic is that the watchdog does not continuously enqueue new unsolicited status frames.

The implementation explicitly clears:

```text
statusPending_
```

when the watchdog expires.

This prevents old watchdog frames from accumulating while the PLC is stopped or disconnected.

---

# 33. Watchdog reconnection behavior

When communication resumes, the next valid command is processed normally.

A valid command:

```text
refreshes the watchdog
clears the watchdog communication error
updates the sequence
updates the axes
```

This is particularly relevant to the CODESYS:

```text
Stop → Run
```

transition.

The firmware does not deliberately maintain a queue of obsolete watchdog status frames that could be received after communication has already recovered.

Therefore the communication recovery mechanism is based on the current valid command rather than stale serial data.

---

# 34. Status generation

The Arduino generates status frames when:

```text
a status is pending
```

or when:

```text
the periodic status interval has elapsed
```

Before transmitting a complete 48-byte status frame, the implementation checks that sufficient serial transmit buffer space is available.

This prevents partial transmission caused by insufficient buffer capacity.

---

# 35. Status construction

The status frame contains:

```text
node status
node error
ready mask
error mask

Axis 1:
    position
    applied velocity
    statusword

Axis 2:
    position
    applied velocity
    statusword

Axis 3:
    position
    applied velocity
    statusword

age of last valid command
CRC
```

The command age provides an indication of how long it has been since the last valid command.

---

# 36. Arduino public control interface

The local API permits direct control without the binary protocol.

The main operations are:

```text
setEnabled()
setQuickStop()
setVelocityStepsPerSecond()
resetPosition()
stopAll()
```

This interface is used by the autonomous test.

The binary communication example uses the same underlying controller but receives commands through the serial protocol.

---

# 37. Autonomous example

`01_ThreeAxes_AutonomousTest.ino` provides a hardware test independent of CODESYS.

The program executes six phases.

```text
Phase 0 → Axis 1 +200 steps/s
Phase 1 → Axis 2 +200 steps/s
Phase 2 → Axis 3 +200 steps/s
Phase 3 → Axis1 +200, Axis2 -120, Axis3 +60
Phase 4 → zero velocity, coils energized
Phase 5 → disabled, coils released
```

Each phase lasts approximately:

```text
3000 ms
```

The program uses `millis()` and does not block the controller with long delays.

---

# 38. Binary node example

`02_ThreeAxes_BinaryNode.ino` configures:

```text
baudRate = 115200
maxAbsStepRate = 4000
watchdogMs = 250
statusPeriodUs = 10000
releaseCoilsWhenDisabled = true
```

It then executes:

```text
node.beginBinary(...)
```

and continuously calls:

```text
node.poll()
```

This example is the firmware used for CODESYS communication.

---

# 39. Arduino package 2.0.1

The Arduino package identifies itself as:

```text
FastMotionNode3Axis
Version 2.0.1
```

The 2.0.1 implementation is a self-contained three-axis package.

It preserves:

```text
protocol version 2
28-byte command frame
48-byte status frame
```

and explicitly documents the byte offsets of all protocol fields.

The package also contains both:

```text
autonomous example
binary communication example
```

---

# 40. CODESYS library architecture

The CODESYS communication layer introduces a dedicated device:

```text
DEV_ArduinoMotionCommunication
```

Its purpose is to own the shared Arduino communication.

The device exposes diagnostic outputs but does not replace the SoftMotion axis references.

The internal controller is:

```text
FB_Arduino3AxisController
```

---

# 41. DEV_ArduinoMotionCommunication declaration

The device declaration exposes:

```text
xReady
xCommunicationValid
xProtocolReady
xNodeWatchdogOk
xNodeWarning
xNodeFault

uiComPortNumber

udiBaudRate
uiWatchdogMs

uiNodeError
wReadyMask
wErrorMask

udiTxFrameCount
udiRxFrameCount
udiRxCrcErrorCount
udiRxFormatErrorCount
udiAutoReconnectCount
```

The internal variable is:

```text
fbArduinoController : FB_Arduino3AxisController
```

This establishes a single communication owner for the Arduino node.

---

# 42. Device initialization

The initialization method reads the COM-port configuration from the device connector.

The parameter is:

```text
IoStandard parameter ID 20000
```

The default value is:

```text
3
```

The valid range is:

```text
1 ... 999
```

The communication parameters are fixed internally to:

```text
115200 baud
250 ms watchdog
```

The initialization method returns:

```text
0
```

on completion.

---

# 43. Cyclic communication

The cyclic method is:

```text
AfterReadInputs
```

It executes the shared controller once per bus cycle.

The central call is conceptually:

```text
fbArduinoController(
    xRun := TRUE,
    uiComPortNumber := uiComPortNumber,
    uiArduinoWatchdogMs := uiWatchdogMs
)
```

The communication diagnostics are then copied to the device outputs.

This architecture prevents the three axes from opening independent serial communication channels.

---

# 44. Communication ownership

The communication ownership model is:

```text
DEV_ArduinoMotionCommunication
            │
            ▼
FB_Arduino3AxisController
            │
            ▼
       Serial port
```

The axes do not independently own the serial interface.

This is essential because only one component should manage the physical serial connection to the Arduino.

---

# 45. Device exit

`FB_Exit` explicitly stops the communication controller.

The controller is called with:

```text
xRun := FALSE
```

The device then clears its communication-ready outputs.

This ensures that the serial owner is explicitly closed when the device lifecycle ends.

---

# 46. Device reinitialization

`FB_Reinit` clears communication and fault outputs and allows the device to re-enter its normal initialization lifecycle.

The reconnection behavior is therefore handled at the communication-device level rather than by the application program.

---

# 47. Device description

The communication device description is:

```text
ArduinoMotionCommunication_0_6_3_2.devdesc.xml
```

Its principal characteristics are:

```text
Device type: 8000
Version: 0.6.3.2
Connector: Common.PCI
```

The device identifier is:

```text
FFFF A652
```

The device exposes parameter:

```text
20000
```

for the COM-port numeric value.

Its function block instance is:

```text
DEV_ArduinoMotionCommunication
```

and its cyclic method is:

```text
AfterReadInputs
```

---

# 48. SoftMotion axis descriptions

Three device descriptions are provided:

```text
ArduinoStepperAxis1_0_6_3_2.devdesc.xml
ArduinoStepperAxis2_0_6_3_2.devdesc.xml
ArduinoStepperAxis3_0_6_3_2.devdesc.xml
```

They represent:

```text
Axis 1 → Arduino channel 1
Axis 2 → Arduino channel 2
Axis 3 → Arduino channel 3
```

The three descriptions are structurally equivalent.

Their principal differences are:

```text
axis name
axis identifier
channel mapping
AXIS_REF instance name
```

---

# 49. SoftMotion axis hierarchy

The device descriptions use:

```text
Common.SoftMotion.Logical
```

as the logical parent connector and:

```text
Common.SoftMotion.General
```

for the axis.

The intended CODESYS hierarchy is:

```text
SoftMotion General Axis Pool
│
├── Arduino Stepper Axis 1
├── Arduino Stepper Axis 2
└── Arduino Stepper Axis 3
```

The axes remain independent from:

```text
Arduino Motion Communication
```

at the device-tree level.

---

# 50. Axis scaling

The axis descriptions implement:

```text
Denominator = 4096
Numerator   = 360
```

which represents:

```text
4096 step events = 360°
```

This is the conversion used between the SoftMotion engineering-unit representation and the Arduino step representation.

The firmware itself continues to operate in:

```text
steps
steps/s
```

---

# 51. Axis limits and SoftMotion parameters

The axis descriptions specify parameters including:

```text
encoder bit width = 32
Kp = 0
maximum following error = 0
following-error check = false
```

The supplied velocity-related values include:

```text
±1000
±87.890625
```

depending on the corresponding SoftMotion representation.

The standard parameter configuration additionally defines:

```text
maximum velocity = 40
maximum acceleration = 100
maximum deceleration = 100
maximum jerk = 1000
```

and the software position range:

```text
0 ... 360°
```

with software-limit checking disabled by default.

---

# 52. StandardParameters.xml

The standard parameter file defines the parameters used by the SoftMotion axis references.

Important entries are:

```text
1021  wDriveID
1040  bVirtual
1060  iMovementType
1061  fPositionPeriod
1062  eRampType

1113  maximum velocity
1123  maximum acceleration
1133  maximum deceleration
1143  maximum jerk
1144  ramp jerk

1200  positive software limit
1201  negative software limit
1205  software-limit enable

1207  position-lag check
1208  maximum position lag

1250  maximum error distance
```

The position period is:

```text
360°
```

---

# 53. Ramp parameter

Parameter:

```text
1062
```

defines the motion ramp type.

The supported values are:

```text
0 = trapez
1 = sinsquare
2 = quadratic_ramp
3 = quadratic_smooth_ramp
```

The supplied configuration uses:

```text
0 = trapezoidal
```

The parameter is therefore part of the SoftMotion configuration and not an Arduino firmware parameter.

---

# 54. PLC application architecture

The application program contains SoftMotion logic only.

The principal function blocks are:

```text
MC_Power
MC_MoveAbsolute
MC_Halt
MC_Reset
```

for each of the three axes.

The communication block:

```text
FB_Arduino3AxisController
```

is not directly instantiated by the application.

---

# 55. MC_Power

`MC_Power` controls the logical power state of each SoftMotion axis.

The application maps:

```text
fbPower1 → Axis1
fbPower2 → Axis2
fbPower3 → Axis3
```

The communication subsystem translates the resulting axis state into the corresponding Arduino enable mask.

---

# 56. MC_MoveAbsolute

`MC_MoveAbsolute` generates absolute position commands in SoftMotion engineering units.

The example application uses:

```text
Axis1 = 90°
Axis2 = -90°
Axis3 = 45°
```

with velocities:

```text
Axis1 = 30°/s
Axis2 = 20°/s
Axis3 = 15°/s
```

and:

```text
Acceleration = 60°/s²
Deceleration = 60°/s²
```

The SoftMotion axis abstraction hides the binary protocol from the application.

---

# 57. MC_Halt

`MC_Halt` provides the application-level controlled stopping function.

The resulting axis state is translated by the SoftMotion and communication layers into the corresponding velocity/stop command.

This is distinct from the communication watchdog.

The watchdog exists to stop the motors when valid commands cease arriving.

---

# 58. MC_Reset

`MC_Reset` is used to reset an axis-level error condition according to the SoftMotion state machine.

It is not a replacement for communication reconnection.

Communication recovery is managed by:

```text
Arduino Motion Communication
```

and the shared controller.

---

# 59. Axis 2 and Axis 3

Axis2 and Axis3 use the same implementation structure as Axis1.

The differences are limited to their respective:

```text
axis reference
device description
channel number
name
pin allocation
```

The mapping is:

```text
Axis1 → channel 1 → D2-D5
Axis2 → channel 2 → D6-D9
Axis3 → channel 3 → D10-D13
```

The same SoftMotion function-block architecture can therefore be applied to all three axes.

---

# 60. Application communication abstraction

The application sees:

```text
Axis1
Axis2
Axis3
```

rather than:

```text
serial port
binary frames
CRC
watchdog
Arduino registers
```

This is one of the principal architectural objectives of FastArduinoMotion.

The communication implementation is below the application layer.

---

# 61. Data flow: CODESYS → Arduino

The command flow is:

```text
SoftMotion
    │
    ▼
Axis state
    │
    ▼
FB_Arduino3AxisController
    │
    ▼
28-byte command frame
    │
    ▼
Serial
    │
    ▼
Arduino parser
    │
    ▼
Axis runtime state
```

The command contains:

```text
sequence
watchdog
enable mask
quick-stop mask
reset-position mask
velocity Axis1
velocity Axis2
velocity Axis3
```

---

# 62. Data flow: Arduino → CODESYS

The status flow is:

```text
Timer / axis state
       │
       ▼
48-byte status frame
       │
       ▼
Serial
       │
       ▼
FB_Arduino3AxisController
       │
       ▼
CODESYS communication diagnostics
       │
       ▼
SoftMotion axis state
```

The status contains:

```text
acknowledged sequence
node status
node error
ready mask
error mask
position Axis1
position Axis2
position Axis3
applied velocity Axis1
applied velocity Axis2
applied velocity Axis3
statusword Axis1
statusword Axis2
statusword Axis3
command age
```

---

# 63. Diagnostics

The communication device provides counters and state information.

Important diagnostic variables are:

```text
xReady
xCommunicationValid
xProtocolReady
xNodeWatchdogOk
xNodeWarning
xNodeFault

uiNodeError
wReadyMask
wErrorMask

udiTxFrameCount
udiRxFrameCount
udiRxCrcErrorCount
udiRxFormatErrorCount
udiAutoReconnectCount
```

These diagnostics distinguish communication health from motor-axis state.

---

# 64. Communication test

A communication test can be performed before executing motion.

The expected normal state is:

```text
xReady = TRUE
xCommunicationValid = TRUE
xProtocolReady = TRUE
xNodeWatchdogOk = TRUE
```

The following counters should remain zero during normal operation:

```text
udiRxCrcErrorCount
udiRxFormatErrorCount
```

The COM-port test consists of intentionally selecting an incorrect port, observing loss of communication, and then restoring the correct port.

The expected result is automatic recovery.

---

# 65. Relationship between watchdogs

Two concepts must be distinguished.

## Arduino watchdog

The Arduino watchdog detects the absence of valid commands.

Its default value is:

```text
250 ms
```

Its purpose is motor safety.

## CODESYS communication state

The CODESYS communication layer determines whether the serial link and protocol communication are currently valid.

These two mechanisms are related but not identical.

The Arduino watchdog must not be interpreted as an instruction to permanently latch a CODESYS external error.

---

# 66. Error propagation

The protocol defines:

```text
WATCHDOG
```

as error code 4.

This indicates that the Arduino has stopped receiving valid commands within the configured watchdog period.

The firmware then performs a safe stop.

When communication resumes, a valid command clears the current watchdog communication condition.

The design intentionally avoids turning stale watchdog status information into a permanent fault after communication has recovered.

This behavior is particularly important during CODESYS lifecycle transitions.

---

# 67. Timing architecture

The system contains two fundamentally different timing domains.

### Deterministic domain

```text
Timer1
20 kHz
50 µs period
step generation
```

### Non-deterministic domain

```text
USB
UART
Serial processing
CODESYS task scheduling
Windows
```

The motor step timing is therefore not directly coupled to the communication timing.

---

# 68. Separation of deterministic and non-deterministic processing

The Arduino `loop()` performs:

```text
serial parsing
watchdog service
status transmission
```

The Timer1 ISR performs:

```text
axis step generation
```

This separation is intentional.

A delay in serial processing does not directly change the timer interrupt frequency.

Similarly, Windows or CODESYS scheduling jitter does not directly modify the 20 kHz motor-step timing.

---

# 69. Why Timer1 is used

Timer1 provides a hardware-timed interrupt source suitable for deterministic periodic execution on the ATmega328P.

The controller configures:

```text
CTC mode
prescaler = 8
frequency = 20 kHz
```

All three axes are serviced from the same timer interrupt.

This produces a common deterministic time base while maintaining independent axis phase accumulators.

---

# 70. Position model

The system is open-loop.

The Arduino does not contain a physical encoder.

Position is therefore estimated from the number and direction of generated half-step transitions.

The status protocol explicitly identifies this through:

```text
POSITION_ESTIMATED
```

The position values should therefore be understood as commanded/generated step position rather than measured physical position.

---

# 71. Safety model

The principal safety mechanism in the communication subsystem is the Arduino watchdog.

If valid commands stop arriving:

```text
communication timeout
        ↓
watchdog expiry
        ↓
safe stop
        ↓
zero velocity
        ↓
stop axis execution
        ↓
optional coil release
```

The safe-stop behavior is independent of whether the CODESYS application is still executing normally.

This prevents an uncontrolled continuation of the last commanded velocity after loss of communication.

---

# 72. Physical test methodology

Before operating the system through CODESYS, the autonomous firmware should be used.

The test sequence validates:

```text
Axis 1
Axis 2
Axis 3
simultaneous motion
zero-speed holding
coil release
```

The test should be performed with the motors mechanically safe and with an external 5 V motor supply.

The six-phase sequence is:

```text
1. Axis1 +200 steps/s
2. Axis2 +200 steps/s
3. Axis3 +200 steps/s
4. Axis1 +200, Axis2 -120, Axis3 +60
5. All stopped, enabled
6. All disabled, coils released
```

---

# 73. CODESYS setup

The installation sequence is:

```text
1. Install FastArduinoMotion 0.6.3.2.
2. Install the associated device descriptions.
3. Ensure the required CODESYS libraries are available.
4. Restart or refresh the device repository if necessary.
5. Add Arduino Motion Communication.
6. Configure the COM port.
7. Add Axis1, Axis2 and Axis3 to the SoftMotion General Axis Pool.
8. Build the application.
9. Download and run.
```

---

# 74. Required files

The CODESYS installation requires:

```text
ArduinoMotionCommunication_0_6_3_2.devdesc.xml
ArduinoStepperAxis1_0_6_3_2.devdesc.xml
ArduinoStepperAxis2_0_6_3_2.devdesc.xml
ArduinoStepperAxis3_0_6_3_2.devdesc.xml
StandardParameters.xml
```

and the FastArduinoMotion library.

The Arduino side requires:

```text
FastMotionNode3Axis.h
FastMotionNode3Axis.cpp
FastMotionProtocol3Axis.h
```

with the corresponding Arduino library metadata and examples.

---

# 75. Declaration and implementation

The communication device is split into lifecycle-specific source files:

```text
50 ... DECLARATION
51 ... IMPLEMENTATION
52 ... Initialize
53 ... AfterReadInputs
54 ... FB_Exit
55 ... FB_Reinit
```

This follows the CODESYS device lifecycle.

The application program is similarly divided into:

```text
60 ... DECLARATION
61 ... IMPLEMENTATION
```

The application declaration contains the SoftMotion function blocks and motion parameters.

The implementation performs the corresponding PLCopen calls.

---

# 76. Removal of direct communication code

The final application architecture does not require direct communication calls from `PLC_PRG`.

The old pattern in which the application explicitly instantiated and called:

```text
FB_Arduino3AxisController
```

is replaced by the device-level communication architecture.

The resulting application contains only motion logic.

This prevents application code from taking ownership of the physical communication interface.

---

# 77. Device-description dependency

The device descriptions define how CODESYS instantiates the corresponding function blocks and lifecycle methods.

For the communication device:

```text
FBInstance:
DEV_ArduinoMotionCommunication
```

For the axes, the corresponding `AXIS_REF` instances are defined by their device descriptions.

The device descriptions therefore form the bridge between:

```text
CODESYS device tree
```

and:

```text
FastArduinoMotion implementation
```

---

# 78. Installation relationship

`StandardParameters.xml` is a dependency of the SoftMotion axis configuration.

It should not be treated as an independent device to be added manually.

The device descriptions and standard parameters must be installed as a consistent package corresponding to:

```text
FastArduinoMotion 0.6.3.2
```

---

# 79. Arduino package files

The complete Arduino package consists of:

```text
CHANGELOG.md
README.md
PROTOCOL_V2.md
library.properties

FastMotionNode3Axis.h
FastMotionNode3Axis.cpp
FastMotionProtocol3Axis.h

01_ThreeAxes_AutonomousTest.ino
02_ThreeAxes_BinaryNode.ino
```

The files have distinct responsibilities:

```text
README.md
    installation and usage

PROTOCOL_V2.md
    protocol specification

FastMotionNode3Axis.h
    public controller API

FastMotionNode3Axis.cpp
    controller implementation

FastMotionProtocol3Axis.h
    protocol constants and serialization

01_ThreeAxes_AutonomousTest.ino
    physical hardware test

02_ThreeAxes_BinaryNode.ino
    CODESYS communication firmware
```

---

# 80. Arduino installation

The library is installed through the Arduino IDE using the ZIP library installation mechanism.

After installation:

```text
File
  → Examples
      → FastMotionNode3Axis
```

provides the two supplied examples.

For CODESYS operation, the firmware to upload is:

```text
02_ThreeAxes_BinaryNode
```

The Arduino Serial Monitor or Serial Plotter must be closed while CODESYS owns the serial port.

---

# 81. End-to-end operation

The final runtime sequence is:

```text
CODESYS initialization
        │
        ▼
Communication device initialization
        │
        ▼
Serial connection
        │
        ▼
FB_Arduino3AxisController
        │
        ▼
28-byte command frame
        │
        ▼
Arduino parser
        │
        ├── CRC validation
        ├── version validation
        ├── type validation
        └── command application
                │
                ▼
        Axis runtime state
                │
                ▼
        Timer1 at 20 kHz
                │
        ┌───────┼───────┐
        ▼       ▼       ▼
      Axis1   Axis2   Axis3
                │
                ▼
        48-byte status
                │
                ▼
        CODESYS controller
                │
                ▼
        SoftMotion state
```

---

# 82. Design characteristics

The resulting implementation has the following characteristics:

### Three-axis architecture

The Arduino controls three independent axes from one controller.

### Shared communication

One CODESYS communication controller owns the serial connection.

### Deterministic stepping

Motor timing is generated from Timer1 at 20 kHz.

### Open-loop position

Position is estimated from generated steps.

### Binary protocol

The protocol uses fixed-size 28/48-byte frames.

### CRC protection

All frames contain CRC16/Modbus.

### Sequence tracking

Commands contain sequence numbers and statuses acknowledge them.

### Watchdog protection

Loss of valid communication produces a safe stop.

### SoftMotion abstraction

The application controls the motors through standard SoftMotion axes.

---

# 83. Version relationship

The two principal software versions are:

```text
FastArduinoMotion      0.6.3.2
FastMotionNode3Axis    2.0.1
```

The Arduino firmware uses:

```text
Protocol version 2
```

The CODESYS implementation is designed around the same protocol.

The Arduino package version and the wire-protocol version are therefore different concepts:

```text
2.0.1 = Arduino software/package version
2     = binary protocol version
0.6.3.2 = CODESYS integration version
```

---

# 84. Final functional model

The final functional model can be represented as:

```text
                 APPLICATION
                     │
             PLCopen / SoftMotion
                     │
          ┌──────────┼──────────┐
          │          │          │
        Axis1      Axis2      Axis3
          │          │          │
          └──────────┼──────────┘
                     │
                     ▼
      Arduino Motion Communication
                     │
                     ▼
        FB_Arduino3AxisController
                     │
              Protocol v2
                     │
              115200 / 8N1
                     │
                     ▼
          FastMotionNode3Axis
                 2.0.1
                     │
                 Timer1
                  20 kHz
                     │
          ┌──────────┼──────────┐
          │          │          │
        Axis1      Axis2      Axis3
        D2-D5      D6-D9     D10-D13
          │          │          │
          ▼          ▼          ▼
        28BYJ-48   28BYJ-48   28BYJ-48
```

The architecture establishes a clear separation of responsibilities:

```text
Application
    → motion commands

SoftMotion
    → axis abstraction and motion state

Communication device
    → serial ownership and diagnostics

Binary protocol
    → deterministic data representation

Arduino controller
    → command processing and safety

Timer1
    → deterministic step generation

Motor drivers
    → electrical actuation
```

This separation allows the three Arduino motors to be used as CODESYS SoftMotion axes without exposing the underlying serial protocol or low-level motor-control implementation to the application programmer.
