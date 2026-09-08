# FastArduinoMotion — Technical Documentation

## 1. Purpose and scope

This document describes the internal architecture and implementation of the FastArduinoMotion system.

The project integrates a custom Arduino-based stepper-motor controller with CODESYS SoftMotion. The implementation is divided into:

1. CODESYS library;
2. CODESYS device descriptions;
3. SoftMotion axis references;
4. shared communication controller;
5. binary communication protocol;
6. Arduino motion node;
7. motion-generation and timing subsystem;
8. example application.

The objective is not to reproduce the source code line by line, but to explain the responsibility of each program, function block, method and functional group.

Where several files implement the same structure for Axis1, Axis2 and Axis3, the common implementation is described once and the axis-specific differences are identified separately.

---

# 2. Global architecture

The complete system can be represented as:

```text
                         CODESYS
┌────────────────────────────────────────────────────┐
│                                                    │
│                    PLC_PRG                         │
│                                                    │
│       MC_Power / MC_MoveAbsolute / MC_Halt        │
│                    / MC_Reset                      │
│                         │                          │
│                         ▼                          │
│               SoftMotion Axis                     │
│          ┌────────┬────────┬────────┐              │
│          │ Axis1  │ Axis2  │ Axis3  │              │
│          └───┬────┴───┬────┴───┬────┘              │
│              │        │        │                   │
│              ▼        ▼        ▼                   │
│          AXIS_REF_ArduinoStepperAxis               │
│                         │                          │
│                         │                         │
│              ┌──────────▼──────────┐              │
│              │ DEV_ArduinoMotion   │              │
│              │ Communication       │              │
│              └──────────┬──────────┘              │
│                         │                          │
│                 FB_Arduino3AxisController          │
│                         │                          │
│                         ▼                          │
│                    UART / USB                      │
└─────────────────────────┼──────────────────────────┘
                          │
                          ▼
                  Arduino motion node
                          │
                  ┌───────▼────────┐
                  │ FastMotionNode │
                  └───────┬────────┘
                          │
                       Timer1
                          │
                          ▼
                    STEP / DIR
                          │
                          ▼
                       Motor

```

The most important architectural characteristic is that communication is **shared**, whereas SoftMotion axes remain independent devices.

The project documentation explicitly specifies that `Arduino Motion Communication` is not placed below SoftMotion and is not the parent of Axis1–Axis3.

---

# 3. CODESYS library

## 3.1 Library dependencies

The CODESYS library contains the functionality necessary to implement:

- the shared communication device;
- the Arduino controller;
- the SoftMotion axis references;
- communication and motion data handling.

The current 0.6.3.2 configuration adds:

```text
IoStandard
```

while retaining:

```text
SysCom
SysTypes2
Interfaces
SM3 Basic
SM3 Drive PosControl
```

The generic communication-device implementation consists of:

```text
50 DEV_ArduinoMotionCommunication declaration
51 implementation
52 Initialize
53 AfterReadInputs
54 FB_Exit
55 FB_Reinit
```

while the existing controller, GVL and three axis references remain in the library.

---

# 4. `DEV_ArduinoMotionCommunication`

## 4.1 Responsibility

`DEV_ArduinoMotionCommunication` is the CODESYS device-level wrapper around the communication controller.

Its purpose is to transform a device-description configuration into a running communication instance.

Before this abstraction was introduced, the application had to declare and execute the communication controller directly.

The new architecture removes that responsibility from `PLC_PRG`.

The old application-level elements:

```iecst
fbArduinoController : FAM.FB_Arduino3AxisController;
xRunLink : BOOL;
uiArduinoComPortNumber : UINT;
uiArduinoWatchdogMs : UINT;
```

are no longer required in the application.

---

# 5. Declaration of `DEV_ArduinoMotionCommunication`

The declaration contains four functional groups.

## 5.1 Communication status

```iecst
xReady
xCommunicationValid
xProtocolReady
xNodeWatchdogOk
```

These variables represent the operational state of the communication subsystem.

Their purpose is to distinguish between:

- device readiness;
- valid communication;
- protocol readiness;
- watchdog status.

---

## 5.2 Node status

```iecst
xNodeWarning
xNodeFault
uiNodeError
wReadyMask
wErrorMask
```

These variables expose status information originating from the Arduino node and distribute it to the CODESYS device layer.

The masks provide per-axis information.

For the documented three-axis protocol:

```text
bit 0 → Axis1
bit 1 → Axis2
bit 2 → Axis3
```

---

## 5.3 Diagnostic counters

```iecst
udiTxFrameCount
udiRxFrameCount
udiRxCrcErrorCount
udiRxFormatErrorCount
udiAutoReconnectCount
```

