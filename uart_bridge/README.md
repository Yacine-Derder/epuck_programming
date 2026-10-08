# E-puck UART2 bridge and wiring diagnostic

This firmware assumes the intended, correctly crossed hardware UART connection.
It contains no software UART or pin-swap workaround. The main algorithms run on
the STM32; the dsPIC executes motor commands and samples sensors locally.

## Intended wiring

| STM32F405 | Connector | E-puck dsPIC30F6014A |
| --- | --- | --- |
| PA9 USART1 TX, through R18 | JE1 pin 4 | RF4 U2RX, MCU pin 39 |
| PA10 USART1 RX, through R19 | JE1 pin 2 | RF5 U2TX, MCU pin 40 |
| Ground | JE2 ground | Ground |

UART2 on the dsPIC and USART1 on STM32: **115200 baud, 8 data bits, no parity,
1 stop bit, no hardware flow control**, normal/non-inverted polarity.

The supplied turret schematic instead routes PA9 to JE1 pin 2 and PA10 to pin 4.
Do not drive both physically connected TX outputs against each other. For the
first listen-only test on that board, keep STM32 PA9 high-impedance (GPIO input,
not USART1 TX) and inspect the dsPIC beacon with a scope or logic analyzer. This
is only a diagnostic; normal operation remains hardware UART on the corrected
board, without changing the firmware's UART roles.

## Build and upload

Put this folder alongside `comm_module` at the repository root:

```bash
cd /home/derder/Documents/epuck_programming/uart_bridge
make test
make
```

The build generates `uart_bridge.hex` using the repository's existing PIC30/XC16
compiler wrappers. Only the selected legacy library sources are rebuilt inside
this folder; the existing library objects are left untouched.

For robot 76:

```bash
cd /home/derder/Documents/epuck_programming
sudo python3 bind_epucks.py --no-pair --replace 76
sudo epuckupload -f uart_bridge/uart_bridge.hex 76
```

Enter Bluetooth PIN `0076` if requested, then press the robot's blue reset button
when the uploader shows dots. Uploading still uses the Bluetooth bootloader/UART1.

## First communication test

1. Motor outputs start stopped. LED0 toggles every 500 ms (unless that LED is faulty).
2. UART2 emits `BOOT EPUCK_UART_BRIDGE 1` after reset and
   `BEACON EPUCK_UART_BRIDGE 1` once per second, even without incoming commands.
3. UART1/Bluetooth emits boot information and a `STATUS` line once per second.
   Open it with `python3 monitor_epucks.py 76` or minicom at 115200 baud.
4. On the STM32, configure normal USART1 on PA9/PA10 and send the bytes `PING\n`.
   The dsPIC returns `PONG\r\n` on UART2. Unsolicited beacons can arrive between
   replies, so receive and parse complete newline-terminated lines.
5. `stm32_example.c/.h` provide an integration example: call `epuck_link_init()`
   after `MX_USART1_UART_Init()`, then `epuck_link_task()` frequently in the main
   loop. Enable USART1's NVIC interrupt; merge the RX/error callbacks if your
   project already defines them. Watch `epuck_rx_bytes`, `epuck_pong_count`, and
   `epuck_last_line` in the debugger. This example is separate from the dsPIC
   Makefile and has not been built against your STM32 project.

For physically crossed-wrong lines, do the listen-only measurement in the wiring
section before trying full-duplex operation. A missing PONG alone does not prove
inversion: verify clock/baud, common ground, successful boot, and actual signals.

## How to establish the routing fault

With STM32 PA9 high-impedance, scope JE1 pin 2 / dsPIC RF5 and decode its beacon at
115200 baud. Measure both STM32 PA9 and PA10 at their pads:

- Intended board: the dsPIC beacon reaches STM32 PA10 (USART1 RX).
- Supplied schematic's routing: the dsPIC beacon reaches STM32 PA9 (USART1 TX).

