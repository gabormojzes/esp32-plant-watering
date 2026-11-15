# Folder under web/pages/ the dashboard and its login page are served from
set(DASHBOARD_TEMPLATE "default")

# The WiFi manager, only on a chip with a radio of its own
if(CONFIG_SOC_WIFI_SUPPORTED)
    set(WIFI_SRCS "network/wifi_manager.cpp")
endif()

idf_component_register(
    SRCS
        "main.cpp"
        "composition_root.cpp"
        "application.cpp"
        "sensor_sampler.cpp"
        "irrigation_policy.cpp"
        "irrigation_controller.cpp"
        "storage/nvs_manager.cpp"
        "storage/runtime_settings_store.cpp"
        "messaging/command_handler.cpp"
        "messaging/json_codec.cpp"
        "messaging/json_format.cpp"
        "messaging/mqtt/mqtt_transport.cpp"
        "messaging/mqtt/mqtt_client.cpp"
        "messaging/mqtt/mqtt_publisher.cpp"
        "messaging/mqtt/mqtt_inbox.cpp"
        "messaging/http/http_transport.cpp"
        "messaging/http/http_telemetry_endpoint.cpp"
        "messaging/http/http_command_endpoint.cpp"
        "web/web_server.cpp"
        "web/access_gate.cpp"
        "web/bearer_auth.cpp"
        "web/password_auth.cpp"
        "web/cookie.cpp"
        "web/session_cookie.cpp"
        "web/session_store.cpp"
        "web/cors.cpp"
        "web/pages/dashboard.cpp"
        "web/pages/login.cpp"
        "hardware/adc_reader.cpp"
        "hardware/sensor.cpp"
        "hardware/pump.cpp"
        "hardware/gpio_output.cpp"
        "hardware/rgb_led.cpp"
        "indicator/led_indicator.cpp"
        ${WIFI_SRCS}
        "network/ip_stack.cpp"
        "shared/math_utils.cpp"
        "core_dump.cpp"
        "reset_reason.cpp"
        "system_info.cpp"
        "watchdog.cpp"
    INCLUDE_DIRS
        "."
    REQUIRES
        nvs_flash
        esp_wifi
        esp_event
        esp_netif
        esp_adc
        driver
        mqtt
        mbedtls
        esp_http_server
        json
        esp_timer
        esp_system
        esp_hw_support
        esp_app_format
        espcoredump
        esptool_py
        led_strip
)

# Every warning in this component fails the build. ESP-IDF keeps some categories as plain
# warnings, which a bare -Werror cannot override, and turns sign-compare off for all components.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Werror
    -Werror=extra
    -Werror=unused-function
    -Werror=unused-variable
    -Werror=unused-but-set-variable
    -Werror=deprecated-declarations
    -Wsign-compare
)

idf_build_get_property(project_ver PROJECT_VER)
target_compile_definitions(${COMPONENT_LIB} PRIVATE FIRMWARE_VERSION="${project_ver}")

# Every flash also writes the coredump partition blank. A dump belongs to the build that crashed,
# not to the one replacing it. `idf.py app-flash` writes the app alone and leaves the dump.
partition_table_get_partition_info(coredump_offset
    "--partition-type data --partition-subtype coredump" "offset")
partition_table_get_partition_info(coredump_size
    "--partition-type data --partition-subtype coredump" "size")
idf_build_get_property(build_dir BUILD_DIR)
idf_build_get_property(python PYTHON)
idf_component_get_property(partition_table_dir partition_table COMPONENT_DIR)
set(blank_coredump_file ${build_dir}/coredump_blank.bin)
add_custom_command(OUTPUT ${blank_coredump_file}
    COMMAND ${python} ${partition_table_dir}/gen_empty_partition.py
        ${coredump_size} ${blank_coredump_file})
add_custom_target(blank_coredump ALL DEPENDS ${blank_coredump_file})
add_dependencies(flash blank_coredump)
esptool_py_flash_target_image(flash coredump "${coredump_offset}" "${blank_coredump_file}")

# A zero mtime keeps the gzip header, and with it the build id, the same from one build to the next.
foreach(page dashboard login)
    set(page_html ${CMAKE_CURRENT_SOURCE_DIR}/web/pages/${DASHBOARD_TEMPLATE}/${page}.html)
    set(page_gz ${CMAKE_CURRENT_BINARY_DIR}/${page}.html.gz)
    add_custom_command(OUTPUT ${page_gz}
        COMMAND ${python} -c
            "import gzip, pathlib, sys; p = pathlib.Path; p(sys.argv[2]).write_bytes(gzip.compress(p(sys.argv[1]).read_bytes(), 9, mtime=0))"
            ${page_html} ${page_gz}
        DEPENDS ${page_html}
        VERBATIM)
    target_add_binary_data(${COMPONENT_LIB} ${page_gz} BINARY)
endforeach()

# cJSON recurses once per nesting level, and its default cap of 1000 would
# overflow the parsing task's stack. 3 covers a command group and an object
# under it. Deeper payloads are dropped.
idf_component_get_property(json_lib json COMPONENT_LIB)
target_compile_definitions(${json_lib} PRIVATE CJSON_NESTING_LIMIT=3)
