cmake_minimum_required(VERSION 3.16)

# Written into the image's app descriptor and reported in telemetry
set(PROJECT_VER "0.1.0")

# Each build directory holds its own sdkconfig, letting chips and profiles build side by side.
set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The component manifest states the same range, but an existing dependency lock skips that check.
# Keep the two in sync with main/idf_component.yml.
set(idf_version_min 5.4.2)
set(idf_version_end 6.0)
set(idf_version "${IDF_VERSION_MAJOR}.${IDF_VERSION_MINOR}.${IDF_VERSION_PATCH}")
if(idf_version VERSION_LESS idf_version_min OR idf_version VERSION_GREATER_EQUAL idf_version_end)
    message(FATAL_ERROR "ESP-IDF ${idf_version} is not supported; "
        "this firmware needs >= ${idf_version_min} and < ${idf_version_end}")
endif()

# A lock records the target it was solved for, and each chip keeps its own.
idf_build_set_property(DEPENDENCIES_LOCK lockfiles/dependencies.lock.${IDF_TARGET})

project(esp32_plant_watering)