These counters provide information useful for commissioning and troubleshooting.

They allow the user to distinguish, for example:

```text
No frames transmitted
        vs.
Frames transmitted but not received
        vs.
Frames received with CRC errors
        vs.
Frames with invalid format
        vs.
Communication recovery/reconnection
```

---

## 5.4 Configuration

```iecst
uiComPortNumber
udiBaudRate
uiWatchdogMs
```

The only user-configurable value exposed by the device description is:

```text
uiComPortNumber
```

The descriptor identifies parameter `20000` as the COM-port number.

The current implementation fixes:

```text
udiBaudRate = 115200
uiWatchdogMs = 250
```

---

# 6. `DEV_ArduinoMotionCommunication.Initialize`

## 6.1 Purpose

`Initialize` is responsible for reading the configuration stored in the CODESYS device description and converting it into internal FB parameters.

The sequence is:

```text
CODESYS device parameter
        │
        ▼
IoStandard.ConfigGetParameter
        │
        ▼
IoStandard.ConfigGetParameterValueDword
        │
        ▼
uiComPortNumber
```

---

## 6.2 Default value

The method starts with:

```iecst
uiComPortNumber := 3;
```

This guarantees that a valid default exists even when no parameter can be obtained.

---

## 6.3 Connector validation

The code checks:

```iecst
pConnector <> 0
```

before attempting to read the configuration.

This prevents an invalid connector pointer from being passed to the configuration functions.

---

## 6.4 Parameter lookup

The method searches for:

```text
ParameterId = 20000
```

This corresponds to:

```text
COM port number
```

in the device description.

---

## 6.5 Value validation

After reading the DWORD, the value is accepted only when:

```text
1 ≤ COM number ≤ 999
```

It is then converted to:

```text
UINT
```

Thus:

```text
3  → COM3
34 → COM34
```

The XML description explicitly defines the parameter as the numeric portion of the COM name.

---

## 6.6 Fixed communication parameters

Finally:

```iecst
udiBaudRate := 115200;
uiWatchdogMs := 250;
```

This is deliberate: the current device description does not expose baudrate or watchdog configuration to the user.

---

# 7. `DEV_ArduinoMotionCommunication.AfterReadInputs`

This method is the execution point that connects the CODESYS bus cycle to the communication controller.

The central call is:

```iecst
fbArduinoController(
    xRun := TRUE,
    uiComPortNumber := uiComPortNumber,
    uiArduinoWatchdogMs := uiWatchdogMs
);
```

The device therefore owns and executes the communication FB.

The descriptor defines:

```text
AfterReadInputs
```

as a cyclic call associated with:

```text
#buscycletask
```

and the `afterReadInputs` phase.

---

## 7.1 Output propagation

After executing the controller, the method copies its state to the device outputs:

```text
FB_Arduino3AxisController
        │
        ├── xReady
        ├── xCommunicationValid
        ├── xProtocolReady
        ├── xNodeWatchdogOk
        ├── xNodeWarning
        ├── xNodeFault
        ├── uiNodeError
        ├── wReadyMask
        ├── wErrorMask
        └── diagnostic counters
```

This creates a clean boundary:

```text
Internal controller
        ↓
Generic CODESYS device
        ↓
Device diagnostics
```

The application does not need to access the controller directly.

---

# 8. `DEV_ArduinoMotionCommunication.FB_Exit`

`FB_Exit` is executed when the device instance is removed or destroyed.

Its main responsibility is explicit shutdown of the communication owner:

```iecst
fbArduinoController(
    xRun := FALSE,
    ...
);
```

The device status variables are subsequently invalidated.

Conceptually:

```text
Device destruction
        ↓
Stop communication controller
        ↓
Close/release communication resource
        ↓
Invalidate status outputs
```

This is especially important because the serial port represents a shared resource.

---

# 9. `DEV_ArduinoMotionCommunication.FB_Reinit`

`FB_Reinit` resets the logical device state.

The following status variables are cleared:

```text
xReady
xCommunicationValid
xProtocolReady
xNodeWatchdogOk
xNodeWarning
xNodeFault
```

The method does not implement the complete communication startup procedure itself; it returns the FB state to a known non-operational condition so that normal initialization can take place again.

---

# 10. Device description: communication device

The XML file:

```text
ArduinoMotionCommunication_0_6_3_2.devdesc.xml
```

defines the generic communication device.

Important elements include:

```text
Type       = 8000
ID         = FFFF A652
Version    = 0.6.3.2
Vendor     = Universitat Jaume I
Order      = FAM-COMM
```

It uses:

```text
Common.PCI
```

as its interface.

The driver information associates the device with:

```text
FastArduinoMotion 0.6.3.2
DEV_ArduinoMotionCommunication
```

