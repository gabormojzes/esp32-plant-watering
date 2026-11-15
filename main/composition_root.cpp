#include "composition_root.hpp"

#include <driver/gpio.h>
#include <esp_crt_bundle.h>
#include <sdkconfig.h>
#include <soc/soc_caps.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "application.hpp"
#include "config.hpp"
#include "core_dump.hpp"
#include "fault/fault_manager.hpp"
#include "fault/fault_sink.hpp"
#include "fault/fault_store.hpp"
#include "fault_status_source.hpp"
#include "hardware/adc_reader.hpp"
#include "hardware/gpio_output.hpp"
#include "hardware/pump.hpp"
#include "hardware/rgb_led.hpp"
#include "hardware/sensor.hpp"
#include "indicator/indicator.hpp"
#include "indicator/led_indicator.hpp"
#include "indicator/null_indicator.hpp"
#include "indicator/status_chain.hpp"
#include "indicator/status_source.hpp"
#include "irrigation_controller.hpp"
#include "irrigation_policy.hpp"
#include "messaging/command_handler.hpp"
#include "messaging/http/http_command_endpoint.hpp"
#include "messaging/http/http_transport.hpp"
#include "messaging/json_format.hpp"
#include "messaging/messaging.hpp"
#include "messaging/messaging_transport.hpp"
#include "messaging/mqtt/mqtt_client.hpp"
#include "messaging/mqtt/mqtt_transport.hpp"
#include "messaging/telemetry_assembler.hpp"
#include "messaging/transport_group.hpp"
#include "network/network_provider.hpp"
#include "network/null_network_provider.hpp"
#include "network/wifi_manager.hpp"
#include "network/wifi_mode.hpp"
#include "periodic_task.hpp"
#include "reset_reason.hpp"
#include "runtime_settings.hpp"
#include "sensor_calibration.hpp"
#include "sensor_sampler.hpp"
#include "settings_rules.hpp"
#include "shared/constants.hpp"
#include "shared/hostname_utils.hpp"
#include "shared/http_utils.hpp"
#include "shared/json_utils.hpp"
#include "shared/math_utils.hpp"
#include "shared/mqtt_utils.hpp"
#include "shared/pem_utils.hpp"
#include "storage/runtime_settings_store.hpp"
#include "system_info.hpp"
#include "web/access_gate.hpp"
#include "web/bearer_auth.hpp"
#include "web/null_web_host.hpp"
#include "web/pages/dashboard.hpp"
#include "web/pages/login.hpp"
#include "web/password_auth.hpp"
#include "web/session_cookie.hpp"
#include "web/session_store.hpp"
#include "web/web_host.hpp"
#include "web/web_server.hpp"

