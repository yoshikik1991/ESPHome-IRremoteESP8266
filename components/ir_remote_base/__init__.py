import esphome.codegen as cg

# Pinned rather than version=None ("latest registry release"): mitsubishi was
# last confirmed working (real hardware, 2026-07-09/10) against registry
# 2.9.0. An unpinned resolve later picked up a newer release whose IRrecv.cpp
# calls Arduino-ESP32 timer APIs (timerAlarmEnable/timerBegin(freq,div,edge)/
# timerAttachInterrupt(timer,func,edge)) removed by a newer arduino-esp32
# core, breaking the cold build. fujitsu_264 never hit this because its own
# climate.py pins a specific IRremoteESP8266 fork commit instead of going
# through this shared, registry-based loader.
def load_ir_remote():
    cg.add_library(name="IRremoteESP8266", version="2.9.0")
    cg.add_define("_IR_ENABLE_DEFAULT_", False)
