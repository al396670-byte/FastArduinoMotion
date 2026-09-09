# FastArduinoMotion

**FastArduinoMotion** is a CODESYS SoftMotion interface for controlling stepper-motor-based motion systems using an Arduino motion node.
The project integrates an Arduino-based motion controller into the CODESYS SoftMotion architecture. Communication, protocol handling and low-level pulse generation are encapsulated by the library, allowing the PLC application to control the axes using standard PLCopen/SoftMotion function blocks.

The intended user workflow is:

```text
CODESYS
   │
   ├── Arduino Motion Communication
   │       └── COM port
   │
   └── SoftMotion General Axis Pool
           ├── Axis1
           ├── Axis2
           └── Axis3
```

The application itself can therefore be programmed using standard motion blocks such as `MC_Power`, `MC_MoveAbsolute`, `MC_Halt` and `MC_Reset`, without implementing the serial communication manually.

---

## 1. Features

- CODESYS SoftMotion integration.
- Three independent SoftMotion axes.
- Shared serial communication channel.
- Dedicated `Arduino Motion Communication` device.
- Binary communication protocol with CRC16/Modbus.
- Sequence-number-based communication.
- Configurable communication port.
- Watchdog supervision.
- Enable, Quick Stop and position-reset commands.
- Position and velocity feedback.
- Communication diagnostics and counters.
- Arduino Timer1-based motion generation.
- STEP/DIR output.
- Four-coil full-step and half-step output on the Arduino node.
- PLC application independent of the communication implementation.
- Axis scaling directly in engineering units.
- Default trapezoidal velocity profile.

---

# 2. System architecture

The project is divided into three main layers:

```text
┌─────────────────────────────────────────────┐
│                  CODESYS                    │
│                                             │
│  PLC application                            │
│      │                                      │
│      ▼                                      │
│  PLCopen / SoftMotion                       │
│      │                                      │
│      ├── Axis1                              │
│      ├── Axis2                              │
│      └── Axis3                              │
│              │                              │
│              ▼                              │
│  Arduino Motion Communication               │
│              │                              │
│              ▼                              │
│       Binary serial protocol                │
└──────────────┼──────────────────────────────┘
               │
               │ USB / UART
               │
┌──────────────▼──────────────────────────────┐
│                  Arduino                    │
│                                             │
│           FastMotionNode                    │
│                │                            │
│                ▼                            │
│          Timer1 / ISR                       │
│                │                            │
│                ▼                            │
│          STEP / DIR                         │
│                │                            │
│                ▼                            │
│        Stepper motor driver                 │
└─────────────────────────────────────────────┘
```

The communication device is deliberately independent of the SoftMotion axis hierarchy. `Arduino Motion Communication` is added to the PLC device, while the three axes are added independently under `SoftMotion General Axis Pool`. It is therefore **not the parent device of the three axes**.

This separation allows several SoftMotion axes to share a single communication owner.

---

# 3. Repository structure

A recommended repository structure is:

```text
FastArduinoMotion/
│
├── Arduino/
│   └── FastMotionNode3AxisV2/
│       ├── examples/
│       │   ├── 01_ThreeAxes_AutonomousTest/
│       │   └── 02_ThreeAxes_BinaryNode/
│       │
│       ├── src/
│       │   ├── FastMotionNode3Axis.cpp
│       │   ├── FastMotionNode3Axis.h
│       │   └── FastMotionProtocol3Axis.h
│       │
│       ├── CHANGELOG.md
│       ├── library.properties
│       ├── PROTOCOL_V2.md
│       └── README.md
│
├── CODESYS/
│   ├── DeviceDescriptions/
│   │   ├── ArduinoMotionCommunication_0_6_3_2.devdesc.xml
│   │   ├── ArduinoStepperAxis1_0_6_3_2.devdesc.xml
│   │   ├── ArduinoStepperAxis2_0_6_3_2.devdesc.xml
│   │   ├── ArduinoStepperAxis3_0_6_3_2.devdesc.xml
│   │   ├── SM3_Drive_FAM.ico
│   │   └── StandardParameters.xml
│   │
│   ├── Examples/
│   │   └── PruebaLibreria0632.project
│   │
│   └── Library/
│       ├── FastArduinoMotion_0_6_3_2.library
│       └── FastArduinoMotion_0_6_3_2.compiledlibrary
│
├── Docs/
│   └── TECHNICAL_DOCUMENTATION.md
│
└── README.md
```

