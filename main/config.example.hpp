#pragma once

#include <hal/adc_types.h>
#include <soc/gpio_num.h>

#include <array>
#include <cstdint>

#include "messaging/messaging_transport.hpp"
#include "network/wifi_mode.hpp"

// The template for "config.hpp", which holds the device's own credentials

namespace config {

// Names this device in telemetry. Up to 31 characters, without quote,
// backslash or control character.
inline constexpr char DEVICE_NAME[]{"your-device-name"};

inline constexpr bool WIFI_ENABLED{true};

// Which WiFi role runs. Settings of the other roles are unused.
inline constexpr WifiMode WIFI_MODE{WifiMode::Sta};

inline constexpr char WIFI_STA_SSID[]{"your-wifi-ssid"};
inline constexpr char WIFI_STA_PASSWORD[]{"your-wifi-password"};
// The name the router lists this device under. 1 to 32 letters, digits and
// hyphens, not starting or ending with a hyphen.
inline constexpr char WIFI_STA_HOSTNAME[]{"your-hostname"};

// The network the device serves itself, reachable at 192.168.4.1. The password
// is 8 to 63 characters.
inline constexpr char WIFI_AP_SSID[]{"your-device-ap"};
inline constexpr char WIFI_AP_PASSWORD[]{"your-ap-password"};
// Held only while the station is unassociated. Once it joins a network, the AP
// follows that network's channel. 0 lets the driver choose, otherwise 1 to 11.
// An ESP32-C5 build also accepts 36 to 48 and 149 to 177 in steps of 4.
inline constexpr std::uint8_t WIFI_AP_CHANNEL{1};
// Stations that may join at once, at least 1
inline constexpr std::uint8_t WIFI_AP_MAX_CONNECTIONS{4};

// Which messaging transports run. Each carries telemetry and commands, and
// settings of unlisted ones are unused. An empty list runs without messaging:
//   inline constexpr std::array<MessagingTransport, 0> MESSAGING{};
inline constexpr std::array<MessagingTransport, 2> MESSAGING{
    MessagingTransport::Mqtt, MessagingTransport::Http
};

// Time between telemetry frames, above 0 s
inline constexpr float TELEMETRY_INTERVAL_S{1.0f};

// Where the device serves its API when Http is listed above
inline constexpr std::uint16_t HTTP_PORT{80};

// Serve the dashboard. It is a client of the HTTP API and needs Http
// listed above. False leaves the dashboard out of the build.
inline constexpr bool DASHBOARD_ENABLED{true};

// What the login page accepts. The user is 5 to 31 characters, the password 8
// to 31.
inline constexpr char DASHBOARD_USER[]{"your-dashboard-user"};
inline constexpr char DASHBOARD_PASSWORD[]{"your-dashboard-password"};

// What "Authorization: Bearer <token>" must carry to reach /api. 32 to 63
// characters of letters, digits and -._~+/, with = only as padding at the end.
inline constexpr char HTTP_API_TOKEN[]{"your-api-token-of-32-characters-or-more"};

// "mqtt" connects in the clear, "mqtts" over TLS. MQTT_BROKER is the host alone,
// without a scheme, and up to 253 characters.
inline constexpr char MQTT_PROTOCOL[]{"mqtt"};
inline constexpr char MQTT_BROKER[]{"192.168.1.x"};
inline constexpr std::uint16_t MQTT_PORT{1883};
// Unique to this device, 1 to 23 letters and digits
inline constexpr char MQTT_CLIENT_ID[]{"YourClientId"};
inline constexpr char MQTT_USER[]{""};
inline constexpr char MQTT_PASSWORD[]{""};
// Used by mqtts only, as PEM text. The broker's certificate must chain to
// MQTT_CA_CERT, or to a public CA built into ESP-IDF when it is empty.
inline constexpr char MQTT_CA_CERT[]{""};
// Used by mqtts only, as PEM text. The device's own certificate and unencrypted
// key, for a broker that asks for one. Both or neither.
inline constexpr char MQTT_CLIENT_CERT[]{""};
inline constexpr char MQTT_CLIENT_KEY[]{""};
// Up to 63 characters each, none empty, starting with $, or holding +, # or a
// control character. The status topic reads online or offline.
inline constexpr char MQTT_TOPIC_TELEMETRY[]{"your-telemetry-topic"};
inline constexpr char MQTT_TOPIC_COMMAND[]{"your-command-topic"};
inline constexpr char MQTT_TOPIC_STATUS[]{"your-status-topic"};

// Target and deadband take one decimal at most, and target +/- deadband must stay within 0..100%.
inline constexpr float SENSOR_TARGET{50.0f};   // Moisture target, %
inline constexpr float SENSOR_DEADBAND{1.0f};  // Hysteresis either side of the target, %

// ADC2 is shared with WiFi, which has priority. Reads on it time out while WiFi runs.
inline constexpr adc_unit_t SENSOR_ADC_UNIT{ADC_UNIT_1};
// Which GPIO a channel sits on is chip-specific, and the chip's datasheet lists it
inline constexpr adc_channel_t SENSOR_ADC_CHANNEL{ADC_CHANNEL_1};
// Raw ADC readings of this sensor when wet (min) and dry (max), mapped to 100..0%.
// Min below max, and max at most 4095 (8191 on the ESP32-S2). A reading above
// max is taken for a disconnected sensor, which faults and stops automatic watering.
inline constexpr std::uint16_t SENSOR_ADC_MIN{0};
inline constexpr std::uint16_t SENSOR_ADC_MAX{2900};
// Weight of each new reading in the smoothed one, 0..1. Lower is steadier.
inline constexpr float SENSOR_EMA_ALPHA{0.2f};

inline constexpr gpio_num_t PUMP_PIN{GPIO_NUM_22};
inline constexpr std::int64_t PUMP_DURATION_S{1};      // Run time per watering, 1..300 s
inline constexpr std::int64_t PUMP_WAIT_TIME_S{3600};  // Wait between waterings, 0..86400 s
// Water whenever the soil calls for it. False starts no automatic run until a
// command turns it on.
inline constexpr bool PUMP_AUTOMATIC{true};
// No automatic run may start until this long after boot, at most the longest wait.
// A device stuck in a reset loop then waters at most this often, not once per boot.
inline constexpr std::int64_t PUMP_BOOT_LOCKOUT_S{60};

// Addressable RGB LED (WS2812), onboard or wired to a free pin
inline constexpr bool LED_ENABLED{true};
inline constexpr gpio_num_t LED_PIN{GPIO_NUM_8};

// Version stamp for the settings persisted on the device. A device whose stored
// stamp differs drops its stored settings and re-seeds from the boot defaults.
inline constexpr std::uint32_t SETTINGS_VERSION{1};

}  // namespace config
