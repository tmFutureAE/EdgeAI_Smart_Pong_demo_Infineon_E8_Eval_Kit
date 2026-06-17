# HARDWARE_SETUP

## Target Kit
PSOC Edge E84 Evaluation Kit (EPC2)

## Required Board Settings (for this app)
- `BOOT SW`: HIGH/ON
- `J20`: NC/open
- `J21`: NC/open

## Display Wiring (4.3-inch)
- Display: Waveshare 4.3-inch DSI 800x480
- Connect display ribbon to board connector `J39`
- Keep cable orientation correct and fully seated

## Host Connection
- Connect USB to KitProg3 port
- Confirm debugger enumerates as KitProg3 CMSIS-DAP

## Quick Triage For Black Screen
1. Re-check `BOOT SW`, `J20`, `J21`
2. Confirm `CONFIG_DISPLAY=W4P3INCH_DISP` in `common.mk`
3. Reflash using `make program TOOLCHAIN=GCC_ARM`
4. Check serial output at 115200 8N1