---

# 4. Requirements

## CODESYS

The CODESYS project requires the FastArduinoMotion library and the corresponding device descriptions.

The library uses the following CODESYS components:

```text
IoStandard
SysCom
SysTypes2
Interfaces
SM3 Basic
SM3 Drive PosControl
```

`IoStandard` is required by the generic communication device. The existing SoftMotion and system libraries remain part of the project.

## Arduino

The Arduino implementation targets:

- Arduino UNO;
- Arduino Nano;
- AVR architecture.

The supplied `FastMotionNode` implementation uses Timer1 for motion generation and supports STEP/DIR and four-coil outputs.

---

# 5. Installation

## 5.1 Install the CODESYS library

Install:

```text
FastArduinoMotion 0.6.3.2
```

The library project uses:

```text
Version: 0.6.3.2
Company: Universitat Jaume I
Title: FastArduinoMotion
Namespace: FAM
```

After installing or rebuilding the library, use:

```text
Build → Clean All
Build → Build
```

---

## 5.2 Install the device descriptions

The device repository must contain the following files:

```text
StandardParameters.xml

ArduinoMotionCommunication_0_6_3_2.devdesc.xml

ArduinoStepperAxis1_0_6_3_2.devdesc.xml

ArduinoStepperAxis2_0_6_3_2.devdesc.xml

ArduinoStepperAxis3_0_6_3_2.devdesc.xml
```

The `.devdesc.xml` files are installed through the CODESYS Device Repository.

`StandardParameters.xml` is included by the axis device descriptions and should **not be installed directly as an independent device description**.

---

# 6. Configure the CODESYS project

## 6.1 Add the communication device

Right-click the main PLC device:

```text
Device
└── Add Device
```

Select:

```text
Arduino Motion Communication
```

The device uses the generic:

```text
Common.PCI
```

interface.

Its purpose is to provide one shared communication instance for the Arduino motion system. The device descriptor identifies it as device type `8000`, version `0.6.3.2`, and associates it with `DEV_ArduinoMotionCommunication`.

---

## 6.2 Configure the COM port

Open:

```text
Arduino Motion Communication
└── Parameters
```

The configurable parameter is:

```text
COM port number
```

Only the numeric part is entered:

```text
3  → COM3
34 → COM34
```

The descriptor defines parameter `20000` as the communication-port parameter and uses `3` as its default value.

For the current implementation, the communication settings are:

```text
Baud rate: 115200 bit/s
Watchdog: 250 ms
```

These values are intentionally not exposed as user-configurable parameters in the current CODESYS device.

---

# 7. Add the SoftMotion axes

Under:

```text
SoftMotion General Axis Pool
```

add:

```text
Arduino Stepper Axis 1 - D2-D5
Arduino Stepper Axis 2 - D6-D9
Arduino Stepper Axis 3 - D10-D13
```

The recommended application names are:

```text
Axis1
Axis2
Axis3
```

The axis descriptors implement the CODESYS SoftMotion interfaces:

```text
Common.SoftMotion.Logical
Common.SoftMotion.General
```

and associate each device with the corresponding `AXIS_REF_ArduinoStepperAxis` implementation.

Axis 2 and Axis 3 follow the same architecture as Axis 1, with their corresponding hardware/channel assignment.

---

# 8. Axis scaling

The axes are configured so that CODESYS works in degrees while the Arduino motion node works in motor steps.

The default scaling is:

```text
4096 half-steps / revolution
360 application degrees / revolution
```

The axis descriptor implements this using:

```text
Parameter 1051 = 4096
Parameter 1052 = 360
```

Therefore:

```text
4096 steps  → 360°
1 revolution → 360°
```

The scaling parameters are part of the SoftMotion axis description.

