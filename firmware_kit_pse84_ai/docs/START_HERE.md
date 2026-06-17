# START_HERE

## Goal
Port Smart Pong onto the PSOC Edge E84 Evaluation Kit (EPC2) using the known-good LVGL graphics pipeline.

## Current Baseline (2026-03-21)
- Board + display boot confirmed with flashed firmware
- Display: Waveshare 4.3-inch (800x480)
- Active app mode: Smart Pong prep screen (`APP_SMART_PONG_MODE=1`)
- Screen confirmed on hardware: `SMART PONG - PORT PREP`

## Read Order
1. `../README.md`
2. `PROJECT_STATE.md`
3. `HARDWARE_SETUP.md`
4. `OPS_RUNBOOK.md`
5. `RESTORE_POINTS.md`
6. `SMART_PONG_PORT_PLAN.md`
7. `TODO.md`

## Source Of Truth
- Runtime entry: `../proj_cm55/main.c`
- Smart Pong scaffold: `../proj_cm55/app/EdgeAI_Smart_Pong_demo_Infineon_E8_Eval_Kit/`
- Display selection: `../common.mk` (`CONFIG_DISPLAY`)

## Mode Switch
- Smart Pong prep screen (active): `APP_SMART_PONG_MODE=1`
- Music demo fallback: set `APP_SMART_PONG_MODE=0`