and automatically invokes:

```text
Initialize
AfterReadInputs
```

during the CODESYS lifecycle.

---

# 11. SoftMotion axis implementation

The three axis devices share the same basic architecture.

Axis1 is identified as:

```text
Arduino Stepper Axis 1 - pins D2-D5
```

and its description specifies a SoftMotion position-controlled axis mapped to channel 1 of the shared motion node.

Axis2 and Axis3 use the same structure, changing the corresponding axis/channel identification.

Therefore, the functional analysis below applies to all three axes unless explicitly stated otherwise.

---

# 12. Axis device description

The axis descriptor provides two main SoftMotion interfaces:

```text
Common.SoftMotion.Logical
Common.SoftMotion.General
```

The logical connector represents the SoftMotion drive interface, while the general connector represents the concrete Arduino axis device.

The descriptor associates the device with:

```text
AXIS_REF_ArduinoStepperAxis1
```

for Axis1.

It also automatically invokes:

```text
Initialize
BeforeReadInputs
AfterReadInputs
BeforeWriteOutputs
AfterWriteOutputs
```

through the CODESYS bus-cycle mechanism.

Axis2 and Axis3 follow the same execution model.

---

# 13. Standard axis parameters

`StandardParameters.xml` supplies the standard SoftMotion configuration.

Among the parameters used by the axes are:

```text
Movement type
Position period
Software limits
Ramp type
Software maximum velocity
Software maximum acceleration
Software maximum deceleration
Software maximum jerk
```

The important project-specific modification is:

```text
Parameter 1062 = 0
```

meaning trapezoidal velocity ramp.

---

# 14. Axis scaling

The main scaling parameters are:

```text
1051 = 4096
1052 = 360
```

with:

```text
4096 half-steps / revolution
360 application degrees / revolution
```

This means that the CODESYS application operates in angular units while the low-level motion controller operates in steps.

The conceptual conversion is:

```text
CODESYS position
      │
      │ degrees
      ▼
SoftMotion scaling
      │
      │ steps
      ▼
Arduino motion node
```

The descriptor additionally defines:

```text
ScalingIncs = 4096
ScalingMotorTurns1 = 1
ScalingMotorTurns2 = 1
ScalingGearOutput1 = 1
ScalingGearOutput2 = 1
ScalingUnits = 360
InvertDirection = FALSE
```

---

# 15. Position-control parameters

The axis description also contains SoftMotion position-controller parameters.

These include:

```text
MaximumOutputVelocityInt
MaximumOutputVelocityTU
MinimumOutputVelocityInt
MinimumOutputVelocityTU
ZeroVelocityInt
DirectionInverted
```

with default values including:

```text
+1000
+87.890625
-1000
-87.890625
0
FALSE
```

The controller section additionally contains:

```text
Kp
MaxFollowingError
EnableFollowingErrorCheck
PartVelocityPilotControl
DeadTime
```

The supplied defaults are:

```text
Kp = 0
MaxFollowingError = 0
EnableFollowingErrorCheck = FALSE
PartVelocityPilotControl = 1
DeadTime = 2
```

These parameters belong to the SoftMotion axis abstraction and should not be confused with the Arduino pulse-generation algorithm.

---

# 16. Encoder-related configuration

The axis descriptor defines:

```text
EncoderBitWidth = 32
```

However, the physical Arduino implementation described in the supplied files does not provide an encoder measurement.

Consequently, the position reported by the Arduino node is based on generated steps rather than an independently measured mechanical position.

---

# 17. `PLC_PRG`

The current application is deliberately reduced to SoftMotion functionality.

The declaration contains, for each axis:

```text
MC_Power
MC_MoveAbsolute
MC_Halt
MC_Reset
```

plus command variables and motion parameters.

There are no communication-specific variables.

In particular, the application no longer needs:

```text
FB_Arduino3AxisController
COM configuration
serial handling
CRC processing
watchdog handling
```

The communication device performs those tasks automatically.

---

# 18. `PLC_PRG` execution structure

The three axes follow the same sequence.

For each axis:

```text
MC_Reset
    ↓
MC_Power
    ↓
MC_MoveAbsolute
    ↓
MC_Halt
```

The implementation is repeated for:

```text
Axis1
Axis2
Axis3
```

The functional interpretation is the same for all three axes; only the `Axis` reference and corresponding command variables differ.

---

# 19. `MC_Power`

`MC_Power` is used to enable the SoftMotion axis.

The supplied application uses:

```iecst
Enable := TRUE
```

while:

```iecst
bRegulatorOn := xPowerX
bDriveStart  := xPowerX
```

control the requested powered state.

The application therefore exposes an explicit enable command for each axis.

---