The internal scaling parameters also define 360 application units per output revolution and provide a direction-inversion parameter.

---

# 9. Motion profile

The standard parameter:

```text
1062 — Velocity ramp type
```

has default value:

```text
0
```

which corresponds to:

```text
Trapezoid
```

The available mapping used by the project is:

```text
0 = trapez
1 = sinsquare
2 = quadratic_ramp
3 = quadratic_smooth_ramp
```

Consequently, a newly added axis uses a trapezoidal velocity profile by default.

---

# 10. Programming the axes

No serial communication code is required in `PLC_PRG`.

A typical application contains only SoftMotion function blocks:

```iecst
fbPower1 : MC_Power;
fbMove1  : MC_MoveAbsolute;
fbHalt1  : MC_Halt;
fbReset1 : MC_Reset;
```

and equivalent blocks for Axis2 and Axis3.

The communication controller, COM-port handling, watchdog and protocol processing are internal to the generic communication device.

A basic application therefore has the following structure:

```text
PLC_PRG
│
├── MC_Reset
├── MC_Power
├── MC_MoveAbsolute
└── MC_Halt
```

---

# 11. Example

The supplied SoftMotion example uses:

```text
Axis 1 target:  90°
Axis 2 target: -90°
Axis 3 target:  45°
```

with:

```text
Axis 1 velocity: 30 °/s
Axis 2 velocity: 20 °/s
Axis 3 velocity: 15 °/s
```

and:

```text
Acceleration: 60 °/s²
Deceleration: 60 °/s²
```

The implementation demonstrates the intended programming model: each axis is controlled directly through standard SoftMotion blocks.

---

# 12. Communication

The current architecture has a single serial communication owner:

```text
DEV_ArduinoMotionCommunication
        │
        ▼
FB_Arduino3AxisController
        │
        ▼
Serial connection
```

The device descriptor automatically creates the communication FB and calls its `AfterReadInputs` method during the CODESYS bus cycle.

This replaces the previous approach in which the application itself had to instantiate and execute the communication controller.

The resulting PLC application does not need to know about:

```text
COM
SysCom
CRC
serial frames
watchdog
FB_Arduino3AxisController
```

---

# 13. Diagnostics

The communication device exposes diagnostic information including:

```text
xReady
xCommunicationValid
xProtocolReady
xNodeWatchdogOk
xNodeWarning
xNodeFault
```

and:

```text
uiNodeError
wReadyMask
wErrorMask
```

as well as communication counters:

```text
udiTxFrameCount
udiRxFrameCount
udiRxCrcErrorCount
udiRxFormatErrorCount
udiAutoReconnectCount
```

These variables make it possible to distinguish normal operation from communication or protocol problems.

---

# 14. Binary protocol

The protocol documentation supplied with the project defines a binary UART protocol using:

```text
115200 baud
8N1
Little-endian
CRC16/Modbus
```

The documented phase-1 protocol contains:

```text
PLC → Arduino : 28 bytes
Arduino → PLC : 48 bytes
```

The PLC-to-Arduino frame contains:

```text
Sync
Version
Type
Sequence
Watchdog
Enable mask
Quick-stop mask
Reset-position mask
Velocity Axis 1
Velocity Axis 2
Velocity Axis 3
CRC
```

The Arduino-to-PLC frame contains:

```text
Sync
Version
Type
Acknowledged sequence
Node state
Node error
Ready mask
Error mask
Position Axis 1
Position Axis 2
Position Axis 3
Applied velocity Axis 1
Applied velocity Axis 2
Applied velocity Axis 3
Axis statuswords
Order age
CRC
```

The axis masks use:

```text
bit 0 → Axis 1
bit 1 → Axis 2
bit 2 → Axis 3
```

---

# 15. Arduino motion node

The Arduino implementation is based on `FastMotionNode`.

Its documented characteristics include:

```text
UART: 115200 bit/s
Command frame: 16 bytes
Status frame: 20 bytes
CRC: CRC16/Modbus
Sequence number
Enable
Quick Stop
Position reset
Configurable watchdog
Signed DINT velocity in steps/s
DINT position in generated steps
Timer1-based motion generation
STEP/DIR output
Four-coil full-step / half-step output
```

