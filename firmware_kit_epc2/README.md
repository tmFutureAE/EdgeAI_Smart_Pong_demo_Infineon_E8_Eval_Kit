# PSOC&trade; Edge MCU: Graphics LVGL demo

This code example demonstrates how to render a 2D graphics demo using the Light and Versatile Graphics (LVGL) on the PSOC&trade; Edge MCU with the following supported displays.

- Waveshare 4.3-inch Raspberry Pi DSI 800x480 pixel display
- Waveshare 7-inch Raspberry Pi DSI LCD C 1024x600 pixel display
- 10.1 inch 1024x600 TFT display (WF101JTYAHMNB0)

The 2D graphics showcase a music player application, which is featured as one of the standard demos on the LVGL page. The LCD is connected through the MIPI Display Serial Interface (DSI), and the code is designed to operate in a FreeRTOS environment.

This code example has a three project structure: CM33 secure, CM33 non-secure, and CM55 projects. All three projects are programmed to the external QSPI flash and executed in Execute in Place (XIP) mode. Extended boot launches the CM33 secure project from a fixed location in the external flash, which then configures the protection settings and launches the CM33 non-secure application. Additionally, CM33 non-secure application enables CM55 CPU and launches the CM55 application. The CM55 application implements the logic for this code example.

> **Note:** This application builds for the 4.3-inch display by default.

   ```
   CONFIG_DISPLAY=W4P3INCH_DISP
   ```

## Smart Pong Porting Workspace

This repo is prepared for Smart Pong porting on EPC2. Start with:

1. `start_here.md`
2. `docs/START_HERE.md`
3. `docs/SMART_PONG_PORT_PLAN.md`

Smart Pong prep code lives in:

- `proj_cm55/app/EdgeAI_Smart_Pong_demo_Infineon_E8_Eval_Kit/`

To run Smart Pong prep screen instead of the music demo, set:

```make
DEFINES+=APP_SMART_PONG_MODE=1
```

in `proj_cm55/Makefile`, then rebuild and reprogram.

   To build the application for the 7-inch display, set the following macro in *common.mk*:

   ```
   CONFIG_DISPLAY=WS7P0DSI_RPI_DISP
   ```

   OR

   To build the application for the 10.1-inch display, set the following macro in *common.mk*:

   ```
   CONFIG_DISPLAY=WF101JTYAHMNB0_DISP
   ```