namespace {

constexpr std::int64_t TELEMETRY_INTERVAL_US{
    static_cast<std::int64_t>(config::TELEMETRY_INTERVAL_S * constants::US_PER_S)
};
constexpr std::int64_t PUMP_DURATION_US{config::PUMP_DURATION_S * constants::US_PER_S};
constexpr std::int64_t PUMP_WAIT_TIME_US{config::PUMP_WAIT_TIME_S * constants::US_PER_S};
constexpr std::int64_t PUMP_BOOT_LOCKOUT_US{config::PUMP_BOOT_LOCKOUT_S * constants::US_PER_S};

constexpr AdcReader::Config ADC_READER_CONFIG{
    .unit = config::SENSOR_ADC_UNIT,
    .channel = config::SENSOR_ADC_CHANNEL,
};

constexpr SensorCalibration SENSOR_CALIBRATION{
    .adc_min = config::SENSOR_ADC_MIN,
    .adc_max = config::SENSOR_ADC_MAX,
};

constexpr SensorSampler::Config SENSOR_SAMPLER_CONFIG{
    .sensor_calibration = SENSOR_CALIBRATION,
    .ema_alpha = config::SENSOR_EMA_ALPHA,
};

constexpr GpioOutput::Config GPIO_OUTPUT_CONFIG{
    .pin = config::PUMP_PIN,
};

[[maybe_unused]] constexpr RgbLed::Config RGB_LED_CONFIG{
    .pin = config::LED_PIN,
};

static_assert(GPIO_IS_VALID_OUTPUT_GPIO(config::PUMP_PIN), "PUMP_PIN cannot drive an output");
static_assert(
    !config::LED_ENABLED || GPIO_IS_VALID_OUTPUT_GPIO(config::LED_PIN),
    "LED_PIN cannot drive an output"
);

constexpr Application::Config APPLICATION_CONFIG{
    .telemetry_interval_us = TELEMETRY_INTERVAL_US,
};

// Telemetry writes the name as it stands, and drops every frame a name would malform.
static_assert(
    json_utils::is_safe_string(config::DEVICE_NAME),
    "DEVICE_NAME carries a quote, a backslash or a control character"
);
static_assert(
    std::string_view{config::DEVICE_NAME}.size() < TelemetryAssembler::MAX_DEVICE_NAME_SIZE,
    "DEVICE_NAME is too long"
);

// FIRMWARE_VERSION carries PROJECT_VER from the top-level CMakeLists.txt.
static_assert(
    std::string_view{FIRMWARE_VERSION}.size() < TelemetryAssembler::MAX_FIRMWARE_VERSION_SIZE,
    "PROJECT_VER is too long"
);

static_assert(
    CONFIG_APP_RETRIEVE_LEN_ELF_SHA < TelemetryAssembler::MAX_FIRMWARE_BUILD_SIZE,
    "CONFIG_APP_RETRIEVE_LEN_ELF_SHA is past the longest firmware build id"
);

[[maybe_unused]] constexpr WifiManager::Config WIFI_MANAGER_CONFIG{
    .mode = config::WIFI_MODE,
    .sta =
        {
            .ssid = config::WIFI_STA_SSID,
            .password = config::WIFI_STA_PASSWORD,
            .hostname = config::WIFI_STA_HOSTNAME,
        },
    .ap = {
        .ssid = config::WIFI_AP_SSID,
        .password = config::WIFI_AP_PASSWORD,
        .channel = config::WIFI_AP_CHANNEL,
        .max_connections = config::WIFI_AP_MAX_CONNECTIONS,
    },
};

// Above the application task, which keeps the 1 ms burst spacing.
constexpr PeriodicTask<SensorSampler>::Config SENSOR_TASK_CONFIG{
    .period_ms = 1 * constants::MS_PER_S,
    .name = "sensor",
    .stack = 4096,
    .priority = 7,
};

// Readings arrive one period plus one burst apart. Two periods cover that and
// the jitter.
constexpr std::int64_t MAX_READING_AGE_US{2 * constants::US_PER_MS * SENSOR_TASK_CONFIG.period_ms};

static_assert(
    MAX_READING_AGE_US < (CONFIG_ESP_TASK_WDT_TIMEOUT_S * constants::US_PER_S),
    "a reading ages out only after the watchdog has already reset the device"
);

constexpr IrrigationPolicy::Config IRRIGATION_POLICY_CONFIG{
    .max_reading_age_us = MAX_READING_AGE_US,
};

constexpr IrrigationController::Config IRRIGATION_CONTROLLER_CONFIG{
    .boot_lockout_us = PUMP_BOOT_LOCKOUT_US,
};

constexpr bool RUNS_MESSAGING{!config::MESSAGING.empty()};
constexpr bool RUNS_MQTT{contains(config::MESSAGING, MessagingTransport::Mqtt)};
constexpr bool RUNS_HTTP{contains(config::MESSAGING, MessagingTransport::Http)};

// One slot per transport built below, leaving no null in the group
constexpr std::size_t TRANSPORT_COUNT{(RUNS_MQTT ? 1 : 0) + (RUNS_HTTP ? 1 : 0)};

static_assert(
    TRANSPORT_COUNT == config::MESSAGING.size(),
    "a transport in MESSAGING is listed twice or has no branch in create_messaging"
);

static_assert(
    !RUNS_HTTP || HttpCommandEndpoint::is_command_media_type(JsonFormat::MEDIA_TYPE),
    "HTTP cannot take commands in the format it is given"
);

// Checked after conversion, since an interval under 1 µs would never send a
// frame.
static_assert(!RUNS_MESSAGING || TELEMETRY_INTERVAL_US > 0, "TELEMETRY_INTERVAL_S must be above 0");

[[maybe_unused]] constexpr WebServer::Config WEB_SERVER_CONFIG{
    .port = config::HTTP_PORT,
};

// Port 0 binds whichever port the stack picks, where no client would find it.
static_assert(!RUNS_HTTP || config::HTTP_PORT > 0, "HTTP_PORT must be above 0");

[[maybe_unused]] constexpr PasswordAuth::Config PASSWORD_AUTH_CONFIG{
    .user = config::DASHBOARD_USER,
    .password = config::DASHBOARD_PASSWORD,
};

[[maybe_unused]] constexpr BearerAuth::Config BEARER_AUTH_CONFIG{
    .api_token = config::HTTP_API_TOKEN,
};

// The dashboard and the API are never served open, and credentials shorter than these
// fall to guessing.
constexpr std::size_t MIN_DASHBOARD_USER_LENGTH{5};
constexpr std::size_t MIN_DASHBOARD_PASSWORD_LENGTH{8};
constexpr std::size_t MIN_HTTP_API_TOKEN_LENGTH{32};

static_assert(
    !RUNS_HTTP || http_utils::is_token68(config::HTTP_API_TOKEN),
    "HTTP_API_TOKEN is not token68, the form an Authorization header carries"
);

static_assert(
    !RUNS_HTTP || std::string_view{config::HTTP_API_TOKEN}.size() >= MIN_HTTP_API_TOKEN_LENGTH,
    "HTTP_API_TOKEN is too short"
);

static_assert(
    !RUNS_HTTP || std::string_view{config::HTTP_API_TOKEN}.size() < BearerAuth::MAX_API_TOKEN_SIZE,
    "HTTP_API_TOKEN is too long"
);

static_assert(
    !config::DASHBOARD_ENABLED ||
        (std::string_view{config::DASHBOARD_USER}.size() >= MIN_DASHBOARD_USER_LENGTH &&
         std::string_view{config::DASHBOARD_PASSWORD}.size() >= MIN_DASHBOARD_PASSWORD_LENGTH),
    "DASHBOARD_USER or DASHBOARD_PASSWORD is too short"
);

static_assert(
    !config::DASHBOARD_ENABLED ||
        (std::string_view{config::DASHBOARD_USER}.size() < PasswordAuth::MAX_USER_SIZE &&
         std::string_view{config::DASHBOARD_PASSWORD}.size() < PasswordAuth::MAX_PASSWORD_SIZE),
    "DASHBOARD_USER or DASHBOARD_PASSWORD is too long"
);

// A Cookie header past the buffer reads as no session at all, and sends a
// logged-in browser back to the login page.
static_assert(
    CONFIG_HTTPD_MAX_REQ_HDR_LEN <= session_cookie::BUFFER_SIZE,
    "CONFIG_HTTPD_MAX_REQ_HDR_LEN is past the longest Cookie header a session is read from"
);

// Built on first call, which keeps them out of the build when nothing serves.
[[maybe_unused]] const PasswordAuth& password_auth() {
    static const PasswordAuth auth{PASSWORD_AUTH_CONFIG};
    return auth;
}

// Session ids are truly random while the RF subsystem runs. WiFi, the only network
// provider built in, runs it whenever anything can reach the server.
[[maybe_unused]] SessionStore& session_store() {
    static SessionStore store{};
    return store;
}

[[maybe_unused]] constexpr char DASHBOARD_URI[]{"/"};
[[maybe_unused]] constexpr char LOGIN_URI[]{"/login"};
[[maybe_unused]] constexpr char LOGOUT_URI[]{"/logout"};

[[maybe_unused]] constexpr AccessGate::Config ACCESS_GATE_CONFIG{
    .login_uri = LOGIN_URI,
};

[[maybe_unused]] const AccessGate& access_gate() {
    static const BearerAuth bearer_auth{BEARER_AUTH_CONFIG};
    static const AccessGate gate{ACCESS_GATE_CONFIG, bearer_auth, session_store()};
    return gate;
}

[[maybe_unused]] WebServer& web_server() {
    static WebServer server{WEB_SERVER_CONFIG, access_gate()};
    return server;
}

[[maybe_unused]] constexpr Dashboard::Config DASHBOARD_CONFIG{
    .uri = DASHBOARD_URI,
};

[[maybe_unused]] constexpr Login::Config LOGIN_CONFIG{
    .uri = LOGIN_URI,
    .logout_uri = LOGOUT_URI,
    .dashboard_uri = DASHBOARD_URI,
};

static_assert(
    !config::DASHBOARD_ENABLED || RUNS_HTTP, "DASHBOARD_ENABLED requires Http in MESSAGING"
);

constexpr bool MQTT_OVER_TLS{std::string_view{config::MQTT_PROTOCOL} == "mqtts"};

// Websockets need setup this client does not do.
static_assert(
    !RUNS_MQTT || MQTT_OVER_TLS || std::string_view{config::MQTT_PROTOCOL} == "mqtt",
    "MQTT_PROTOCOL must be mqtt or mqtts"
);

// A path, or a block cut short, would build and fail every handshake.
static_assert(
    !RUNS_MQTT || !MQTT_OVER_TLS ||
        ((std::string_view{config::MQTT_CA_CERT}.empty() ||
          pem_utils::is_pem(config::MQTT_CA_CERT)) &&
         (std::string_view{config::MQTT_CLIENT_CERT}.empty() ||
          pem_utils::is_pem(config::MQTT_CLIENT_CERT)) &&
         (std::string_view{config::MQTT_CLIENT_KEY}.empty() ||
          pem_utils::is_pem(config::MQTT_CLIENT_KEY))),
    "MQTT_CA_CERT, MQTT_CLIENT_CERT and MQTT_CLIENT_KEY take whole PEM blocks, not a path"
);

// No key password reaches esp-tls, which leaves an encrypted key unreadable.
static_assert(
    !RUNS_MQTT || !MQTT_OVER_TLS || !pem_utils::is_encrypted_key(config::MQTT_CLIENT_KEY),
    "MQTT_CLIENT_KEY must be unencrypted"
);

// esp-tls refuses a client certificate without its key, and a key without its
// certificate.
static_assert(
    !RUNS_MQTT || !MQTT_OVER_TLS ||
        std::string_view{config::MQTT_CLIENT_CERT}.empty() ==
            std::string_view{config::MQTT_CLIENT_KEY}.empty(),
    "MQTT_CLIENT_CERT and MQTT_CLIENT_KEY are set together or not at all"
);

// The client prefixes the scheme itself, and a broker carrying its own would end
// up with two.
static_assert(
    !RUNS_MQTT || !std::string_view{config::MQTT_BROKER}.contains("://"),
    "MQTT_BROKER is a host, the scheme comes from MQTT_PROTOCOL"
);

constexpr std::size_t MQTT_URI_LENGTH{
    std::string_view{config::MQTT_PROTOCOL}.size() + std::string_view{"://"}.size() +
    std::string_view{config::MQTT_BROKER}.size() + std::string_view{":"}.size() +
    math_utils::decimal_digits(config::MQTT_PORT)
};
static_assert(
    !RUNS_MQTT || MQTT_URI_LENGTH < MqttClient::URI_BUFFER_SIZE,
    "MQTT_BROKER is too long for the broker URI"
);

// esp-mqtt reads port 0 as its scheme's default port.
static_assert(!RUNS_MQTT || config::MQTT_PORT > 0, "MQTT_PORT must be above 0");

static_assert(
    !RUNS_MQTT ||
        (std::string_view{config::MQTT_TOPIC_TELEMETRY}.size() < MqttClient::MAX_TOPIC_SIZE &&
         std::string_view{config::MQTT_TOPIC_COMMAND}.size() < MqttClient::MAX_TOPIC_SIZE &&
         std::string_view{config::MQTT_TOPIC_STATUS}.size() < MqttClient::MAX_TOPIC_SIZE),
    "MQTT_TOPIC_TELEMETRY, MQTT_TOPIC_COMMAND or MQTT_TOPIC_STATUS is too long"
);

// A broker closes the connection over a topic MQTT does not allow. A wildcard
// command topic would subscribe, but no command would match it.
static_assert(
    !RUNS_MQTT || (mqtt_utils::is_valid_topic_name(config::MQTT_TOPIC_TELEMETRY) &&
                   mqtt_utils::is_valid_topic_name(config::MQTT_TOPIC_COMMAND) &&
                   mqtt_utils::is_valid_topic_name(config::MQTT_TOPIC_STATUS)),
    "MQTT_TOPIC_TELEMETRY, MQTT_TOPIC_COMMAND or MQTT_TOPIC_STATUS is empty, starts with $, or "
    "holds a + or # wildcard, a control character or broken UTF-8"
);

// An empty id gets a new one on every connect, which lets a dropped connection's
// offline will land after the new online.
static_assert(
    !RUNS_MQTT || mqtt_utils::is_valid_client_id(config::MQTT_CLIENT_ID),
    "MQTT_CLIENT_ID must be 1 to 23 letters and digits"
);

// The password is binary data, which MQTT puts no character rule on.
static_assert(
    !RUNS_MQTT || mqtt_utils::is_valid_string(config::MQTT_USER),
    "MQTT_USER holds a control character or broken UTF-8"
);

// The CA bundle is referenced, and linked into the image, only when it is the
// one checked.
[[maybe_unused]] constexpr MqttClient::Tls MQTT_TLS{
    .ca_cert = config::MQTT_CA_CERT,
    .attach_ca_bundle =
        std::string_view{config::MQTT_CA_CERT}.empty() ? esp_crt_bundle_attach : nullptr,
    .client_cert = config::MQTT_CLIENT_CERT,
    .client_key = config::MQTT_CLIENT_KEY,
};

[[maybe_unused]] constexpr MqttTransport::Config MQTT_TRANSPORT_CONFIG{
    .client =
        {
            .protocol = config::MQTT_PROTOCOL,
            .broker = config::MQTT_BROKER,
            .port = config::MQTT_PORT,
            .client_id = config::MQTT_CLIENT_ID,
            .user = config::MQTT_USER,
            .password = config::MQTT_PASSWORD,
            .status_topic = config::MQTT_TOPIC_STATUS,
            .tls = MQTT_OVER_TLS ? &MQTT_TLS : nullptr,
        },
    .topics = {
        .telemetry = config::MQTT_TOPIC_TELEMETRY,
        .command = config::MQTT_TOPIC_COMMAND,
    },
};

// Boot defaults must pass the rules a command has to pass, or the device boots
// into a state no command could restate. A command is rounded to a tenth on the
// way in. A default is not, and must already be one.
static_assert(
    settings_rules::is_valid_percent(config::SENSOR_TARGET) &&
        settings_rules::is_valid_percent(config::SENSOR_DEADBAND) &&
        settings_rules::is_valid_sensor_band(config::SENSOR_TARGET, config::SENSOR_DEADBAND),
    "SENSOR_TARGET and SENSOR_DEADBAND must be tenths, with target +/- deadband within 0..100"
);
static_assert(
    settings_rules::is_valid_pump_duration(PUMP_DURATION_US),
    "PUMP_DURATION_S is outside what a command may set"
);
static_assert(
    settings_rules::is_valid_pump_wait_time(PUMP_WAIT_TIME_US),
    "PUMP_WAIT_TIME_S is outside what a command may set"
);

static_assert(config::SENSOR_ADC_UNIT < SOC_ADC_PERIPH_NUM, "SENSOR_ADC_UNIT is not on this chip");
static_assert(
    config::SENSOR_ADC_CHANNEL < SOC_ADC_CHANNEL_NUM(config::SENSOR_ADC_UNIT),
    "SENSOR_ADC_CHANNEL is not on SENSOR_ADC_UNIT"
);

// Endpoints that meet or cross map no reading at all, leaving the device
// faulted from its first sample. A SENSOR_ADC_MAX the ADC cannot reach would
// boot the device on a calibration its own setter refuses.
static_assert(
    settings_rules::is_valid_sensor_calibration(SENSOR_CALIBRATION),
    "SENSOR_ADC_MIN must be below SENSOR_ADC_MAX, and SENSOR_ADC_MAX at most the ADC's full scale"
);

// A weight the smoothing refuses leaves every burst unsmoothed, faulting the
// sampler from its first reading.
static_assert(
    config::SENSOR_EMA_ALPHA >= math_utils::Ema::MIN_ALPHA &&
        config::SENSOR_EMA_ALPHA <= math_utils::Ema::MAX_ALPHA,
    "SENSOR_EMA_ALPHA must be within 0..1"
);

// A negative lockout would leave the first automatic run ungated, and one past the
// longest wait would hold the pump back longer than any setting can.
static_assert(
    PUMP_BOOT_LOCKOUT_US >= 0 && PUMP_BOOT_LOCKOUT_US <= settings_rules::MAX_PUMP_WAIT_TIME_US,
    "PUMP_BOOT_LOCKOUT_S must not be negative or past the longest wait"
);

#ifndef SOC_WIFI_SUPPORTED
static_assert(!config::WIFI_ENABLED, "this chip has no WiFi radio");
#endif

constexpr bool RUNS_STA{config::WIFI_ENABLED && has_sta(config::WIFI_MODE)};
constexpr bool RUNS_AP{config::WIFI_ENABLED && has_ap(config::WIFI_MODE)};

static_assert(
    !RUNS_STA ||
        (!std::string_view{config::WIFI_STA_SSID}.empty() &&
         std::string_view{config::WIFI_STA_SSID}.size() <= WifiManager::MAX_STA_SSID_LENGTH),
    "WIFI_STA_SSID is empty or too long"
);

static_assert(
    !RUNS_STA ||
        std::string_view{config::WIFI_STA_PASSWORD}.size() <= WifiManager::MAX_STA_PASSWORD_LENGTH,
    "WIFI_STA_PASSWORD is too long"
);

static_assert(
    !RUNS_STA || (hostname_utils::is_valid_hostname(config::WIFI_STA_HOSTNAME) &&
                  std::string_view{config::WIFI_STA_HOSTNAME}.size() <=
                      WifiManager::MAX_STA_HOSTNAME_LENGTH),
    "WIFI_STA_HOSTNAME must be 1 to 32 letters, digits and hyphens, not starting or ending with a "
    "hyphen"
);

// An empty SSID names no network at all, and one past the field truncates to a
// name no client is looking for.
static_assert(
    !RUNS_AP || (!std::string_view{config::WIFI_AP_SSID}.empty() &&
                 std::string_view{config::WIFI_AP_SSID}.size() <= WifiManager::MAX_AP_SSID_LENGTH),
    "WIFI_AP_SSID is empty or too long"
);

static_assert(
    !RUNS_AP ||
        (std::string_view{config::WIFI_AP_PASSWORD}.size() >= WifiManager::MIN_AP_PASSWORD_LENGTH &&
         std::string_view{config::WIFI_AP_PASSWORD}.size() <= WifiManager::MAX_AP_PASSWORD_LENGTH),
    "WIFI_AP_PASSWORD is shorter than WPA2 accepts or too long"
);

static_assert(
    !RUNS_AP || WifiManager::is_valid_ap_channel(config::WIFI_AP_CHANNEL),
    "WIFI_AP_CHANNEL is not a channel the AP can hold on this chip"
);

// Zero admits no station at all, and the AP still starts and reports up.
static_assert(
    !RUNS_AP || config::WIFI_AP_MAX_CONNECTIONS >= 1, "WIFI_AP_MAX_CONNECTIONS must be at least 1"
);

constexpr RuntimeSettings::Config RUNTIME_SETTINGS_CONFIG{
    .sensor_target = config::SENSOR_TARGET,
    .sensor_deadband = config::SENSOR_DEADBAND,
    .sensor_calibration = SENSOR_CALIBRATION,
    .pump_duration_us = PUMP_DURATION_US,
    .pump_wait_time_us = PUMP_WAIT_TIME_US,
    .pump_automatic = config::PUMP_AUTOMATIC,
};

constexpr RuntimeSettingsStore::Config RUNTIME_SETTINGS_STORE_CONFIG{
    .version = config::SETTINGS_VERSION,
};

[[nodiscard]] Indicator& create_indicator(const FaultStore& fault_store) {
    if constexpr (config::LED_ENABLED) {
        static RgbLed led{RGB_LED_CONFIG};
        static const FaultStatusSource fault_status{fault_store};
        static const std::array<const StatusSource*, 1> sources{&fault_status};
        static const StatusChain chain{{.sources = sources}};
        static LedIndicator indicator{led, chain};
        return indicator;
    } else {
        static NullIndicator indicator{};
        return indicator;
    }
}

[[nodiscard]] NetworkProvider& create_network_provider() {
    if constexpr (config::WIFI_ENABLED) {
        static WifiManager provider{WIFI_MANAGER_CONFIG};
        return provider;
    } else {
        static NullNetworkProvider provider{};
        return provider;
    }
}

[[nodiscard]] Messaging& create_messaging() {
    static std::array<Messaging*, TRANSPORT_COUNT> transports{};

    if constexpr (RUNS_MESSAGING) {
        static const JsonFormat json{};
        std::size_t next{0};

        if constexpr (RUNS_MQTT) {
            static MqttTransport mqtt{MQTT_TRANSPORT_CONFIG, json};
            transports[next++] = &mqtt;
        }
        if constexpr (RUNS_HTTP) {
            static HttpTransport http{web_server(), json};
            transports[next++] = &http;
        }
    }

    static TransportGroup group{{.transports = transports}};
    return group;
}

[[nodiscard]] WebHost& create_web_host() {
    if constexpr (RUNS_HTTP) {
        return web_server();
    } else {
        static NullWebHost web_host{};
        return web_host;
    }
}

// Claiming here is early enough, because the host registers its route table
// when it starts.
void register_pages() {
    if constexpr (config::DASHBOARD_ENABLED) {
        static Dashboard dashboard{web_server(), DASHBOARD_CONFIG};
        dashboard.start();
        // The dashboard sits behind a session, and the login that opens one is claimed
        // with it. Without the login nothing could reach the dashboard.
        static Login login{web_server(), session_store(), password_auth(), LOGIN_CONFIG};
        login.start();
    }
}

}  // namespace

