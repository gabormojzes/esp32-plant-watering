#include "core_dump.hpp"

#include <esp_core_dump.h>
#include <esp_err.h>

bool core_dump::stored() {
    // A blank partition and a damaged dump both fail the check.
    return esp_core_dump_image_check() == ESP_OK;
}
