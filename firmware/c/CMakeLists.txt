cmake_minimum_required(VERSION 3.13)
# initialize the SDK based on PICO_SDK_PATH
# note: this must happen before project()
include(pico_sdk_import.cmake)
project(picoemp C CXX ASM)
 
pico_sdk_init()

add_executable(picoemp)

pico_generate_pio_header(picoemp ${CMAKE_CURRENT_LIST_DIR}/trigger_basic.pio)


target_sources(picoemp PRIVATE
        main.c
        oled.c
        picoemp.c
        settings.c
        serial.c
        )

target_link_libraries(picoemp PRIVATE pico_stdlib hardware_flash hardware_i2c hardware_pwm hardware_pio pico_multicore)
pico_add_extra_outputs(picoemp)

# enable usb output, disable uart output
pico_enable_stdio_usb(picoemp 1)
pico_enable_stdio_uart(picoemp 0)

pico_define_boot_stage2(my_boot2 ${PICO_SDK_PATH}/src/rp2040/boot_stage2/boot2_generic_03h.S)
pico_set_boot_stage2(picoemp my_boot2)