The important architectural principle is that Timer1 generates the motion independently from the communication process.

```text
Serial communication
        │
        └── receives commands

Timer1
        │
        └── generates motion
```

This prevents serial communication itself from being responsible for the precise timing of every motor step.

---

# 16. Watchdog

The communication watchdog protects the system against loss of command communication.

The Arduino node monitors the elapsed time since the last valid command.

If the configured timeout expires, the motion is stopped and the node enters its watchdog condition.

The CODESYS communication layer exposes the watchdog state through:

```text
xNodeWatchdogOk
```

The current CODESYS configuration uses a watchdog value of:

```text
250 ms
```

while the Arduino node itself supports a configurable watchdog.

---

# 17. Error codes

The documented protocol defines:

| Code | Meaning |
|------|---------|
| 0 | No error |
| 1 | CRC error |
| 2 | Invalid version |
| 3 | Invalid type |
| 4 | Watchdog |
| 5 | Velocity limited |
| 6 | Invalid/reserved mask |

During the documented phase, codes `4` and `5` are treated as warnings rather than permanent communication errors.

---

# 18. Testing

A basic commissioning sequence is:

### Step 1 — Verify communication

Without enabling the motors:

```text
xReady = TRUE
xCommunicationValid = TRUE
CRC errors = 0
Format errors = 0
```

### Step 2 — Enable an axis

Use:

```text
MC_Power
```

for Axis1.

### Step 3 — Execute a movement

Use:

```text
MC_MoveAbsolute
```

with a suitable position and velocity.

### Step 4 — Test the remaining axes

Repeat for Axis2 and Axis3.

### Step 5 — Test simultaneous operation

Enable and command multiple axes at the same time.

### Step 6 — Test communication loss

With the motors stopped:

```text
COM port = 33
```

Download the configuration and verify that communication is lost.

Restore:

```text
COM port = 3
```

and verify that communication is restored.

This test also demonstrates that the serial-port configuration is owned by the generic communication device rather than by `PLC_PRG`.

---

# 19. Important limitation: no encoder feedback

The Arduino position is an estimate based on the steps generated by the controller.

It is **not a measured mechanical position**.

Without an encoder:

```text
Generated steps ≠ guaranteed mechanical displacement
```

if the motor loses steps mechanically.

Consequently, the system cannot independently detect mechanical step loss using the supplied hardware/software configuration.

---

# 20. Version information

The CODESYS integration described by this documentation corresponds to:

```text
FastArduinoMotion 0.6.3.2
```

The Arduino motion-node material supplied with the project identifies:

```text
FastMotionNode 1.0
```

These version numbers should be kept explicit in releases because the supplied documentation also contains a separate protocol-v2 specification.

---

# 21. Protocol-version note

The repository contains a protocol specification identified as:

```text
Binary Protocol v2
```

which specifies 28-byte PLC-to-Arduino and 48-byte Arduino-to-PLC frames.

The supplied `FastMotionNode` documentation, however, describes a different frame organization of:

```text
16-byte command frame
20-byte status frame
```

These descriptions must therefore be treated as belonging to their respective implementation/version stages unless the corresponding complete controller/protocol source establishes their equivalence.

This README intentionally does not assume an undocumented conversion between the two formats.

---

# 22. Project philosophy

The main design objective of FastArduinoMotion is to separate the three responsibilities of the system:

```text
CODESYS SoftMotion
        │
        │ Motion commands
        ▼
Communication layer
        │
        │ Binary protocol
        ▼
Arduino motion node
        │
        │ Timed pulses
        ▼
Stepper motor
```

This separation means that the end user can interact with the system at the SoftMotion level rather than at the UART or GPIO level.

The final user workflow is therefore:

```text
1. Install FastArduinoMotion
2. Install the device descriptions
3. Add Arduino Motion Communication
4. Configure the COM port
5. Add Axis1 / Axis2 / Axis3
6. Program the axes using SoftMotion
```

This is the intended user interface of the project.
