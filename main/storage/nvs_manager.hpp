#pragma once

#include <esp_err.h>
#include <nvs.h>

#include <cstdint>
#include <expected>

// One NVS namespace, opened read-write. Getters return the NVS error on
// failure, separating a missing key (ESP_ERR_NVS_NOT_FOUND) from a failed read.
// Setters and erases return success as a bool. Writes reach flash on commit().
class NvsManager {
public:
    // Mounts the NVS partition, erasing and remounting it when it is full or
    // holds a newer format.
    [[nodiscard]] static bool init_flash();

    // Declaring the copy operations below costs the implicit default.
    NvsManager() = default;
    ~NvsManager();
    // A copy would close the namespace twice.
    NvsManager(const NvsManager&) = delete;
    NvsManager& operator=(const NvsManager&) = delete;

    // Opens `ns`, closing any namespace already held. A failed open leaves none
    // open.
    [[nodiscard]] bool open(const char* ns);
    bool is_open() const;

    std::expected<std::int64_t, esp_err_t> get_i64(const char* key) const;
    std::expected<std::uint32_t, esp_err_t> get_u32(const char* key) const;
    // Floats are stored as their u32 bit pattern.
    std::expected<float, esp_err_t> get_float(const char* key) const;

    [[nodiscard]] bool set_i64(const char* key, std::int64_t value) const;
    [[nodiscard]] bool set_u32(const char* key, std::uint32_t value) const;
    [[nodiscard]] bool set_float(const char* key, float value) const;

    [[nodiscard]] bool erase_all() const;
    [[nodiscard]] bool commit() const;

private:
    // Safe when nothing is open.
    void close();

    // 0 until open() succeeds. NVS never hands out that value.
    nvs_handle_t namespace_{0};
};