# 20. `MC_MoveAbsolute`

`MC_MoveAbsolute` receives:

```text
Axis
Execute
Position
Velocity
Acceleration
Deceleration
```

For the example:

```text
Axis1 → 90°
Axis2 → -90°
Axis3 → 45°
```

The velocity values are:

```text
Axis1 → 30 °/s
Axis2 → 20 °/s
Axis3 → 15 °/s
```

and the common acceleration/deceleration values are:

```text
60 °/s²
```

The important point is that the application specifies angular quantities rather than motor-step counts. The axis scaling layer handles the conversion toward the low-level representation.

---

# 21. `MC_Halt`

`MC_Halt` provides controlled stopping of the corresponding axis.

The application passes:

```text
Axis
Execute
Deceleration
```

using the configured deceleration value.

---

# 22. `MC_Reset`

`MC_Reset` is used to request reset of an axis error condition.

The application simply associates each reset block with:

```text
Axis1
Axis2
Axis3
```

and its corresponding command variable.

---

# 23. Communication controller

The shared internal controller is:

```text
FB_Arduino3AxisController
```

The `DEV_ArduinoMotionCommunication` FB contains one instance:

```iecst
fbArduinoController : FB_Arduino3AxisController;
```

This is the unique communication owner created by the generic device.

The controller is therefore not instantiated once per axis.

The architecture is:

```text
                    FB_Arduino3AxisController
                           │
                ┌──────────┼──────────┐
                │          │          │
             Axis1       Axis2      Axis3
```

This is what makes it possible for three independent SoftMotion devices to share one physical serial connection.

---

# 24. Arduino library

The Arduino side is represented by the `FastMotionNode` implementation and its associated protocol functionality.

Its documented characteristics are:

```text
Arduino UNO/Nano AVR
115200 bit/s
Binary communication
CRC16/Modbus
Sequence numbers
Watchdog
Enable
Quick Stop
Position reset
Signed velocity in steps/s
Generated-step position
Timer1 motion generation
STEP/DIR
4-wire full-step
4-wire half-step
```

---

# 25. `FastMotionNode`

`FastMotionNode` is the low-level motion-node class.

Its responsibility is to transform received motion commands into physical motor-control signals.

The division of responsibility is:

```text
Serial communication
       ↓
Receive command
       ↓
FastMotionNode
       ↓
Motion state
       ↓
Timer1
       ↓
GPIO transitions
       ↓
Motor
```

---

# 26. Node configuration

The supplied implementation initializes a configuration structure containing parameters such as:

```text
baud rate
driver mode
step pin
direction pin
enable pin
enable polarity
coil-release behaviour
maximum step rate
step pulse duration
direction setup time
watchdog
status period
coil pins
```

For the documented default configuration:

```text
Baud rate       = 115200
Driver mode     = STEP/DIR
STEP pin        = 9
DIR pin         = 8
ENABLE pin      = 7
Max step rate   = 12000 steps/s
Step pulse      = 4 µs
Direction setup = 5 µs
```

The supplied source also supports a four-coil output mode.

---

# 27. `begin()`

`begin()` performs the startup sequence.

Conceptually:

```text
begin()
 │
 ├── Store configuration
 │
 ├── Configure GPIO
 │
 ├── Configure Timer1
 │
 ├── Calculate timing constants
 │
 ├── Start Serial
 │
 ├── Initialize watchdog timing
 │
 └── Initialize status timing
```

The function also ensures that the Timer1-based architecture is initialized consistently.

---

# 28. `configurePins()`

`configurePins()` selects the appropriate GPIO architecture.

For STEP/DIR:

```text
STEP
DIR
ENABLE
```

are configured.

For four-wire modes:

```text
Coil 1
Coil 2
Coil 3
Coil 4
```

are configured.

The implementation also supports releasing the coils when the driver is disabled.

---

# 29. Fast GPIO access

The Arduino implementation contains functions for initializing and writing GPIO through direct port registers.

The purpose is to reduce the execution overhead associated with high-level GPIO functions.

This is particularly relevant to:

```text
STEP pulse generation
coil-pattern generation
direction changes
```

where timing is important.

---

# 30. Timer1 configuration

The motion generator uses:

```text
Timer1
```

with a prescaler of:

```text
8
```

On a 16 MHz AVR clock:

```text
16 MHz / 8 = 2 MHz
```

giving:

```text
0.5 µs / timer tick
```

This timer is independent from the serial communication process.

The project therefore separates:

```text
Communication timing
```

from:

```text
Motion timing
```

which is one of the key implementation characteristics of the Arduino node.

---

# 31. `poll()`

`poll()` is responsible for the non-interrupt-driven communication and supervision work.

Its conceptual sequence is:

