import esphome.codegen as cg

def load_ir_remote():
    # Pin the same IRremoteESP8266 fork/commit fujitsu_264/climate.py loads
    # directly: the registry release (auto-resolved by cg.add_library(name=...,
    # version=None)) is missing headers under the esp-idf framework (e.g.
    # ir_Mitsubishi.h fails to be found), whereas this fork/commit -- with the
    # fix from https://github.com/BorisKofman/IRremoteESP8266/tree/Espressif-version-3,
    # from https://github.com/crankyoldgit/IRremoteESP8266/pull/2030 -- builds
    # under both arduino and esp-idf.
    # TODO: Remove this pin once that PR is merged and the registry release is updated.
    cg.add_library(
        name="IRremoteESP8266",
        repository="https://github.com/hldh214/IRremoteESP8266.git#564c20fa2e345420598bd0286d7070036551928b",
        version="564c20fa2e345420598bd0286d7070036551928b",
    )
    cg.add_define("_IR_ENABLE_DEFAULT_", False)
