# SMART_PONG_PORT_PLAN

## Scope
Port Smart Pong onto the existing LVGL/FreeRTOS graphics firmware baseline.

## Architecture Direction
- Keep CM33 secure/non-secure boot flow unchanged
- Implement game logic in CM55 app layer only
- Use existing display/touch ports (`lv_port_disp`, `lv_port_indev`)

## Planned Modules (CM55)
- `app/EdgeAI_Smart_Pong_demo_Infineon_E8_Eval_Kit/smart_pong_app.c` : app entry and screen composition
- `app/EdgeAI_Smart_Pong_demo_Infineon_E8_Eval_Kit/smart_pong_game.c` : ball, paddles, collisions, scoring
- `app/EdgeAI_Smart_Pong_demo_Infineon_E8_Eval_Kit/smart_pong_input.c` : touch-to-paddle mapping
- `app/EdgeAI_Smart_Pong_demo_Infineon_E8_Eval_Kit/smart_pong_render.c` : LVGL draw/update helpers

## Frame Timing
- Target 60 FPS UI/game tick where possible
- Start with LVGL timer-driven updates and profile CPU load

## Bring-Up Sequence
1. Static Smart Pong prep screen (done)
2. Paddle rectangles + center line + score labels
3. Deterministic ball movement update tick
4. Collision with paddles and walls
5. Touch input for player paddle
6. Optional AI paddle and sound effects

## Done Definition (Phase 1)
- Boots reliably on EPC2 + 4.3-inch panel
- Responsive paddle movement from touch
- Score increments and reset flow works
- Reflash/run documented in runbook
