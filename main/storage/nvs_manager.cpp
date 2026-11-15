#include "storage/nvs_manager.hpp"

#include <esp_log.h>
#include <nvs_flash.h>

#include <bit>
#include <expected>

namespace {

constexpr char TAG[]{"NVS_MANAGER"};

}  // namespace

NvsManager::~NvsManager() {
    close();
}

void NvsManager::close() {
    if (namespace_ == 0) {
        return;
    }
    nvs_close(namespace_);
    namespace_ = 0;
}

bool NvsManager::init_flash() {
    esp_err_t err{nvs_flash_init()};
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        const esp_err_t erase_err{nvs_flash_erase()};
        if (erase_err != ESP_OK) {
            ESP_LOGE(TAG, "nvs_flash_erase failed: %s", esp_err_to_name(erase_err));
            return false;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_flash_init failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool NvsManager::open(const char* ns) {
    // Closed first, because nvs_open writes `namespace_` only on success and a
    // failed open would otherwise keep the old, closed one.
    close();
    const esp_err_t err{nvs_open(ns, NVS_READWRITE, &namespace_)};
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "nvs_open failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool NvsManager::is_open() const {
    return namespace_ != 0;
}

// Returns the NVS error instead of logging it, since only the caller knows
// whether a missing key is expected.
std::expected<std::int64_t, esp_err_t> NvsManager::get_i64(const char* key) const {
    std::int64_t value{0};
    const esp_err_t err{nvs_get_i64(namespace_, key, &value)};
    if (err != ESP_OK) {
        return std::unexpected(err);
    }
    return value;
}

std::expected<std::uint32_t, esp_err_t> NvsManager::get_u32(const char* key) const {
    std::uint32_t value{0};
    const esp_err_t err{nvs_get_u32(namespace_, key, &value)};
    if (err != ESP_OK) {
        return std::unexpected(err);
    }
    return value;
}

std::expected<float, esp_err_t> NvsManager::get_float(const char* key) const {
    return get_u32(key).transform([](std::uint32_t bits) { return std::bit_cast<float>(bits); });
}

bool NvsManager::set_i64(const char* key, std::int64_t value) const {
    const esp_err_t err{nvs_set_i64(namespace_, key, value)};
    // A warning rather than an error, because a failed write loses one value and
    // the namespace stays usable.
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_i64 failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool NvsManager::set_u32(const char* key, std::uint32_t value) const {
    const esp_err_t err{nvs_set_u32(namespace_, key, value)};
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_set_u32 failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool NvsManager::set_float(const char* key, float value) const {
    return set_u32(key, std::bit_cast<std::uint32_t>(value));
}

bool NvsManager::erase_all() const {
    const esp_err_t err{nvs_erase_all(namespace_)};
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_erase_all failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool NvsManager::commit() const {
    const esp_err_t err{nvs_commit(namespace_)};
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "nvs_commit failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}
