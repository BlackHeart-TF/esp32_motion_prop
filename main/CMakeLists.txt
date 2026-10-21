idf_component_register(
    SRCS "main.c" "audio.c" "motor.c" "leds.c"
    INCLUDE_DIRS "."
    REQUIRES esp_driver_gpio esp_driver_ledc esp_driver_i2s esp_partition esp_hw_support esp_common log freertos
)

idf_build_get_property(python PYTHON)

set(SCREAMS_BIN "${CMAKE_BINARY_DIR}/screams.bin")
file(GLOB SCREAM_WAVS CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/screams/*.wav")

add_custom_command(
    OUTPUT "${SCREAMS_BIN}"
    COMMAND ${python} "${CMAKE_SOURCE_DIR}/tools/pack_screams.py" "${CMAKE_SOURCE_DIR}/screams" "${SCREAMS_BIN}"
    DEPENDS ${SCREAM_WAVS} "${CMAKE_SOURCE_DIR}/tools/pack_screams.py"
    VERBATIM
)
add_custom_target(screams_bin ALL DEPENDS "${SCREAMS_BIN}")
add_dependencies(${COMPONENT_LIB} screams_bin)
