# RESTORE_POINTS

## Purpose
Known-good checkpoints for fast recovery when development changes break boot, display, or flashing.

## Active Restore Tags
- `golden-e8-smart-pong`
- `golden-e8-smart-pong-20260325`
- `failsafe-e8-smart-pong`
- `failsafe-e8-smart-pong-20260325`

## Latest Verified Baseline (2026-03-25)
- Board: `PSE846GPS2DBZC4A` (Rev `B0`)
- Build:
  - `make build TOOLCHAIN=GCC_ARM CONFIG_DISPLAY=W4P3INCH_DISP -j8`
- Flash:
  - `make program TOOLCHAIN=GCC_ARM CONFIG_DISPLAY=W4P3INCH_DISP`
- Artifact:
  - `build/app_combined.hex`

## Restore Procedure
1. Fetch tags:
   ```bash
   git fetch --tags
   ```
2. Checkout restore point:
   ```bash
   git checkout <tag>
   ```
3. Rebuild and program:
   ```bash
   make clean TOOLCHAIN=GCC_ARM
   make program TOOLCHAIN=GCC_ARM CONFIG_DISPLAY=W4P3INCH_DISP
   ```
