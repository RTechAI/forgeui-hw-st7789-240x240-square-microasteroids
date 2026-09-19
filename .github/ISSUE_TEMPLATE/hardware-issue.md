---
name: MicroAsteroids hardware or gameplay issue
about: Report a ForgeUI MicroAsteroids hardware, control, build, or gameplay problem
title: "[MicroAsteroids] "
labels: hardware
assignees: ""
---

## Hardware

- ESP32 model and board:
- ST7789 display/controller:
- PCB or module marking:

## Display wiring

| Display signal | Connected pin / voltage |
| --- | --- |
| GND | |
| VCC | |
| SCL / SCLK | |
| SDA / MOSI | |
| RES / RST | |
| DC | |
| CS | |
| BLK | |

State whether MISO is connected. For the tested 1.54-inch square module, BLK is wired to 3.3V.

## Joystick wiring and calibration

| Joystick signal | Connected pin / voltage |
| --- | --- |
| SW | |
| VRy | |
| VRx | |
| Supply | |
| GND | |

- Observed centre calibration values (`Joystick centre X=... Y=...`):
- Rotation behaviour:
- Forward thrust behaviour:
- Reverse/braking behaviour:
- Fire-button behaviour:

## Game behaviour

- Current game state (title, playing, `SHIP LOST`, or `MISSION LOST`):
- Asteroid rendering and movement:
- Bullet collision and asteroid-splitting behaviour:
- HUD, radar, or threat-indicator behaviour:

## Software and results

- PlatformIO version:
- `espressif32` platform version:
- Arduino_GFX version:
- Build: PASS / FAIL
- Flash: PASS / FAIL

## Evidence

Attach a physical photo of the board, display, joystick, and wiring. Include short relevant build, upload, or serial logs; remove unrelated output and secrets.
