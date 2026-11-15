#include "network/wifi_manager.hpp"

#include <esp_event.h>
#include <esp_log.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <esp_wifi.h>

#include <algorithm>
#include <cinttypes>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <tuple>

#include "fault/fault_definitions.hpp"
#include "fault/faults.hpp"
#include "network/wifi_info.hpp"
#include "shared/net_utils.hpp"

namespace {

constexpr char TAG[]{"WIFI_MANAGER"};

// Copies `text` into a zero-initialized driver field, cut to `max_length`, and
// returns the length copied.
std::size_t copy_to_field(
    std::span<std::uint8_t> field, std::string_view text, std::size_t max_length
) {
    const std::string_view kept{text.substr(0, std::min(max_length, field.size()))};
    std::ranges::copy(kept, field.begin());
    return kept.size();
}

// The interface's IPv4 address. Nullopt when the interface does not exist or has
// none yet.
std::optional<net_utils::Ipv4Address> ipv4_of(esp_netif_t* netif) {
    if (netif == nullptr) {
        return std::nullopt;
    }
    esp_netif_ip_info_t ip_info{};
    if (esp_netif_get_ip_info(netif, &ip_info) != ESP_OK || ip_info.ip.addr == 0) {
        return std::nullopt;
    }
    return net_utils::Ipv4Address{
        esp_ip4_addr1(&ip_info.ip),
        esp_ip4_addr2(&ip_info.ip),
        esp_ip4_addr3(&ip_info.ip),
        esp_ip4_addr4(&ip_info.ip),
    };
}

}  // namespace

WifiManager::WifiManager(const Config& config) : config_{config} {}

WifiManager::~WifiManager() {
    release();
}

void WifiManager::init() {
    // Each phase logs its own error. A failed phase leaves the radio down for good.
    const bool radio_up{
        init_netif() && init_driver() && register_events() && configure() && start()
    };
    if (!radio_up) {
        faults::raise(fault_definitions::WIFI_BRING_UP_FAILED);
        ESP_LOGE(TAG, "Init failed, staying offline");
        release();
    }
}

NetworkInfo WifiManager::info() const {
    WifiInfo wifi{};
    wifi.mode = mode_name(config_.mode);
    if (has_sta(config_.mode)) {
        wifi.station = station_info();
    }
    if (has_ap(config_.mode)) {
        wifi.access_point = access_point_info();
    }
    const bool sta_joined{wifi.station && wifi.station->bssid};
    wifi.channel = current_channel(sta_joined);
    return {.wifi = wifi};
}

WifiInfo::Station WifiManager::station_info() const {
    WifiInfo::Station station{};
    station.ipv4 = ipv4_of(sta_netif_);
    // A radio that never came up has no access point to hear.
    if (!driver_up_) {
        return station;
    }
    net_utils::MacAddress mac{};
    if (esp_wifi_get_mac(WIFI_IF_STA, mac.data()) == ESP_OK) {
        station.mac = mac;
    }
    // Filled by esp_wifi_sta_get_ap_info
    // NOLINTNEXTLINE(bugprone-invalid-enum-default-initialization)
    wifi_ap_record_t access_point{};
    // Refused while the station is not joined, which leaves both readings empty.
    if (esp_wifi_sta_get_ap_info(&access_point) == ESP_OK) {
        net_utils::MacAddress bssid{};
        std::ranges::copy(access_point.bssid, bssid.begin());
        station.bssid = bssid;
        station.rssi = access_point.rssi;
    }
    const std::uint32_t links{links_.load()};
    // The first link is no reconnect, however long it took.
    station.reconnects = links == 0 ? 0 : links - 1;
    return station;
}

WifiInfo::AccessPoint WifiManager::access_point_info() const {
    WifiInfo::AccessPoint access_point{};
    access_point.ipv4 = ipv4_of(ap_netif_);
    if (driver_up_) {
        wifi_sta_list_t clients{};
        if (esp_wifi_ap_get_sta_list(&clients) == ESP_OK) {
            access_point.clients = static_cast<std::uint8_t>(clients.num);
        }
    }
    return access_point;
}

std::optional<std::uint8_t> WifiManager::current_channel(bool sta_joined) const {
    // A station alone holds no channel until it joins, since it scans them all.
    // An access point holds one from the start.
    if (!driver_up_ || (!has_ap(config_.mode) && !sta_joined)) {
        return std::nullopt;
    }
    std::uint8_t primary{0};
    wifi_second_chan_t secondary{WIFI_SECOND_CHAN_NONE};
    if (esp_wifi_get_channel(&primary, &secondary) != ESP_OK) {
        return std::nullopt;
    }
    return primary;
}

