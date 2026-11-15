# 3.20 is where CXX_STANDARD learned the value 23
cmake_minimum_required(VERSION 3.20)
project(esp32_plant_watering_host_tests C CXX)

# Matches the gnu++23 ESP-IDF compiles the firmware with, giving main/ one
# language in both builds
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
add_compile_options(-Wall -Wextra -Wpedantic)

# Stops a test at the first out-of-bounds access, use after free or undefined
# arithmetic, which a passing assertion can hide. Set before any target, which
# instruments the vendored C libraries too. float-cast-overflow is named because GCC
# leaves it out of undefined.
option(SANITIZE "Build the tests with AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
# Builds the fuzz targets, which need Clang's libFuzzer, and sanitizes as above,
# which turns a bad input into a finding.
option(FUZZ "Build the libFuzzer targets (Clang only)" OFF)
if(SANITIZE OR FUZZ)
  add_compile_options(
    -fsanitize=address,undefined,float-cast-overflow
    -fno-sanitize-recover=all
    -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined,float-cast-overflow)
endif()
# Coverage goes into every target, which steers a fuzzer through the firmware code
# and cJSON alike.
if(FUZZ)
  add_compile_options(-fsanitize=fuzzer-no-link)
endif()

enable_testing()

# A std::atomic wider than a machine word lowers to libatomic calls, which GNU
# toolchains keep in a separate library.
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("
#include <atomic>
struct Wide {
    double a;
    double b;
};
int main() {
    std::atomic<Wide> wide;
    wide.store({});
    return static_cast<int>(wide.load().a);
}" ATOMIC_IS_BUILTIN)
if(NOT ATOMIC_IS_BUILTIN)
  set(ATOMIC_LIBRARY atomic)
endif()

# Pure, hardware-independent firmware logic lives in main/ and is exercised here
# on the host - no ESP-IDF or device required.
set(FIRMWARE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../main)

# doctest is vendored as a single header. doctest_main.cpp compiles its
# implementation and main() once for every test to link.
add_library(doctest_main STATIC doctest_main.cpp)
target_include_directories(doctest_main PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)

include(${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest.cmake OPTIONAL RESULT_VARIABLE DOCTEST_CMAKE)

# cJSON is vendored like doctest. The codec parses commands with it.
add_library(cjson STATIC third_party/cJSON.c)
target_include_directories(cjson PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party)
# The device's nesting cap from main/CMakeLists.txt, giving the tests the same
# depth limit
target_compile_definitions(cjson PRIVATE CJSON_NESTING_LIMIT=3)

# A build that turns warnings into errors leaves the vendored code out, since a
# compiler upgrade or a platform's headers can raise new warnings in it.
set_target_properties(doctest_main cjson PROPERTIES COMPILE_WARNING_AS_ERROR OFF)

# Register a test suite built from one or more .cpp files. Each case is exposed
# to CTest individually when doctest's CMake helper is available, otherwise the
# whole binary is registered as a single test.
function(add_host_test name)
  add_executable(${name} ${ARGN})
  # stubs/ holds only esp_log.h, which every layer includes, and must precede
  # FIRMWARE_DIR. The stubs in stubs/hardware/, stubs/storage/, stubs/system/ and
  # stubs/network/ are added per test, which makes an accidental ESP-IDF include
  # fail loudly anywhere else.
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${FIRMWARE_DIR})
  target_link_libraries(${name} PRIVATE doctest_main ${ATOMIC_LIBRARY})
  # Compiled into each test rather than doctest_main, because a linker skips an
  # archive member nothing references.
  if(SANITIZE OR FUZZ)
    target_sources(${name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lsan_suppressions.cpp)
  endif()
  if(DOCTEST_CMAKE)
    doctest_discover_tests(${name})
  else()
    add_test(NAME ${name} COMMAND ${name})
  endif()
endfunction()

add_host_test(test_periodic_timer test_periodic_timer.cpp)
add_host_test(test_gpio_output test_gpio_output.cpp ${FIRMWARE_DIR}/hardware/gpio_output.cpp)
target_include_directories(test_gpio_output PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_pump test_pump.cpp
  ${FIRMWARE_DIR}/hardware/pump.cpp
  ${FIRMWARE_DIR}/hardware/gpio_output.cpp)
target_include_directories(test_pump PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_sensor test_sensor.cpp
  ${FIRMWARE_DIR}/hardware/sensor.cpp
  ${FIRMWARE_DIR}/hardware/adc_reader.cpp
  ${FIRMWARE_DIR}/shared/math_utils.cpp)
target_include_directories(test_sensor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_sensor_sampler test_sensor_sampler.cpp
  ${FIRMWARE_DIR}/sensor_sampler.cpp
  ${FIRMWARE_DIR}/hardware/sensor.cpp
  ${FIRMWARE_DIR}/hardware/adc_reader.cpp
  ${FIRMWARE_DIR}/shared/math_utils.cpp)
target_include_directories(test_sensor_sampler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
# settings_rules.hpp brings in the chip capabilities, which every test reaching
# it takes from stubs/hardware.
add_host_test(test_runtime_settings test_runtime_settings.cpp)
target_include_directories(test_runtime_settings PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_settings_rules test_settings_rules.cpp)
target_include_directories(test_settings_rules PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_runtime_settings_store test_runtime_settings_store.cpp
  ${FIRMWARE_DIR}/storage/runtime_settings_store.cpp
  ${FIRMWARE_DIR}/storage/nvs_manager.cpp)
target_include_directories(test_runtime_settings_store PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs/storage
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_irrigation_controller test_irrigation_controller.cpp
  ${FIRMWARE_DIR}/irrigation_controller.cpp
  ${FIRMWARE_DIR}/irrigation_policy.cpp
  ${FIRMWARE_DIR}/hardware/pump.cpp
  ${FIRMWARE_DIR}/hardware/gpio_output.cpp)
target_include_directories(test_irrigation_controller PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_irrigation_policy test_irrigation_policy.cpp ${FIRMWARE_DIR}/irrigation_policy.cpp)
target_include_directories(test_irrigation_policy PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_math_utils test_math_utils.cpp ${FIRMWARE_DIR}/shared/math_utils.cpp)
add_host_test(test_time_utils test_time_utils.cpp)
add_host_test(test_json_utils test_json_utils.cpp)
add_host_test(test_pem_utils test_pem_utils.cpp)
add_host_test(test_string_utils test_string_utils.cpp)
add_host_test(test_color test_color.cpp)
add_host_test(test_http_utils test_http_utils.cpp)
add_host_test(test_mqtt_utils test_mqtt_utils.cpp)
add_host_test(test_net_utils test_net_utils.cpp)
add_host_test(test_hostname_utils test_hostname_utils.cpp)
add_host_test(test_json_codec test_json_codec.cpp ${FIRMWARE_DIR}/messaging/json_codec.cpp)
target_link_libraries(test_json_codec PRIVATE cjson)
add_host_test(test_command_handler test_command_handler.cpp ${FIRMWARE_DIR}/messaging/command_handler.cpp)
target_include_directories(test_command_handler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_transport_group test_transport_group.cpp)
add_host_test(test_next_command test_next_command.cpp)
add_host_test(test_json_format test_json_format.cpp ${FIRMWARE_DIR}/messaging/json_format.cpp ${FIRMWARE_DIR}/messaging/json_codec.cpp)
target_link_libraries(test_json_format PRIVATE cjson)
add_host_test(test_telemetry_assembler test_telemetry_assembler.cpp)
target_include_directories(test_telemetry_assembler PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_telemetry_frame test_telemetry_frame.cpp ${FIRMWARE_DIR}/messaging/json_codec.cpp)
target_link_libraries(test_telemetry_frame PRIVATE cjson)
target_include_directories(test_telemetry_frame PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
# The endpoints reach the server only through WebHost, which a fake stands in
# for. Their FreeRTOS queues come from stubs/hardware.
add_host_test(test_http_command_endpoint test_http_command_endpoint.cpp
  ${FIRMWARE_DIR}/messaging/http/http_command_endpoint.cpp
  ${FIRMWARE_DIR}/web/cors.cpp)
target_include_directories(test_http_command_endpoint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_http_telemetry_endpoint test_http_telemetry_endpoint.cpp
  ${FIRMWARE_DIR}/messaging/http/http_telemetry_endpoint.cpp
  ${FIRMWARE_DIR}/web/cors.cpp)
target_include_directories(test_http_telemetry_endpoint PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_host_test(test_http_transport test_http_transport.cpp
  ${FIRMWARE_DIR}/messaging/http/http_transport.cpp
  ${FIRMWARE_DIR}/messaging/http/http_command_endpoint.cpp
  ${FIRMWARE_DIR}/messaging/http/http_telemetry_endpoint.cpp
  ${FIRMWARE_DIR}/web/cors.cpp)
target_include_directories(test_http_transport PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
# The inbox runs on the real client, which a stubbed esp-mqtt in stubs/network
# hands its events to.
add_host_test(test_mqtt_inbox test_mqtt_inbox.cpp
  ${FIRMWARE_DIR}/messaging/mqtt/mqtt_inbox.cpp
  ${FIRMWARE_DIR}/messaging/mqtt/mqtt_client.cpp
  ${FIRMWARE_DIR}/network/ip_stack.cpp)
target_include_directories(test_mqtt_inbox PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs/network)
# Every component is the real one but the indicator, messaging and the web
# host, which fakes stand in for. No network is built in.
add_host_test(test_application test_application.cpp
  ${FIRMWARE_DIR}/application.cpp
  ${FIRMWARE_DIR}/watchdog.cpp
  ${FIRMWARE_DIR}/sensor_sampler.cpp
  ${FIRMWARE_DIR}/irrigation_controller.cpp
  ${FIRMWARE_DIR}/irrigation_policy.cpp
  ${FIRMWARE_DIR}/hardware/sensor.cpp
  ${FIRMWARE_DIR}/hardware/adc_reader.cpp
  ${FIRMWARE_DIR}/hardware/pump.cpp
  ${FIRMWARE_DIR}/hardware/gpio_output.cpp
  ${FIRMWARE_DIR}/messaging/command_handler.cpp
  ${FIRMWARE_DIR}/storage/runtime_settings_store.cpp
  ${FIRMWARE_DIR}/storage/nvs_manager.cpp
  ${FIRMWARE_DIR}/shared/math_utils.cpp)
target_include_directories(test_application PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs/storage
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs/system)
add_host_test(test_fault_status_source test_fault_status_source.cpp)
add_host_test(test_status_chain test_status_chain.cpp)
add_host_test(test_led_indicator test_led_indicator.cpp)
add_host_test(test_fault_store test_fault_store.cpp)
add_host_test(test_fault_manager test_fault_manager.cpp)
add_host_test(test_password_auth test_password_auth.cpp ${FIRMWARE_DIR}/web/password_auth.cpp)
add_host_test(test_bearer_auth test_bearer_auth.cpp ${FIRMWARE_DIR}/web/bearer_auth.cpp)
add_host_test(test_cookie test_cookie.cpp ${FIRMWARE_DIR}/web/cookie.cpp)
# Session ids are minted from the hardware RNG, which stubs/system/ stands in for
add_host_test(test_session_store test_session_store.cpp ${FIRMWARE_DIR}/web/session_store.cpp)
target_include_directories(test_session_store PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/system)
add_host_test(test_access_gate test_access_gate.cpp
  ${FIRMWARE_DIR}/web/access_gate.cpp
  ${FIRMWARE_DIR}/web/password_auth.cpp
  ${FIRMWARE_DIR}/web/bearer_auth.cpp
  ${FIRMWARE_DIR}/web/session_store.cpp
  ${FIRMWARE_DIR}/web/session_cookie.cpp
  ${FIRMWARE_DIR}/web/cookie.cpp
  ${FIRMWARE_DIR}/web/cors.cpp)
target_include_directories(test_access_gate PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/system)
add_host_test(test_dashboard test_dashboard.cpp ${FIRMWARE_DIR}/web/pages/dashboard.cpp)
# The test stands in for both the WebHost and the page, which is embedded on
# the device.
add_host_test(test_login test_login.cpp
  ${FIRMWARE_DIR}/web/pages/login.cpp
  ${FIRMWARE_DIR}/web/password_auth.cpp
  ${FIRMWARE_DIR}/web/session_store.cpp
  ${FIRMWARE_DIR}/web/session_cookie.cpp
  ${FIRMWARE_DIR}/web/cookie.cpp)
target_include_directories(test_login PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/system)
target_link_libraries(test_login PRIVATE cjson)

# Each target feeds libFuzzer's inputs to one inbound parser and checks what must
# hold for any input. libFuzzer supplies main(). Without FUZZ the target's own
# file still compiles, unlinked, which keeps it checked by every build and known
# to the editor.
function(add_fuzz_target name source)
  if(FUZZ)
    add_executable(${name} ${source} ${ARGN})
    target_link_options(${name} PRIVATE -fsanitize=fuzzer)
  else()
    add_library(${name} OBJECT ${source})
  endif()
  target_include_directories(${name} PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/stubs
    ${CMAKE_CURRENT_SOURCE_DIR}/third_party
    ${FIRMWARE_DIR})
  target_link_libraries(${name} PRIVATE ${ATOMIC_LIBRARY})
endfunction()

add_fuzz_target(fuzz_command fuzz_command.cpp
  ${FIRMWARE_DIR}/messaging/json_codec.cpp
  ${FIRMWARE_DIR}/messaging/command_handler.cpp)
target_link_libraries(fuzz_command PRIVATE cjson)
target_include_directories(fuzz_command PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/hardware)
add_fuzz_target(fuzz_login fuzz_login.cpp
  ${FIRMWARE_DIR}/web/pages/login.cpp
  ${FIRMWARE_DIR}/web/password_auth.cpp
  ${FIRMWARE_DIR}/web/session_store.cpp
  ${FIRMWARE_DIR}/web/session_cookie.cpp
  ${FIRMWARE_DIR}/web/cookie.cpp)
target_include_directories(fuzz_login PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs/system)
target_link_libraries(fuzz_login PRIVATE cjson)
add_fuzz_target(fuzz_cookie fuzz_cookie.cpp ${FIRMWARE_DIR}/web/cookie.cpp)
add_fuzz_target(fuzz_bearer fuzz_bearer.cpp ${FIRMWARE_DIR}/web/bearer_auth.cpp)
add_fuzz_target(fuzz_media_type fuzz_media_type.cpp)