Confirm the return path with an unpowered continuity check: PA10 through R19
should reach dsPIC RF5/JE1 pin 2; PA9 through R18 should reach RF4/JE1 pin 4.
Series resistors mean a resistance reading can be more useful than a continuity
beeper. This distinguishes an actual routing error from a firmware configuration
problem. Reordering the PCB should use these verified net connections.

Bluetooth STATUS shows `rx` bytes observed by UART2, `err` framing/parity/overrun
errors, `drop` receive buffer drops, `cmd` recognized commands, `tx` bytes placed
in UART2's FIFO, `txdrop` output queue drops, `arm`, and `stream` period. Transmit
counts show local activity, not proof the STM32 received it. Counters and the
millisecond timestamp are 16-bit and wrap; time wraps about every 65.5 seconds.

## Commands from the STM32

Commands are uppercase ASCII, terminated with LF (`\n`); CRLF is also accepted.
The longest line is 63 characters excluding its newline. Invalid/overlong lines
return an error and do not execute a motor command.

| Send | Response / behavior |
| --- | --- |
| `PING\n` | `PONG\r\n` |
| `STATUS\n` | UART2 status counters |
| `R\n` | One `DATA` record |
| `STREAM 100\n` | `OK STREAM`, followed by DATA about every 100 ms (10 Hz) |
| `STREAM 0\n` | Stop streaming |
| `ARM\n` | Stop motors, then enable motor commands for 250 ms |
| `M 200 200\n` | Set left/right speed to 200 steps/s, acknowledge `OK M` |
| `HB\n` | Refresh the motor timeout if armed |
| `STOP\n` | Stop motors and disarm |

Send ARM before M, within the 250 ms window. While motors should run, send a new
M command or HB at least every 100 ms. ARM always first stops the motors; do not
use ARM as a heartbeat. A timeout or UART receive loss stops and disarms motors;
a fresh ARM is then required. PING and sensor requests do not refresh motor motion.
Try motor commands only after communication is established, with wheels lifted.

Speeds are limited to -1000..1000 steps/s. Streaming accepts 50..10000 ms or zero,
so the current ASCII implementation supports at most 20 Hz telemetry. The bridge
rejects unsupported periods instead of claiming arbitrarily high sampling rates.

DATA format:

```text
DATA time_ms prox0 ... prox7 light0 ... light7 acc_x acc_y acc_z steps_left steps_right
```

Proximity/light/acceleration are raw library/ADC values, not calibrated metres,
lux or m/s². Proximity values come from the background Timer2 scan; accelerometer
values are read during this report. The timestamp marks report acquisition start;
the channels are not sampled simultaneously. Motor counters are commanded steps,
not encoder measurements. Requests can repeat an existing proximity measurement.

The accelerometer requires the JE1 sensor bridges **33-34 (X), 35-36 (Y),
37-38 (Z)**. If these paths are absent on the turret, the corresponding ADC inputs
are disconnected and the data cannot be treated as valid acceleration.

## Firmware design and limits

- UART2 has a new interrupt-driven 256-byte RX ring and 512-byte TX queue. The
  stock legacy UART2 routines are not linked.
- UART1 has a separate TX queue for independent diagnostic reporting.
- Timer1 supplies the millisecond clock; the selected proximity driver uses
  Timer2; the existing motor driver uses Timers4/5. Camera, radio and microphone
  drivers are not included. Check timer/ADC conflicts before adding them.
- Motor timeout/packet processing never waits for an entire incoming line or a
  UART transmit buffer to drain. Queues drop complete outgoing lines when full.
- This is a bring-up protocol without CRC or authenticated commands. For research
  operation, add a framed binary protocol, CRC, sequence numbers and better
  acquisition timestamps after electrical communication has been established.

## Local validation

The actual dsPIC firmware was cross-compiled and linked to produce the HEX file.
Host-side tests exercise recognized commands, signed motor limits, stream limits,
malformed input, extra arguments and numeric overflow. Compiler warnings originate
from the existing proximity ISR PSV annotation and legacy linker startup defaults.
Physical UART operation, motor timing, sensor validity and your STM32 integration
still require bench testing. No firmware was uploaded to a robot automatically.