void WifiManager::release() {
    // First, to stop handlers running while the rest comes down.
    unregister_events();
    if (driver_up_) {
        // Unchecked, because a driver left running makes the deinit below refuse,
        // and that result is checked.
        esp_wifi_stop();
        const esp_err_t deinit_err{esp_wifi_deinit()};
        if (deinit_err == ESP_OK) {
            driver_up_ = false;
        } else {
            ESP_LOGE(TAG, "esp_wifi_deinit failed: %s", esp_err_to_name(deinit_err));
        }
    }
    if (sta_netif_ != nullptr) {
        esp_netif_destroy_default_wifi(sta_netif_);
        sta_netif_ = nullptr;
    }
    if (ap_netif_ != nullptr) {
        esp_netif_destroy_default_wifi(ap_netif_);
        ap_netif_ = nullptr;
    }
    // The netif layer and the default event loop stay up, because both are
    // process-wide and shared with whatever else runs on them.
}

void WifiManager::unregister_events() {
    // Neither result needs checking. An unregister fails only for a handle its base
    // and id never registered, or a missing default loop, and neither applies.
    if (ip_events_ != nullptr) {
        esp_event_handler_instance_unregister(IP_EVENT, IP_EVENT_STA_GOT_IP, ip_events_);
        ip_events_ = nullptr;
    }
    if (wifi_events_ != nullptr) {
        esp_event_handler_instance_unregister(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_events_);
        wifi_events_ = nullptr;
    }
}

bool WifiManager::init_netif() {
    const esp_err_t netif_err{esp_netif_init()};
    if (netif_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_netif_init failed: %s", esp_err_to_name(netif_err));
        return false;
    }
    // The default loop is process-wide. ESP_ERR_INVALID_STATE means it already
    // exists, which is fine.
    const esp_err_t loop_err{esp_event_loop_create_default()};
    if (loop_err != ESP_OK && loop_err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "esp_event_loop_create_default failed: %s", esp_err_to_name(loop_err));
        return false;
    }
    // Both abort internally on failure, returning no error to check. The AP one
    // brings its own address and DHCP server, making the network it serves
    // reachable the moment it starts.
    if (has_sta(config_.mode)) {
        sta_netif_ = esp_netif_create_default_wifi_sta();
    }
    if (has_ap(config_.mode)) {
        ap_netif_ = esp_netif_create_default_wifi_ap();
    }
    // Before the station starts. A name set later goes out only from the next
    // connection on.
    if (has_sta(config_.mode)) {
        const esp_err_t hostname_err{esp_netif_set_hostname(sta_netif_, config_.sta.hostname)};
        if (hostname_err != ESP_OK) {
            ESP_LOGE(TAG, "esp_netif_set_hostname failed: %s", esp_err_to_name(hostname_err));
            return false;
        }
    }
    return true;
}

bool WifiManager::init_driver() {
    const wifi_init_config_t init_config = WIFI_INIT_CONFIG_DEFAULT();
    const esp_err_t err{esp_wifi_init(&init_config)};
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_init failed: %s", esp_err_to_name(err));
        return false;
    }
    driver_up_ = true;
    return true;
}

bool WifiManager::register_events() {
    const esp_err_t wifi_err{esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &WifiManager::event_handler, this, &wifi_events_
    )};
    if (wifi_err != ESP_OK) {
        ESP_LOGE(TAG, "WIFI_EVENT registration failed: %s", esp_err_to_name(wifi_err));
        return false;
    }
    const esp_err_t ip_err{esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &WifiManager::event_handler, this, &ip_events_
    )};
    if (ip_err != ESP_OK) {
        ESP_LOGE(TAG, "IP_EVENT registration failed: %s", esp_err_to_name(ip_err));
        return false;
    }
    return true;
}

wifi_mode_t WifiManager::driver_mode(WifiMode mode) {
    switch (mode) {
        case WifiMode::Sta:
            return WIFI_MODE_STA;
        case WifiMode::Ap:
            return WIFI_MODE_AP;
        case WifiMode::ApSta:
            break;
    }
    return WIFI_MODE_APSTA;
}

bool WifiManager::configure() const {
    const esp_err_t mode_err{esp_wifi_set_mode(driver_mode(config_.mode))};
    if (mode_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_mode failed: %s", esp_err_to_name(mode_err));
        return false;
    }
    if (has_sta(config_.mode) && !configure_sta()) {
        return false;
    }
    return !has_ap(config_.mode) || configure_ap();
}