```text
poll()
 │
 ├── Read available serial bytes
 │
 ├── Pass bytes to parser
 │
 ├── Check watchdog
 │
 └── Send status when due
```

The timer interrupt is deliberately not handled here.

This means:

```text
poll()
→ communication

Timer1 ISR
→ motion generation
```

---

# 32. `parseReceivedByte()`

The parser searches for the protocol synchronization sequence.

For the documented FastMotionNode implementation, the command frame uses a synchronization sequence before the fixed-size frame.

Once synchronized, bytes are accumulated until the complete command frame is available.

The parser then transfers the frame to:

```text
acceptCommandFrame()
```

This design separates:

```text
Byte reception
```

from:

```text
Frame validation and interpretation
```

---

# 33. `acceptCommandFrame()`

This function validates a complete command frame.

The logical sequence is:

```text
Received frame
      │
      ▼
CRC verification
      │
      ▼
Protocol version
      │
      ▼
Message type
      │
      ▼
Sequence number
      │
      ▼
Watchdog value
      │
      ▼
Command data
      │
      ▼
applyCommand()
```

Invalid frames are rejected before their motion commands are applied.

---

# 34. `applyCommand()`

`applyCommand()` interprets the command-control fields.

The principal operations are:

```text
Enable
Quick Stop
Reset Position
Velocity command
```

The command state determines whether the driver should be:

```text
disabled
enabled and stopped
enabled and moving
quick-stopped
```

A position reset is handled on the appropriate command transition rather than continuously resetting the position.

---

# 35. Watchdog handling

Every valid command refreshes the watchdog timing.

The principle is:

```text
Valid command
      ↓
Update last-command timestamp
      ↓
Continue motion
```

If no valid command arrives within the configured interval:

```text
Watchdog timeout
      ↓
Stop motion
      ↓
Disable driver
      ↓
Set watchdog condition
```

This provides a fail-safe response to communication interruption.

---

# 36. `setDriverEnabled()`

This function converts the logical enable state into the electrical state required by the configured driver.

For STEP/DIR operation this controls the enable output according to the configured polarity.

For four-coil operation it controls whether the coils are energized or released.

The implementation therefore abstracts the electrical polarity from the higher-level motion command.

---

# 37. `setVelocityStepsPerSecond()`

This function receives the requested velocity in:

```text
steps/s
```

The velocity is signed:

```text
positive → one direction
negative → opposite direction
zero     → stop
```

The requested value is limited to:

```text
±maxStepRate
```

If the request exceeds the allowed maximum, the applied velocity is clamped and the corresponding warning condition is generated.

The protocol documentation identifies velocity limitation as error/warning code `5`.

---

# 38. Velocity-to-period conversion

The timer does not directly operate with:

```text
steps/s
```

It needs a timer interval.

Conceptually:

```text
Requested velocity
        │
        ▼
steps per second
        │
        ▼
period between steps
        │
        ▼
Timer1 ticks
```

The implementation calculates this timing using the Timer1 clock and its configured prescaler.

The result is scheduled through the timer compare mechanism.

---

# 39. Direction changes

Direction changes are handled explicitly rather than treating the direction signal as an ordinary instantaneous state.

The implementation accounts for:

```text
direction
direction setup time
timer scheduling
```

This ensures that the required setup interval is respected around direction changes.

---

# 40. Timer scheduling

The Timer1 scheduling functions include:

```text
stopMotionLocked()
scheduleTicksLocked()
rescheduleForPeriodChangeLocked()
```

Their common purpose is to maintain the timing relationship between:

```text
current velocity
timer period
next compare event
```

When velocity changes, the current schedule can be adjusted rather than restarting the entire motion-generation mechanism blindly.

The implementation also handles long intervals that exceed a single timer compare range.

---

# 41. Timer interrupt

The Timer1 interrupt dispatches into the `FastMotionNode` singleton.

The architecture is conceptually:

```text
Timer1 Compare Interrupt
          │
          ▼
onTimerCompareISR()
          │
          ▼
handleTimerISR()
```

The singleton structure allows the interrupt routine to access the active node instance.

---

# 42. `handleTimerISR()`

This is one of the most time-critical functions.

## STEP/DIR mode

The sequence is conceptually:

```text
STEP LOW
   │
   ▼
STEP HIGH
   │
   ▼
register generated step
   │
   ▼
wait pulse duration
   │
   ▼
STEP LOW
   │
   ▼
wait remaining period
   │
   ▼
next step
```

The generated position is updated as the step is produced.

---

# 43. Four-coil mode

For four-wire operation, `handleTimerISR()` does not generate a STEP pulse.

Instead it advances through predefined coil patterns.

Two excitation schemes are supported:

