# PicoCalc Speech  (by Thomas Dzubin)
#
# Reuses the drivers from ./picocalc-text-starter-main (lcd, southbridge),
# which is picocalc-text-starter by Blair Leduc, plus the Raspberry Pi Pico SDK.
# Build it the same way you build that starter: with the Raspberry Pi Pico VS
# Code extension, or from a terminal that has PICO_SDK_PATH set.

cmake_minimum_required(VERSION 3.13)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# == DO NOT EDIT THE FOLLOWING LINES for the Raspberry Pi Pico VS Code Extension to work ==
if(WIN32)
    set(USERHOME $ENV{USERPROFILE})
else()
    set(USERHOME $ENV{HOME})
endif()
set(sdkVersion 2.3.1)
set(toolchainVersion 14_2_Rel1)
set(picotoolVersion 2.3.1)
set(picoVscode ${USERHOME}/.pico-sdk/cmake/pico-vscode.cmake)
if (EXISTS ${picoVscode})
    include(${picoVscode})
endif()
# ====================================================================================
# set(PICO_BOARD pico CACHE STRING "Board type")

# Pull in the Pico SDK (must be before project())
include(pico_sdk_import.cmake)

project(picocalc-speech C CXX ASM)

pico_sdk_init()

# Location of the picocalc-text-starter drivers we build against
set(STARTER ${CMAKE_CURRENT_LIST_DIR}/picocalc-text-starter-main)

add_executable(picocalc-speech
    main.c
    picocalc_audio.c            # sound engine
    picocalc_sound_effects.c    # wind / explosion / gunshot presets
    picocalc_speech.c           # vowels, words, singing
    ${STARTER}/drivers/lcd.c
    ${STARTER}/drivers/southbridge.c
    ${STARTER}/drivers/font-8x10.c
    ${STARTER}/drivers/font-5x10.c
)

pico_set_program_name(picocalc-speech "picocalc-speech")
pico_set_program_version(picocalc-speech "0.1")

# No serial console needed; keep USB free so '~' -> BOOTSEL is clean
pico_enable_stdio_uart(picocalc-speech 0)
pico_enable_stdio_usb(picocalc-speech 0)

target_compile_options(picocalc-speech PRIVATE -Wall)

target_include_directories(picocalc-speech PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}
    ${STARTER}
    ${STARTER}/drivers
)

target_link_libraries(picocalc-speech
    pico_stdlib
    hardware_gpio
    hardware_i2c
    hardware_spi
    hardware_watchdog   # watchdog_reboot(): leaving for the UF2 Loader
    # picocalc_audio.c: PWM-as-DAC sound engine, optionally serviced on
    # core 1 (pico_multicore) with the sample rate derived from clk_sys
    pico_multicore
    hardware_pwm
    hardware_clocks
    hardware_irq
)

# The chip is part of every output file name (speech-RP2040.uf2 /
# speech-RP2350.uf2, and the matching .elf/.bin/.hex), so a downloaded file
# always says which board it was built for.
if(PICO_PLATFORM MATCHES "^rp2040")
    set(CHIP_NAME RP2040)
elseif(PICO_PLATFORM MATCHES "^rp2350")
    set(CHIP_NAME RP2350)
else()
    set(CHIP_NAME ${PICO_PLATFORM})
endif()
set_target_properties(picocalc-speech PROPERTIES OUTPUT_NAME speech-${CHIP_NAME})

pico_add_extra_outputs(picocalc-speech)

# Copy the finished .uf2 (same name) to the top-level project folder, so the
# RP2040 and RP2350 builds can sit side by side.
add_custom_command(TARGET picocalc-speech POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy
            $<TARGET_FILE_DIR:picocalc-speech>/$<TARGET_FILE_BASE_NAME:picocalc-speech>.uf2
            ${CMAKE_CURRENT_LIST_DIR}/
)