bool WifiManager::configure_sta() const {
    wifi_config_t sta_config{};
    copy_to_field(sta_config.sta.ssid, config_.sta.ssid, MAX_STA_SSID_LENGTH);
    copy_to_field(sta_config.sta.password, config_.sta.password, MAX_STA_PASSWORD_LENGTH);
    // Several access points may serve one SSID. The driver's default scan stops
    // at the first it hears, and this one hears them all and joins the strongest.
    sta_config.sta.scan_method = WIFI_ALL_CHANNEL_SCAN;
    sta_config.sta.sort_method = WIFI_CONNECT_AP_BY_SIGNAL;
    const esp_err_t err{esp_wifi_set_config(WIFI_IF_STA, &sta_config)};
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config STA failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool WifiManager::configure_ap() const {
    wifi_config_t ap_config{};
    // An SSID filling the field has no terminator to stop at, and the driver
    // takes its length from here instead.
    ap_config.ap.ssid_len = static_cast<std::uint8_t>(
        copy_to_field(ap_config.ap.ssid, config_.ap.ssid, MAX_AP_SSID_LENGTH)
    );
    copy_to_field(ap_config.ap.password, config_.ap.password, MAX_AP_PASSWORD_LENGTH);
    ap_config.ap.channel = config_.ap.channel;
    ap_config.ap.max_connection = config_.ap.max_connections;
    // WPA3 clients join with SAE, and those without it still with WPA2. Hash to
    // element resists timing side channels, and hunting and pecking stays for
    // the clients that only know it.
    ap_config.ap.authmode = WIFI_AUTH_WPA2_WPA3_PSK;
    ap_config.ap.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

    const esp_err_t err{esp_wifi_set_config(WIFI_IF_AP, &ap_config)};
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_set_config AP failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}

bool WifiManager::start() const {
    const esp_err_t start_err{esp_wifi_start()};
    if (start_err != ESP_OK) {
        ESP_LOGE(TAG, "esp_wifi_start failed: %s", esp_err_to_name(start_err));
        return false;
    }
    // Power save applies only to a station. ESP-IDF defaults it to WIFI_PS_MIN_MODEM,
    // which parks the radio between beacons and delays incoming traffic.
    if (has_sta(config_.mode)) {
        const esp_err_t ps_err{esp_wifi_set_ps(WIFI_PS_NONE)};
        if (ps_err != ESP_OK) {
            ESP_LOGW(TAG, "esp_wifi_set_ps failed: %s", esp_err_to_name(ps_err));
        }
    }
    return true;
}

void WifiManager::event_handler(void* arg, esp_event_base_t base, std::int32_t id, void* data) {
    auto* self{static_cast<WifiManager*>(arg)};
    if (base == WIFI_EVENT) {
        self->on_wifi_event(id, data);
    } else if (base == IP_EVENT) {
        self->on_ip_event(id, data);
    }
}

void WifiManager::on_wifi_event(std::int32_t id, const void* data) {
    switch (static_cast<wifi_event_t>(id)) {
        case WIFI_EVENT_STA_START:
            // The driver is up and the first association starts here. A refused
            // attempt produces no event, and is raised here or nowhere.
            if (!connect_sta()) {
                faults::raise(fault_definitions::WIFI_LINK_DOWN);
            }
            break;

        case WIFI_EVENT_STA_DISCONNECTED: {
            // The reason separates a wrong credential from a missing AP from a
            // lost signal, which are otherwise indistinguishable.
            const auto* event{static_cast<const wifi_event_sta_disconnected_t*>(data)};
            ESP_LOGD(
                TAG, "Disconnected (reason %u), reconnecting", static_cast<unsigned>(event->reason)
            );
            // Whatever the attempt's outcome, the link stays down until an address
            // arrives.
            faults::raise(fault_definitions::WIFI_LINK_DOWN);
            link_up_ = false;
            // No explicit backoff. Each attempt ends in another disconnect event,
            // and the association timeout paces the retries. The result needs no
            // checking. A refusal ends the retries, since no event follows one,
            // but after a disconnect only an internal driver error refuses, and
            // the link fault raised above stays active to show it.
            std::ignore = connect_sta();
            break;
        }

        case WIFI_EVENT_AP_START:
            ESP_LOGI(TAG, "AP up");
            break;

        case WIFI_EVENT_AP_STACONNECTED: {
            const auto* event{static_cast<const wifi_event_ap_staconnected_t*>(data)};
            ESP_LOGD(TAG, "Station " MACSTR " joined", MAC2STR(event->mac));
            break;
        }

        case WIFI_EVENT_AP_STADISCONNECTED: {
            const auto* event{static_cast<const wifi_event_ap_stadisconnected_t*>(data)};
            ESP_LOGD(TAG, "Station " MACSTR " left", MAC2STR(event->mac));
            break;
        }

        default:
            ESP_LOGV(TAG, "Unhandled WiFi event: %" PRId32, id);
            break;
    }
}

void WifiManager::on_ip_event(std::int32_t id, const void* data) {
    switch (static_cast<ip_event_t>(id)) {
        case IP_EVENT_STA_GOT_IP: {
            // Only an address clears the link, since an association without one
            // reaches nothing.
            faults::clear(fault_definitions::WIFI_LINK_DOWN);
            // A renewal that changes the address arrives on a link already up.
            if (!link_up_) {
                link_up_ = true;
                links_.fetch_add(1);
            }
            const auto* event{static_cast<const ip_event_got_ip_t*>(data)};
            ESP_LOGI(TAG, "Connected, IP: " IPSTR, IP2STR(&event->ip_info.ip));
            break;
        }

        default:
            ESP_LOGV(TAG, "Unhandled IP event: %" PRId32, id);
            break;
    }
}

bool WifiManager::connect_sta() {
    const esp_err_t err{esp_wifi_connect()};
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
        return false;
    }
    return true;
}
