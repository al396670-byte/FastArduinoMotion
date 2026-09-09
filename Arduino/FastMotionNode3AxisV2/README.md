# FastMotionNode3Axis 2.0.1

Library for **Arduino UNO/Nano classic (ATmega328P)** that controls three 28BYJ-48 motors through three ULN2003 driver boards using a single binary serial communication link with CODESYS.

## Frozen hardware

| Axis | Arduino | ULN2003 |
|---|---|---|
| 1 | D2, D3, D4, D5 | IN1, IN2, IN3, IN4 |
| 2 | D6, D7, D8, D9 | IN1, IN2, IN3, IN4 |
| 3 | D10, D11, D12, D13 | IN1, IN2, IN3, IN4 |

- D0 and D1 are reserved for USB-serial communication.
- All three motors operate in **half-step** mode.
- Nominal educational value: **4096 step events per revolution of the output shaft**.
- The Arduino works internally in steps and steps/s; the 4096/360 conversion is configured later in SoftMotion.
- Do not power three motors from the Arduino 5 V pin. Use an external 5 V power supply for the ULN2003 boards and connect the power supply GND to the Arduino GND.

## Installation

In Arduino IDE:

`Sketch -> Include Library -> Add .ZIP Library...`

Select the complete ZIP file and restart Arduino IDE.

## Firmware to be loaded for CODESYS

`File -> Examples -> FastMotionNode3Axis -> 02_ThreeAxes_BinaryNode`

Compile and upload the firmware. Then completely close the Serial Monitor and Serial Plotter.

## Physical test without CODESYS

Open `01_ThreeAxes_AutonomousTest`. It repeats six phases of 3 s each:

1. Axis 1 at +200 steps/s;
2. Axis 2 at +200 steps/s;
3. Axis 3 at +200 steps/s;
4. All three axes at +200, -120, and +60 steps/s;
5. Zero velocity with the coils energized;
6. Axes disabled and coils released.

## Real-time operation

Timer1 runs at 20 kHz. Each axis uses its own phase accumulator. The timing of the steps does not depend on `loop()`, USB, Windows, or the CODESYS cycle.

## Protocol

- 115200 bit/s, 8N1.
- Little-endian.
- CRC16/Modbus.
- Protocol version: 2.
- PLC -> Arduino: 28 bytes.
- Arduino -> PLC: 48 bytes.

The exact frame map is documented in `PROTOCOL_V2.md` and in `src/FastMotionProtocol3Axis.h`.

## Changes from 2.0.0 to 2.0.1

This is a clean reconstruction of the codebase used in the project. It maintains protocol v2 and adds safer recovery behavior: when the watchdog expires, the Arduino stops and releases the motors, while also preventing old error responses from accumulating in the serial buffer. When a new valid command arrives, the watchdog state is cleared before the next response is sent. This reduces false `External error` conditions in CODESYS after a Stop -> Run transition.