```text
Full-step
Half-step
```

The relevant functions construct and apply the appropriate coil states.

---

# 44. Coil pattern generation

The implementation contains:

```text
buildCoilPortPatterns()
writeCoilPattern()
releaseCoils()
```

The purpose is to optimize the physical output operation.

When the four coils are located on compatible ports, direct port writes can be used.

Otherwise, individual fast-pin operations are used.

---

# 45. Position representation

The Arduino node maintains the generated position as an integer number of steps.

The CODESYS side then interprets this through the configured SoftMotion scaling.

Therefore:

```text
Arduino
position = generated steps

CODESYS
position = scaled engineering units
```

This is why the axis descriptor can expose degrees to the PLC application while the Arduino controller operates in steps.

---

# 46. `sendStatusIfDue()`

The status transmitter periodically sends the node state.

It first determines whether the configured status period has elapsed.

It also checks that enough serial-buffer capacity exists.

The function does not wait indefinitely for buffer space.

This avoids turning status transmission into a blocking operation.

---

# 47. `buildStatusFrame()`

This function assembles the status message.

The status information includes, depending on the protocol implementation:

```text
Synchronization
Protocol version
Message type
Sequence
Node status
Velocity
Position
Error
CRC
```

The status bits represent conditions such as:

```text
communication/link
watchdog
enabled
running
fault
```

The position and velocity are obtained using atomic protection because they can also be modified by the Timer1 interrupt.

---

# 48. Atomic access

Motion variables are accessed both by:

```text
main execution context
```

and:

```text
Timer1 interrupt
```

Consequently, values such as:

```text
position
applied velocity
running state
```

must be handled atomically when copied or modified across execution contexts.

The implementation uses AVR atomic/interrupt-protection mechanisms for this purpose.

---

# 49. CRC

The communication protocol uses:

```text
CRC16/Modbus
```

The CRC covers the frame contents excluding the CRC field itself.

The general process is:

```text
Frame payload
      │
      ▼
CRC calculation
      │
      ▼
Append CRC
      │
      ▼
Transmit
```

On reception:

```text
Received payload
      │
      ▼
Recalculate CRC
      │
      ▼
Compare
      │
      ├── equal → accept
      │
      └── different → reject
```

The documented protocol identifies CRC failure as error code `1`.

---

# 50. Sequence numbers

The protocol contains a sequence number.

Its purpose is to associate a received command with a corresponding acknowledged or reported state.

The documented three-axis protocol includes:

```text
PLC → Arduino
sequence

Arduino → PLC
acknowledged sequence
```

This is particularly useful for diagnostics because the communication layer can determine whether the returned state corresponds to the expected command.

---

# 51. Three-axis masks

The protocol avoids sending separate command frames for every axis.

Instead, one frame contains the state of the three axes.

The control masks use:

```text
bit 0 → Axis1
bit 1 → Axis2
bit 2 → Axis3
```

Thus:

```text
0001 → Axis1
0010 → Axis2
0100 → Axis3
0111 → Axis1 + Axis2 + Axis3
```

This allows simultaneous multi-axis commands through a single communication channel.

---

# 52. Protocol v2 frame organization

The supplied protocol-v2 document defines the following PLC-to-Arduino frame:

| Bytes | Field |
|---|---|
| 0–1 | `A5 5A` |
| 2 | Version |
| 3 | Type |
| 4–5 | Sequence |
| 6–7 | Watchdog |
| 8–9 | Enable mask |
| 10–11 | Quick-stop mask |
| 12–13 | Reset-position mask |
| 14–17 | Velocity Axis1 |
| 18–21 | Velocity Axis2 |
| 22–25 | Velocity Axis3 |
| 26–27 | CRC |

The return frame is:

| Bytes | Field |
|---|---|
| 0–1 | `5A A5` |
| 2 | Version |
| 3 | Type |
| 4–5 | Acknowledged sequence |
| 6–7 | Node state |
| 8–9 | Node error |
| 10–11 | Ready mask |
| 12–13 | Error mask |
| 14–17 | Position Axis1 |
| 18–21 | Position Axis2 |
| 22–25 | Position Axis3 |
| 26–29 | Applied velocity Axis1 |
| 30–33 | Applied velocity Axis2 |
| 34–37 | Applied velocity Axis3 |
| 38–39 | Statusword Axis1 |
| 40–41 | Statusword Axis2 |
| 42–43 | Statusword Axis3 |
| 44–45 | Age of last order |
| 46–47 | CRC |

---

# 53. Error handling

The documented error codes are:

| Code | Condition |
|---|---|
| 0 | No error |
| 1 | CRC |
| 2 | Invalid version |
| 3 | Invalid message type |
| 4 | Watchdog |
| 5 | Velocity limited |
| 6 | Invalid/reserved mask |

