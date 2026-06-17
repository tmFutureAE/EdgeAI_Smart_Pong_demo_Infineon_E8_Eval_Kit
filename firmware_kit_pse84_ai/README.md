# Smart Pong

Smart Pong is a PSOC Edge E84 AI Kit firmware project for the Infineon
`KIT_PSE84_AI` board with the Waveshare 4.3-inch Raspberry Pi DSI
800x480 display.

This project is derived from the PSOC Edge graphics LVGL example, but this
folder is configured specifically for the Edge AI Kit and the Smart Pong
application. The project uses a three-core ModusToolbox application structure:

- `proj_cm33_s`: CM33 secure application
- `proj_cm33_ns`: CM33 non-secure application
- `proj_cm55`: CM55 Smart Pong application

The application is built and programmed as a combined image. Extended boot starts
the CM33 secure image from external flash, then launches CM33 non-secure, which
starts the CM55 application.

## Supported Hardware

- Infineon PSOC Edge E84 AI Kit: `KIT_PSE84_AI`
- Local application BSP target: `APP_KIT_PSE84_AI`
- Waveshare 4.3-inch Raspberry Pi DSI display, 800x480 pixels

No other display option is supported by this project configuration.

## Display Connection

Connect the Waveshare 4.3-inch Raspberry Pi DSI display to the Edge AI Kit RPi
MIPI DSI connector:

| Kit | Display connector |
| --- | --- |
| PSOC Edge E84 AI Kit | `J10` |

The project is configured for:

```make
CONFIG_DISPLAY=W4P3INCH_DISP
```

## Software Requirements

- ModusToolbox 3.7 or later
- GNU Arm Embedded Compiler, default `GCC_ARM`
- KitProg3 USB connection for programming and UART output

## Build And Program

From the `firmware_kit_pse84_ai` folder:

```bash
make getlibs
make build
```

To regenerate Eclipse for ModusToolbox metadata:

```bash
make eclipse
```

Then import/open the application in Eclipse for ModusToolbox and program the
application.

The expected build target is:

```make
TARGET=APP_KIT_PSE84_AI
```

The CM55 output should build under:

```text
proj_cm55/build/APP_KIT_PSE84_AI/Debug/
```

## UART Startup Output

Open the KitProg3 UART at:

- 115200 baud
- 8 data bits
- no parity
- 1 stop bit

On boot, the application prints:

```text
EdgeAI Smart Pong v1.0.0
```

## Application Notes

- Smart Pong starts automatically after programming.
- The display resolution is fixed at 800x480.
- The application runs with LVGL software rendering enabled.
- The local `APP_KIT_PSE84_AI` BSP uses normal sleep mode so GFXSS and MIPI DSI
  remain active during the game.
- The game simulation is fixed at 60 FPS to match the 4.3-inch display refresh.

## Project Files

Smart Pong application code lives in:

```text
proj_cm55/app/EdgeAI_Smart_Pong_demo_Infineon_E8_Eval_Kit/
```

Useful project documentation:

- `docs/START_HERE.md`
- `docs/HARDWARE_SETUP.md`
- `docs/OPS_RUNBOOK.md`
- `docs/PROJECT_STATE.md`

## Repository Folder

This Edge AI Kit project lives in:

```text
firmware_kit_pse84_ai/
```
