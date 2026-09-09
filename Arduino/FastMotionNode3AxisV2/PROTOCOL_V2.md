# Protocolo binario FastMotionNode3Axis v2

UART **115200, 8N1**, enteros **little-endian**, CRC16/Modbus.

## PLC -> Arduino: 28 bytes

| Bytes | Campo | Tipo |
|---:|---|---|
| 0-1 | Cabecera `A5 5A` | 2 BYTE |
| 2 | Versión `2` | BYTE |
| 3 | Tipo `1` | BYTE |
| 4-5 | Secuencia | UINT |
| 6-7 | Watchdog, ms | UINT |
| 8-9 | Enable mask | WORD |
| 10-11 | Quick-stop mask | WORD |
| 12-13 | Reset-position mask | WORD |
| 14-17 | Velocidad eje 1, pasos/s | DINT |
| 18-21 | Velocidad eje 2, pasos/s | DINT |
| 22-25 | Velocidad eje 3, pasos/s | DINT |
| 26-27 | CRC sobre bytes 0-25 | WORD |

En las máscaras, bit 0=eje 1, bit 1=eje 2 y bit 2=eje 3.

## Arduino -> PLC: 48 bytes

| Bytes | Campo | Tipo |
|---:|---|---|
| 0-1 | Cabecera `5A A5` | 2 BYTE |
| 2 | Versión `2` | BYTE |
| 3 | Tipo `2` | BYTE |
| 4-5 | Secuencia reconocida | UINT |
| 6-7 | Estado general del nodo | WORD |
| 8-9 | Error general del nodo | UINT |
| 10-11 | Ready mask | WORD |
| 12-13 | Error mask | WORD |
| 14-17 | Posición eje 1, pasos | DINT |
| 18-21 | Posición eje 2, pasos | DINT |
| 22-25 | Posición eje 3, pasos | DINT |
| 26-29 | Velocidad aplicada eje 1 | DINT |
| 30-33 | Velocidad aplicada eje 2 | DINT |
| 34-37 | Velocidad aplicada eje 3 | DINT |
| 38-39 | Statusword eje 1 | WORD |
| 40-41 | Statusword eje 2 | WORD |
| 42-43 | Statusword eje 3 | WORD |
| 44-45 | Edad de última orden válida, ms | UINT |
| 46-47 | CRC sobre bytes 0-45 | WORD |

## Errores

- 0: ninguno.
- 1: CRC incorrecto.
- 2: versión incorrecta.
- 3: tipo de trama incorrecto.
- 4: watchdog vencido.
- 5: velocidad limitada a ±4000 pasos/s.
- 6: máscara no válida/reservado.

El watchdog produce parada segura. No debe convertirse directamente en un
`bExternalError` permanente de SoftMotion durante la secuencia de reconexión.