Codes `4` and `5` are specified as warnings during the documented phase rather than permanent errors.

---

# 54. Communication lifecycle

The CODESYS communication device has the following lifecycle:

```text
Device creation
      │
      ▼
Initialize()
      │
      ▼
Read COM parameter
      │
      ▼
Configure controller
      │
      ▼
Cyclic operation
      │
      ▼
AfterReadInputs()
      │
      ▼
FB_Arduino3AxisController
      │
      ▼
Serial communication
```

When the device is removed:

```text
FB_Exit()
      │
      ▼
Stop communication
      │
      ▼
Release serial resource
```

When reinitialization is requested:

```text
FB_Reinit()
      │
      ▼
Clear logical state
      │
      ▼
Normal initialization
```

---

# 55. Axis lifecycle

The axis descriptor defines the following calls:

```text
Initialize
BeforeReadInputs
AfterReadInputs
BeforeWriteOutputs
AfterWriteOutputs
```

This allows the axis FB to participate in the normal CODESYS device-cycle architecture without requiring explicit calls from `PLC_PRG`.

The application therefore interacts with the axis through SoftMotion while the device runtime handles the lower-level lifecycle.

---

# 56. Why the communication device is separate from the axes

A fundamental design decision is that the serial connection is owned once.

The incorrect conceptual architecture would be:

```text
Axis1 → Serial
Axis2 → Serial
Axis3 → Serial
```

because several independent instances would compete for the same physical communication resource.

The implemented architecture is:

```text
             Shared serial link
                    │
                    ▼
       DEV_ArduinoMotionCommunication
                    │
             ┌──────┼──────┐
             │      │      │
           Axis1  Axis2  Axis3
```

The communication device therefore acts as the resource owner, while the axes represent SoftMotion endpoints.

---

# 57. Separation of abstraction levels

The system can be understood as four abstraction levels.

## Level 1 — Application

```text
MC_Power
MC_MoveAbsolute
MC_Halt
MC_Reset
```

## Level 2 — SoftMotion

```text
AXIS_REF
position scaling
velocity
acceleration
deceleration
axis state
```

## Level 3 — Communication

```text
FB_Arduino3AxisController
UART
CRC
sequence
watchdog
frames
```

## Level 4 — Hardware

```text
Timer1
GPIO
STEP
DIR
ENABLE
coils
```

The main purpose of the library is to keep these levels separated.

---

# 58. Communication versus motion timing

One of the most important characteristics of the Arduino implementation is the separation between communication and pulse generation.

The architecture is:

```text
                Arduino
                   │
        ┌──────────┴──────────┐
        │                     │
     Serial                 Timer1
        │                     │
        ▼                     ▼
   Receive command       Generate steps
        │                     │
        └──────────┬──────────┘
                   ▼
              Motion state
```

Serial communication therefore does not directly determine when each motor step occurs.

The Timer1 interrupt is responsible for the time-critical motion generation.

---

# 59. Position feedback limitation

The Arduino position is based on generated steps.

This means that the system can know:

```text
How many steps the controller generated
```

but not necessarily:

```text
How far the mechanical system actually moved
```

without additional feedback hardware.

The supplied documentation explicitly states that, without an encoder, mechanical missed steps cannot be detected.

This should be considered when interpreting following-error or position information at the application level.

---

# 60. Diagnostic philosophy

The diagnostic variables are distributed so that problems can be classified.

A useful interpretation is:

```text
xReady = FALSE
        ↓
Initialization/device problem

xReady = TRUE
xCommunicationValid = FALSE
        ↓
Communication problem

xCommunicationValid = TRUE
CRC errors > 0
        ↓
Data integrity problem

xNodeWatchdogOk = FALSE
        ↓
Command reception timeout

xNodeWarning = TRUE
        ↓
Non-fatal node condition

xNodeFault = TRUE
        ↓
Node fault condition
```

The exact interpretation of a particular state must follow the implementation of the controller and node.

---

# 61. Axis 1, Axis 2 and Axis 3 repetition

The three axis implementations should not be considered three independent algorithms.

Their architecture is common:

```text
AXIS_REF_ArduinoStepperAxisX
```

with:

```text
Axis X scaling
Axis X channel
Axis X device name
Axis X hardware assignment
```

The descriptor supplied for Axis1 associates it with channel 1 and the shared three-axis node.

Axis2 and Axis3 follow the same pattern with their corresponding channel/device identifiers.

Consequently, modifications to the common axis implementation should be reviewed against all three device descriptions.

---

# 62. Example application architecture

The example application intentionally demonstrates only the user-facing SoftMotion layer.

Its structure is:

```text
PLC_PRG
│
├── Axis1
│   ├── Reset
│   ├── Power
│   ├── MoveAbsolute
│   └── Halt
│
├── Axis2
│   ├── Reset
│   ├── Power
│   ├── MoveAbsolute
│   └── Halt
│
└── Axis3
    ├── Reset
    ├── Power
    ├── MoveAbsolute
    └── Halt
```

There is intentionally no:

```text
UART code
COM handling
CRC code
communication FB
```

inside the example application.

---

# 63. Example test sequence

A complete commissioning test can be performed as follows.

### Communication

Verify:

```text
xReady = TRUE
xCommunicationValid = TRUE
xProtocolReady = TRUE
```

and:

```text
udiRxCrcErrorCount = 0
udiRxFormatErrorCount = 0
```

### Axis 1

```text
Power Axis1
MoveAbsolute Axis1 → 90°
Halt Axis1
```

### Axis 2

```text
Power Axis2
MoveAbsolute Axis2 → -90°
Halt Axis2
```

### Axis 3

```text
Power Axis3
MoveAbsolute Axis3 → 45°
Halt Axis3
```

### Simultaneous operation

Power and command multiple axes within the same application cycle.

The supplied installation notes explicitly recommend testing the individual axes and then simultaneous operation.

---

# 64. COM-port test

The communication parameter can be tested independently of motion.

With the motors stopped:

```text
COM = 33
```

should make the current communication configuration invalid if the actual node remains connected to another port.

Restoring:

```text
COM = 3
```

should restore communication when COM3 is the actual port.

This validates that the device-description parameter is being propagated through:

```text
Device Description
        ↓
Initialize()
        ↓
uiComPortNumber
        ↓
FB_Arduino3AxisController
        ↓
Serial communication
```

The supplied project instructions define this as an explicit commissioning test.

---

# 65. Version separation

The supplied material contains two protocol descriptions that should remain distinguishable in the repository.

The protocol document named:

```text
09_PROTOCOLO_BINARIO_V2.md
```

defines 28-byte and 48-byte frames.

The supplied `FastMotionNode` documentation describes:

```text
16-byte command frame
20-byte status frame
```

Therefore, the documentation must not silently treat these formats as identical.

They should be represented as separate implementation/protocol stages unless the complete source of the communication controller establishes a direct correspondence.

---

# 66. Main design advantages

The implemented architecture provides several important advantages.

## Encapsulation

The application does not implement communication details.

## Reusability

The same communication device can serve the three SoftMotion axes.

## SoftMotion integration

The axes are presented to the application as standard SoftMotion devices.

## Centralized communication

Only one component owns the serial connection.

## Hardware-independent application layer

The PLC program works in engineering units rather than GPIO or step counts.

## Deterministic motion generation

The Arduino uses Timer1 independently from the serial polling process.

## Diagnostics

Communication, CRC, watchdog and node states are exposed to CODESYS.

---

# 67. Main limitations

The current implementation has several limitations that should be explicitly documented.

### No mechanical position feedback

The position is estimated from generated steps.

### Step loss cannot be detected

Without an encoder, mechanical missed steps are not observable by the node.

### Current communication parameters

The CODESYS device currently exposes only the COM-port number; baudrate and watchdog are fixed by the implementation.

### Arduino hardware limitation

The supplied node targets AVR Arduino UNO/Nano-class hardware.

### Protocol-version distinction

The supplied documentation contains both a 16/20-byte node protocol description and a 28/48-byte protocol-v2 specification. These must not be merged without corresponding implementation evidence.

---

# 68. Final execution model

The complete operation can finally be summarized as:

```text
                 USER APPLICATION
                       │
                       ▼
              PLCopen / SoftMotion
                       │
             ┌─────────┼─────────┐
             ▼         ▼         ▼
           Axis1     Axis2     Axis3
             │         │         │
             └─────────┼─────────┘
                       ▼
        DEV_ArduinoMotionCommunication
                       │
                       ▼
             FB_Arduino3AxisController
                       │
                       ▼
                 Binary UART
                       │
                       ▼
               FastMotionNode
                       │
                       ▼
                    Timer1
                       │
                       ▼
                 GPIO / STEP-DIR
                       │
                       ▼
                 Stepper motors
```

The resulting abstraction is:

```text
User
 │
 │ SoftMotion commands
 ▼
CODESYS
 │
 │ Device-managed communication
 ▼
Arduino
 │
 │ Hardware-timed pulses
 ▼
Motion system
```

The principal contribution of the architecture is therefore not merely the transmission of movement commands between CODESYS and Arduino, but the integration of a custom stepper-motor controller into the standard CODESYS SoftMotion device model while keeping the communication mechanism transparent to the PLC application.