[View this README on GitHub.](https://github.com/Infineon/mtb-example-psoc-edge-gfx-lvgl-demo)

[Provide feedback on this code example.](https://yourvoice.infineon.com/jfe/form/SV_1NTns53sK2yiljn?Q_EED=eyJVbmlxdWUgRG9jIElkIjoiQ0UyMzkyNTkiLCJTcGVjIE51bWJlciI6IjAwMi0zOTI1OSIsIkRvYyBUaXRsZSI6IlBTT0MmdHJhZGU7IEVkZ2UgTUNVOiBHcmFwaGljcyBMVkdMIGRlbW8iLCJyaWQiOiJzYW5qZWV2Lm1hanVtZGFyQGluZmluZW9uLmNvbSIsIkRvYyB2ZXJzaW9uIjoiMi4zLjAiLCJEb2MgTGFuZ3VhZ2UiOiJFbmdsaXNoIiwiRG9jIERpdmlzaW9uIjoiTUNEIiwiRG9jIEJVIjoiSUNXIiwiRG9jIEZhbWlseSI6IlBTT0MifQ==)

See the [Design and implementation](docs/design_and_implementation.md) for the functional description of this code example.


## Requirements

- [ModusToolbox&trade;](https://www.infineon.com/modustoolbox) v3.7 or later (tested with v3.7)
- Board support package (BSP) minimum required version: 1.0.0
- Programming language: C
- Associated parts: All [PSOC&trade; Edge MCU](https://www.infineon.com/products/microcontroller/32-bit-psoc-arm-cortex/32-bit-psoc-edge-arm) parts


## Supported toolchains (make variable 'TOOLCHAIN')

- GNU Arm&reg; Embedded Compiler v14.2.1 (`GCC_ARM`) – Default value of `TOOLCHAIN`
- Arm&reg; Compiler v6.22 (`ARM`)
- IAR C/C++ Compiler v9.50.2 (`IAR`)
- LLVM Embedded Toolchain for Arm&reg; v19.1.5 (`LLVM_ARM`)


## Supported kits (make variable 'TARGET')

- [PSOC&trade; Edge E84 Evaluation Kit](https://www.infineon.com/KIT_PSE84_EVAL) (`KIT_PSE84_EVAL_EPC2`) – Default value of `TARGET`
- [PSOC&trade; Edge E84 Evaluation Kit](https://www.infineon.com/KIT_PSE84_EVAL) (`KIT_PSE84_EVAL_EPC4`)
- [PSOC&trade; Edge E84 AI Kit](https://www.infineon.com/KIT_PSE84_AI) (`KIT_PSE84_AI`)


## Hardware setup

This example uses the board's default configuration. See the kit user guide to ensure that the board is configured correctly.

Ensure the following jumper and pin configuration on board.
- BOOT SW must be in the HIGH/ON position
- J20 and J21 must be in the tristate/not connected (NC) position

> **Note:** This hardware setup is not required for KIT_PSE84_AI.

### Supported display and electrical connection

1. **Waveshare 4.3 inch Raspberry Pi DSI 800*480 pixel display:** This display is supported by default <br>

   Connect the FPC 15-pin cable between the display connector and the PSOC&trade; Edge E84 kit's RPi MIPI DSI connector as shown in **Figure 1** <br>

   **Table 1. Cable connection between display connector and kit**

   Kit's name                                      | DSI connector
   ----------------------------------------------- | --------------
   PSOC&trade; Edge E84 Evaluation Kit             | J39
   PSOC&trade; Edge E84 AI Kit                     | J10

   > **Note:** The PSOC&trade; Edge E84 AI kit supports Waveshare 4.3 inch Raspberry Pi DSI 800*480 pixel display.

   **Figure 1.  Display connection with PSOC&trade; Edge E84 evaluation kit**

   ![](images/display-kit-connection.png)

2. **Waveshare 7-inch Raspberry-Pi DSI LCD C 1024*600 pixel display:** <br>

   In this display, few I2C connections are present on the header named `FAN` on display's hardware, it is highlighted in **Figure 2** <br>

   **Figure 2. Waveshare 7-inch Raspberry Pi DSI LCD (C) display's I2C connection (FAN connector)**

   ![](images/ws7p0dsi_panel_i2c_connection.png)

   Interface the display with the PSOC&trade; Edge E84 Evaluation Kit using the connections outlined in **Table 2** <br>

   **Table 2: PSOC&trade; Edge E84 Evaluation Kit connections**

   Display's Connector | PSOC&trade; Edge E84 Evaluation Kit's connector
   --------------------|----------------------------------------
   DSI connector       | J39
   GND (FAN)           | GND (J41)
   5V  (FAN)           | 5V (J41)
   SCL (FAN)           | I2C_SCL (J41)
   SDA (FAN)           | I2C_SDA (J41)

<br>

3. **10.1 inch 1024*600 pixel TFT LCD (WF101JTYAHMNB0):** This setup requires rework on the PSOC&trade; Edge E84 evaluation kit, and the rework instructions are as follows:

   - **Remove:** R22, R23, R24, R25, R26, R27
   - **Populate:** R28, R29, R30, R31, R32, R33

   **Figure 3. Rework on PSOC™ Edge E84 baseboard**

   ![](images/pse84_kit_mipi_disp_rework.png)

   Interface the display with the PSOC&trade; Edge E84 Evaluation Kit using the connections outlined in **Table 3** <br>

   **Table 3: PSOC&trade; Edge E84 Evaluation Kit connections**

   Display's Connector | PSOC&trade; Edge E84 Evaluation Kit's connector
   --------------------|----------------------------------------
   DSI connector       | J38
   Touch connector     | J37

<br>

## Software setup

See the [ModusToolbox&trade; tools package installation guide](https://www.infineon.com/ModusToolboxInstallguide) for information about installing and configuring the tools package.

Install a terminal emulator if you do not have one. Instructions in this document use [Tera Term](https://teratermproject.github.io/index-en.html).

This example requires no additional software or tools.


## Operation

See [Using the code example](docs/using_the_code_example.md) for instructions on creating a project, opening it in various supported IDEs, and performing tasks, such as building, programming, and debugging the application within the respective IDEs.

1. Connect the selected LCD display to the board according to the instructions given in the [Display setup](#supported-display-and-electrical-connection) section

2. Connect the board to your PC using the provided USB cable through the KitProg3 USB connector

3. Open a terminal program and select the KitProg3 COM port. Set the serial port parameters to 8N1 and 115200 baud

4. In the common makefile - _<application\>/common.mk_, add one of the following set of values in the **variable** `CONFIG_DISPLAY` to enable display and it's corresponding touch driver compilation for the selected display panel. The same information is mentioned in comments in the common makefile

   - **Waveshare 4.3-inch Raspberry-Pi DSI LCD and it's touch panel (FT5406):** `W4P3INCH_DISP` <br> This is enabled by default

   - **Waveshare 7 inch Raspberry Pi DSI LCD (C) Display (DISP_WS7P0DSI_RPI) and it's touch panel (GT911):** `WS7P0DSI_RPI_DISP`

   - **10.1 inch 1024*600 TFT LCD (WF101JTYAHMNB0) and it's touch panel (ILI2511):** `WF101JTYAHMNB0_DISP`

      > **Note:** From the above set, at a time only one display with its touch driver will be enabled in the _common makefile_.

      **Example**:

      To use the Waveshare 7-inch Raspberry Pi DSI LCD (C) display:

      ```
      CONFIG_DISPLAY = WS7P0DSI_RPI_DISP
      ```

      OR

      To use the 10.1 inch WF101JTYAHMNB0 display:

      ```
      CONFIG_DISPLAY = WF101JTYAHMNB0_DISP
      ```

5. Build and program the application

6. After programming, the application starts automatically. Confirm that "PSOC Edge MCU: Graphics LVGL Demo" is displayed on the UART terminal

   **Figure 4. Terminal output on program startup**

   ![](images/terminal-output.png)

7. Observe that the LCD displays a music player demo application. You can use the touch screen to perform various actions such as playing or pausing a track, changing to the next or previous track, and viewing the playlist

   **Figure 5. LVGL demo**

   ![](images/lvgl-demo.png)

   **Figure 6. LVGL music player**

   ![](images/lvgl-music-player.gif)

8. To display CPU usage on the screen, enable the following macro in lv_conf.h:

      ```
      #define LV_USE_SYSMON 1
      ```

   The on-screen position of the CPU usage indicator is determined by the macro:

      ```
      #define LV_USE_PERF_MONITOR_POS LV_ALIGN_BOTTOM_LEFT
      ```

   **Figure 7. LVGL music player with cpu usage**

   ![](images/cpu_usage_display.png)

9. For testing the code example with the other supported display, repeat the above steps. At **Step 4**, enable the display of your choice and then follow the rest of the steps



## Related resources

Resources  | Links
-----------|----------------------------------
Application notes  | [AN235935](https://www.infineon.com/AN235935) – Getting started with PSOC&trade; Edge E8 MCU on ModusToolbox&trade; software <br> [AN239191](https://www.infineon.com/AN239191) – Getting started with graphics on PSOC&trade; Edge MCU
Code examples  | [Using ModusToolbox&trade;](https://github.com/Infineon/Code-Examples-for-ModusToolbox-Software) on GitHub
Device documentation | [PSOC&trade; Edge MCU datasheets](https://www.infineon.com/products/microcontroller/32-bit-psoc-arm-cortex/32-bit-psoc-edge-arm#documents) <br> [PSOC&trade; Edge MCU reference manuals](https://www.infineon.com/products/microcontroller/32-bit-psoc-arm-cortex/32-bit-psoc-edge-arm#documents)
Development kits | Select your kits from the [Evaluation board finder](https://www.infineon.com/cms/en/design-support/finder-selection-tools/product-finder/evaluation-board)
Libraries  | [mtb-dsl-pse8xxgp](https://github.com/Infineon/mtb-dsl-pse8xxgp) – Device support library for PSE8XXGP <br> [retarget-io](https://github.com/Infineon/retarget-io) – Utility library to retarget STDIO messages to a UART port
Tools  | [ModusToolbox&trade;](https://www.infineon.com/modustoolbox) – ModusToolbox&trade; software is a collection of easy-to-use libraries and tools enabling rapid development with Infineon MCUs for applications ranging from wireless and cloud-connected systems, edge AI/ML, embedded sense and control, to wired USB connectivity using PSOC&trade; Industrial/IoT MCUs, AIROC&trade; Wi-Fi and Bluetooth&reg; connectivity devices, XMC&trade; Industrial MCUs, and EZ-USB&trade;/EZ-PD&trade; wired connectivity controllers. ModusToolbox&trade; incorporates a comprehensive set of BSPs, HAL, libraries, configuration tools, and provides support for industry-standard IDEs to fast-track your embedded application development

<br>


## Other resources

Infineon provides a wealth of data at [www.infineon.com](https://www.infineon.com) to help you select the right device, and quickly and effectively integrate it into your design.


## Document history

Document title: *CE239259* - *PSOC&trade; Edge MCU: Graphics LVGL demo*

 Version | Description of change
 ------- | ---------------------
 1.x.0   | New code example <br> Early access release
 2.0.0   | GitHub release
 2.1.0   | Added KIT_PSE84_AI BSP support
 2.2.0   | Patched alpha-premultiplied images assets for widgets demo <br> Provided fix to use target display's actual resolution
 2.3.0   | Updated design files to fix ModusToolbox&trade; v3.7 build warnings
<br>


All referenced product or service names and trademarks are the property of their respective owners.

The Bluetooth&reg; word mark and logos are registered trademarks owned by Bluetooth SIG, Inc., and any use of such marks by Infineon is under license.

PSOC&trade;, formerly known as PSoC&trade;, is a trademark of Infineon Technologies. Any references to PSoC&trade; in this document or others shall be deemed to refer to PSOC&trade;.

---------------------------------------------------------

© Cypress Semiconductor Corporation, 2023-2025. This document is the property of Cypress Semiconductor Corporation, an Infineon Technologies company, and its affiliates ("Cypress").  This document, including any software or firmware included or referenced in this document ("Software"), is owned by Cypress under the intellectual property laws and treaties of the United States and other countries worldwide.  Cypress reserves all rights under such laws and treaties and does not, except as specifically stated in this paragraph, grant any license under its patents, copyrights, trademarks, or other intellectual property rights.  If the Software is not accompanied by a license agreement and you do not otherwise have a written agreement with Cypress governing the use of the Software, then Cypress hereby grants you a personal, non-exclusive, nontransferable license (without the right to sublicense) (1) under its copyright rights in the Software (a) for Software provided in source code form, to modify and reproduce the Software solely for use with Cypress hardware products, only internally within your organization, and (b) to distribute the Software in binary code form externally to end users (either directly or indirectly through resellers and distributors), solely for use on Cypress hardware product units, and (2) under those claims of Cypress's patents that are infringed by the Software (as provided by Cypress, unmodified) to make, use, distribute, and import the Software solely for use with Cypress hardware products.  Any other use, reproduction, modification, translation, or compilation of the Software is prohibited.
<br>
TO THE EXTENT PERMITTED BY APPLICABLE LAW, CYPRESS MAKES NO WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, WITH REGARD TO THIS DOCUMENT OR ANY SOFTWARE OR ACCOMPANYING HARDWARE, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  No computing device can be absolutely secure.  Therefore, despite security measures implemented in Cypress hardware or software products, Cypress shall have no liability arising out of any security breach, such as unauthorized access to or use of a Cypress product. CYPRESS DOES NOT REPRESENT, WARRANT, OR GUARANTEE THAT CYPRESS PRODUCTS, OR SYSTEMS CREATED USING CYPRESS PRODUCTS, WILL BE FREE FROM CORRUPTION, ATTACK, VIRUSES, INTERFERENCE, HACKING, DATA LOSS OR THEFT, OR OTHER SECURITY INTRUSION (collectively, "Security Breach").  Cypress disclaims any liability relating to any Security Breach, and you shall and hereby do release Cypress from any claim, damage, or other liability arising from any Security Breach.  In addition, the products described in these materials may contain design defects or errors known as errata which may cause the product to deviate from published specifications. To the extent permitted by applicable law, Cypress reserves the right to make changes to this document without further notice. Cypress does not assume any liability arising out of the application or use of any product or circuit described in this document. Any information provided in this document, including any sample design information or programming code, is provided only for reference purposes.  It is the responsibility of the user of this document to properly design, program, and test the functionality and safety of any application made of this information and any resulting product.  "High-Risk Device" means any device or system whose failure could cause personal injury, death, or property damage.  Examples of High-Risk Devices are weapons, nuclear installations, surgical implants, and other medical devices.  "Critical Component" means any component of a High-Risk Device whose failure to perform can be reasonably expected to cause, directly or indirectly, the failure of the High-Risk Device, or to affect its safety or effectiveness.  Cypress is not liable, in whole or in part, and you shall and hereby do release Cypress from any claim, damage, or other liability arising from any use of a Cypress product as a Critical Component in a High-Risk Device. You shall indemnify and hold Cypress, including its affiliates, and its directors, officers, employees, agents, distributors, and assigns harmless from and against all claims, costs, damages, and expenses, arising out of any claim, including claims for product liability, personal injury or death, or property damage arising from any use of a Cypress product as a Critical Component in a High-Risk Device. Cypress products are not intended or authorized for use as a Critical Component in any High-Risk Device except to the limited extent that (i) Cypress's published data sheet for the product explicitly states Cypress has qualified the product for use in a specific High-Risk Device, or (ii) Cypress has given you advance written authorization to use the product as a Critical Component in the specific High-Risk Device and you have signed a separate indemnification agreement.
<br>
Cypress, the Cypress logo, and combinations thereof, ModusToolbox, PSoC, CAPSENSE, EZ-USB, F-RAM, and TRAVEO are trademarks or registered trademarks of Cypress or a subsidiary of Cypress in the United States or in other countries. For a more complete list of Cypress trademarks, visit www.infineon.com. Other names and brands may be claimed as property of their respective owners.
