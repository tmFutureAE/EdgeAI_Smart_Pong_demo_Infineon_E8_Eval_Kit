# PROJECT_STATE

## Project
PSOC Edge E84 Eval (EPC2), Smart Pong release path.

## Date
2026-03-25

## Working Hardware
- Kit: `KIT_PSE84_EVAL_EPC2`
- MCU detected during flash: `PSE846GPS2DBZC4A`
- Display: Waveshare 4.3-inch DSI panel on `J39`

## Build Configuration
- `TARGET=KIT_PSE84_EVAL_EPC2`
- `TOOLCHAIN=GCC_ARM`
- `CONFIG_DISPLAY=W4P3INCH_DISP`
- `APP_SMART_PONG_MODE=1`

## Current State
- Smart Pong repo is standalone and no longer depends on insulin app sources.
- Build and flash verified on hardware on `2026-03-25`.
- Program artifact: `firmware_kit_epc2/build/app_combined.hex`.