Application& create_application() {
    static RuntimeSettings runtime_settings{RUNTIME_SETTINGS_CONFIG};
    static RuntimeSettingsStore runtime_settings_store{
        runtime_settings, RUNTIME_SETTINGS_STORE_CONFIG
    };

    static AdcReader adc_reader{ADC_READER_CONFIG};
    static Sensor sensor{adc_reader};
    static SensorSampler sensor_sampler{sensor, SENSOR_SAMPLER_CONFIG};
    static GpioOutput gpio_output{GPIO_OUTPUT_CONFIG};
    static Pump pump{gpio_output};

    NetworkProvider& network_provider{create_network_provider()};
    Messaging& messaging{create_messaging()};
    WebHost& web_host{create_web_host()};
    register_pages();

    static PeriodicTask<SensorSampler> sensor_task{SENSOR_TASK_CONFIG, sensor_sampler};

    static const IrrigationPolicy irrigation_policy{runtime_settings, IRRIGATION_POLICY_CONFIG};
    static IrrigationController irrigation_controller{
        pump, irrigation_policy, runtime_settings, IRRIGATION_CONTROLLER_CONFIG
    };

    static CommandHandler command_handler{runtime_settings};
    static TelemetryAssembler telemetry_assembler{
        config::DEVICE_NAME,
        FIRMWARE_VERSION,
        system_info::firmware_build(),
        system_info::chip_model(),
        system_info::chip_revision(),
        reset_reason::name(),
        core_dump::stored()
    };

    static FaultStore fault_store{};
    static const std::array<FaultSink*, 1> fault_sinks{&fault_store};
    // Installed before init(), where components first raise.
    FaultManager::instance().install(fault_sinks);

    Indicator& indicator{create_indicator(fault_store)};

    static Application application{
        APPLICATION_CONFIG,
        {
            .sensor_sampler = sensor_sampler,
            .pump = pump,
            .indicator = indicator,
            .network_provider = network_provider,
            .messaging = messaging,
            .web_host = web_host,
            .sensor_task = sensor_task,
            .irrigation_controller = irrigation_controller,
            .command_handler = command_handler,
            .runtime_settings = runtime_settings,
            .runtime_settings_store = runtime_settings_store,
            .telemetry_assembler = telemetry_assembler,
            .fault_store = fault_store,
        },
    };
    return application;
}
