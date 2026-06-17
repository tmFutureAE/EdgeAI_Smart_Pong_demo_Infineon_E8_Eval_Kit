[Click here](../README.md) to view the README.

## Design and implementation

This project is now locked to one display profile:

   **[Waveshare 4.3-inch Raspberry Pi DSI LCD display](https://www.waveshare.com/4.3inch-DSI-LCD.htm):** The LCD houses a Chipone ICN6211 display controller and uses the MIPI DSI interface.

Build-time enforcement:
- `CONFIG_DISPLAY` must be `W4P3INCH_DISP`.
- Non-4.3 display configs are rejected by `proj_cm55/Makefile` with an explicit build error.
- Touch path is fixed to FT5406 on this profile.

The design of this application is minimalistic to get started with code examples on PSOC&trade; Edge MCU devices. All PSOC&trade; Edge E84 MCU applications have a dual-CPU three-project structure to develop code for the CM33 and CM55 cores. The CM33 core has two separate projects for the secure processing environment (SPE) and non-secure processing environment (NSPE). A project folder consists of various subfolders, each denoting a specific aspect of the project. The three project folders are as follows:

**Table 1. Application projects**

Project | Description
--------|------------------------
*proj_cm33_s* | Project for CM33 secure processing environment (SPE)
*proj_cm33_ns* | Project for CM33 non-secure processing environment (NSPE)
*proj_cm55* | CM55 project

<br>

In this code example, at device reset, the secure boot process starts from the ROM boot with the secure enclave (SE) as the root of trust (RoT). From the secure enclave, the boot flow is passed on to the system CPU subsystem where the secure CM33 application starts. After all necessary secure configurations, the flow is passed on to the non-secure CM33 application. Resource initialization for this example is performed by this CM33 non-secure project. It configures the system clocks, pins, clock to peripheral connections, and other platform resources. It then enables the CM55 core using the `Cy_SysEnableCM55()` function and the CM33 core is subsequently put to Deep Sleep mode.

The CM55 application drives the LCD and renders the image using the PSOC&trade; Edge graphics subsystem, which houses an independent 2.5D GPU, a display controller (DC), and a MIPI DSI host controller with a MIPI D-PHY physical layer interface.

**cm55_gfx_task** initializes the Graphics subsystem and configures the DC and GPU interrupts. After that it initializes the LCD panel based on selection through _<application\>/proj_cm55/Makefile_. Once the panel is initialized, the required amount of memory is allocated for VGLite draw/blit functions to be consumed by LVGL library. The `lv_init()` function is used to initialize LVGL and set up the essential components required for LVGL to work correctly. The display and touch drivers are initialized using `lv_port_disp_init()` and `lv_port_indev_init()` functions, respectively. The LVGL demo music player is displayed on the display by calling the LVGL demo widget API `lv_demo_music()`. In order to switch to other available LVGL demos, user need to enable the `LV_USE_DEMO_<demo_name>` macro in `lv_conf.h` file and call the corresponding `lv_demo_<demo_name>()` API in place of `lv_demo_music()`.

This application allows user to evaluate the performance of PSOC&trade; Edge's graphics subsystem using the built-in system monitor component of LVGL. In order to enable the system monitor component, user needs to perform the following steps:
   1. Set `LV_USE_SYSMON` in `lv_conf.h`
   2. Set `configGENERATE_RUN_TIME_STATS` in `FreeRTOSConfig.h`
   3. Build and program the application and observe the performance data (FPS, CPU usage) in bottom-right corner of the display
