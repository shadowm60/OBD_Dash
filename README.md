# OBD_Dash
esp32s3 based obd dash board with logging capabilities.

Based on Waveshare ESP32-S3-Touch-LCD-5
    > https://www.waveshare.com/esp32-s3-touch-lcd-5.htm  
    > https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-5

    
your-project/
├── main/ (or src/)
│   ├── main.c / main.cpp        — setup, task creation
│   ├── display/                 — LVGL init, screen driver, UI screens
│   ├── peripherals/             — CAN, RS485, I2C sensors, RTC, SD
│   ├── ui/                      — LVGL widgets/screens as separate files per screen
│   └── config.h                 — pin defs, board-specific constants
├── components/ (ESP-IDF only)   — vendored/managed components
└── platformio.ini or CMakeLists.txt
