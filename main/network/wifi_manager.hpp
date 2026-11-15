#pragma once

#include <esp_event.h>
#include <esp_netif.h>
#include <esp_wifi_types_generic.h>
#include <soc/soc_caps.h>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <type_traits>

#include "network/network_info.hpp"
#include "network/network_provider.hpp"
#include "network/wifi_info.hpp"
#include "network/wifi_mode.hpp"

class WifiManager : public NetworkProvider {
public:
    // Longest values the driver takes. An SSID may fill its field, which the
    // driver reads up to its end, while a password keeps its terminator inside
    // the field. Longer values are cut on copy.
    static constexpr std::size_t MAX_STA_SSID_LENGTH{
        std::extent_v<decltype(wifi_sta_config_t::ssid)>
    };
    static constexpr std::size_t MAX_STA_PASSWORD_LENGTH{
        std::extent_v<decltype(wifi_sta_config_t::password)> - 1
    };
    static constexpr std::size_t MAX_AP_SSID_LENGTH{
        std::extent_v<decltype(wifi_ap_config_t::ssid)>
    };
    static constexpr std::size_t MAX_AP_PASSWORD_LENGTH{
        std::extent_v<decltype(wifi_ap_config_t::password)> - 1
    };
    // The shortest password WPA2 accepts. The driver refuses a shorter one,
    // and the AP never starts.
    static constexpr std::size_t MIN_AP_PASSWORD_LENGTH{8};
    // esp_netif refuses a longer hostname
    static constexpr std::size_t MAX_STA_HOSTNAME_LENGTH{32};

    // 0 lets the driver choose. The default country code ends 2.4 GHz at 11.
    static constexpr bool is_valid_ap_channel(std::uint8_t channel) {
        constexpr std::uint8_t last_2g_channel{11};
        if (channel <= last_2g_channel) {
            return true;
        }
#ifdef SOC_WIFI_SUPPORT_5G
        // An AP may not hold the radar-shared channels between these bands
        constexpr int channel_step{4};
        const auto in_band{[channel](int first, int last) {
            return channel >= first && channel <= last && (channel - first) % channel_step == 0;
        }};
        return in_band(36, 48) || in_band(149, 177);
#else
        return false;
#endif
    }

    struct StaConfig {
        const char* ssid;
        const char* password;
        // Sent with every DHCP request
        const char* hostname;
    };

    struct ApConfig {
        const char* ssid;
        const char* password;
        // Held only while the station is unassociated. Once it joins a network,
        // the AP follows that network's channel.
        std::uint8_t channel;
        std::uint8_t max_connections;
    };

    struct Config {
        WifiMode mode;
        StaConfig sta;
        ApConfig ap;
    };

    explicit WifiManager(const Config& config);
    // Takes the link down. No handler runs after this.
    ~WifiManager() override;
    // A copy would free the netif and event handles twice.
    WifiManager(const WifiManager&) = delete;
    WifiManager& operator=(const WifiManager&) = delete;

    void init() override;
    NetworkInfo info() const override;

private:
    // Receives the driver's events and dispatches them by base. WIFI_EVENT
    // carries both station and AP events.
    static void event_handler(void* arg, esp_event_base_t base, std::int32_t id, void* data);
    // Starts an association attempt. Returns false when the driver refuses, and
    // no event follows a refusal.
    [[nodiscard]] static bool connect_sta();
    static wifi_mode_t driver_mode(WifiMode mode);

    void on_wifi_event(std::int32_t id, const void* data);
    void on_ip_event(std::int32_t id, const void* data);
    [[nodiscard]] bool init_netif();
    [[nodiscard]] bool init_driver();
    [[nodiscard]] bool register_events();
    [[nodiscard]] bool configure() const;
    [[nodiscard]] bool configure_sta() const;
    [[nodiscard]] bool configure_ap() const;
    [[nodiscard]] bool start() const;
    WifiInfo::Station station_info() const;
    WifiInfo::AccessPoint access_point_info() const;
    // The radio's channel, shared by both roles. Nullopt while it holds none.
    std::optional<std::uint8_t> current_channel(bool sta_joined) const;
    // Releases whatever bring-up reached and leaves the link down.
    void release();
    void unregister_events();

    Config config_{};
    // Only the interfaces the mode runs are created. The rest stay null.
    esp_netif_t* sta_netif_{nullptr};
    esp_netif_t* ap_netif_{nullptr};
    esp_event_handler_instance_t wifi_events_{nullptr};
    esp_event_handler_instance_t ip_events_{nullptr};
    bool driver_up_{false};
    bool link_up_{false};
    // Addresses gained on a down link, the first one included
    std::atomic<std::uint32_t> links_{0};
};
