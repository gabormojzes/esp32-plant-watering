#include <doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <optional>
#include <string_view>

#include "messaging/json_codec.hpp"
#include "shared/net_utils.hpp"

namespace {

std::string_view text_of(const Payload& payload) {
    return {payload.data.data(), static_cast<std::size_t>(payload.length)};
}

Payload make_payload(std::string_view json) {
    Payload payload{};
    payload.length = static_cast<int>(json.size());
    std::ranges::copy(json, payload.data.begin());
    return payload;
}

// Balanced, which leaves the depth as the only thing wrong with what it builds.
Payload nested(std::size_t levels) {
    Payload payload{};
    payload.length = static_cast<int>(2 * levels);
    auto* const closing{std::fill_n(payload.data.begin(), levels, '[')};
    std::fill_n(closing, levels, ']');
    return payload;
}

}  // namespace

TEST_CASE("encode_telemetry writes every field as it is given") {
    const Telemetry telemetry{
        .sequence = 7,
        .device =
            {.name = "test-device",
             .uptime_s = 90,
             .reset_reason = "panic",
             .core_dump = true,
             .firmware = {.version = "1.0.0", .build = "b6c3172ee"},
             .chip = {.model = "esp32c6", .revision = "v0.1", .temperature = 41.7f},
             .heap =
                 {.internal =
                      {.total = 362396,
                       .free = 237968,
                       .min_free = 225232,
                       .largest_free_block = 217088,
                       .free_blocks = 9},
                  .psram =
                      Telemetry::HeapUsage{
                          .total = 5000,
                          .free = 4000,
                          .min_free = 3000,
                          .largest_free_block = 2000,
                          .free_blocks = 1
                      }},
             .network =
                 {.wifi =
                      Telemetry::Wifi{
                          .mode = "sta",
                          .channel = 6,
                          .station =
                              Telemetry::Station{
                                  .mac =
                                      net_utils::format_mac({0x00, 0x00, 0x5E, 0x00, 0x53, 0xAF}),
                                  .bssid =
                                      net_utils::format_mac({0x00, 0x00, 0x5E, 0x00, 0x53, 0x01}),
                                  .rssi = -67,
                                  .reconnects = 2,
                                  .ipv4 = net_utils::format_ipv4({192, 168, 1, 5}),
                              },
                          .access_point = std::nullopt,
                      }},
             .messaging = {.mqtt = Telemetry::Mqtt{.connected = true, .reconnects = 4}}},
        .sensor =
            {.target = 55.5f,
             .deadband = 0.5f,
             .calibration = {.adc_min = 1200, .adc_max = 2900},
             .latest = 40.25f,
             .smoothed = 42.0f,
             .raw = 1234,
             .ready = true},
        .pump = {
            .run_count = 3,
            .on = true,
            .current_run_time_s = 4,
            .idle_time_s = 9,
            .automatic = true,
            .duration_s = 5,
            .wait_time_s = 60,
            .remaining_wait_time_s = 30,
            .total_run_time_s = 12,
        },
    };
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    CHECK(
        text_of(payload) ==
        R"({"sequence":7,"device":{"name":"test-device","uptime":90,"resetReason":"panic",)"
        R"("coreDump":true,"firmware":{"version":"1.0.0","build":"b6c3172ee"},)"
        R"("chip":{"model":"esp32c6","revision":"v0.1","temperature":41.7},)"
        R"("heap":{"internal":{"total":362396,"free":237968,"minFree":225232,"largestFreeBlock":217088,)"
        R"("freeBlocks":9},)"
        R"("psram":{"total":5000,"free":4000,"minFree":3000,"largestFreeBlock":2000,"freeBlocks":1}},)"
        R"("network":{"wifi":{"mode":"sta","channel":6,"station":{"mac":"00:00:5e:00:53:af",)"
        R"("bssid":"00:00:5e:00:53:01","rssi":-67,"reconnects":2,"ipv4":"192.168.1.5"}}},)"
        R"("messaging":{"mqtt":{"connected":true,"reconnects":4}}},)"
        R"("sensor":{"target":55.50,"deadband":0.50,"calibration":{"adcMin":1200,"adcMax":2900},)"
        R"("latest":40.25,"smoothed":42.00,"raw":1234,)"
        R"("ready":true,"faulted":false},)"
        R"("pump":{"runCount":3,"on":true,"currentRunTime":4,"idleTime":9,"automatic":true,"duration":5,)"
        R"("waitTime":60,"remainingWaitTime":30,"totalRunTime":12,"faulted":false},)"
        R"("faults":[]})"
    );
}

TEST_CASE("encode_telemetry writes the health flags a broken device raises") {
    Telemetry telemetry{
        .device = {},
        .sensor = {.ready = false, .faulted = true},
        .pump = {.faulted = true},
    };
    telemetry.device.core_dump = true;
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    const std::string_view text{text_of(payload)};
    CHECK(text.contains(R"("coreDump":true,"firmware":)"));
    CHECK(text.contains(R"("ready":false,"faulted":true},"pump":)"));
    CHECK(text.contains(R"("faulted":true},"faults":)"));
}

TEST_CASE("encode_telemetry writes the faults as a code and a flag each") {
    Telemetry telemetry{};
    telemetry.fault_count = 2;
    telemetry.faults[0] = {.code = "ETST01", .active = true};
    telemetry.faults[1] = {.code = "WTST01", .active = false};

    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    const std::string_view text{text_of(payload)};
    // The second is cleared, and still on the wire as a record of what happened.
    CHECK(text.ends_with(
        R"("faults":[{"code":"ETST01","active":true},{"code":"WTST01","active":false}]})"
    ));
    // The message never goes on the wire. A reader maps the code.
    CHECK_FALSE(text.contains("message"));
}

TEST_CASE("a station with nothing known yet writes every value as null, the keys still there") {
    Telemetry telemetry{};
    telemetry.device.network.wifi = Telemetry::Wifi{
        .mode = "sta",
        .channel = std::nullopt,
        .station = Telemetry::Station{},
        .access_point = std::nullopt,
    };
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    CHECK(text_of(payload).contains(
        R"("wifi":{"mode":"sta","channel":null,"station":{"mac":null,"bssid":null,"rssi":null,)"
        R"("reconnects":null,"ipv4":null}})"
    ));
}

TEST_CASE("an access point without an address writes it as null, the key still there") {
    Telemetry telemetry{};
    telemetry.device.network.wifi = Telemetry::Wifi{
        .mode = "ap",
        .channel = 1,
        .station = std::nullopt,
        .access_point = Telemetry::AccessPoint{},
    };
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    CHECK(text_of(payload).contains(
        R"("wifi":{"mode":"ap","channel":1,"accessPoint":{"clients":0,"ipv4":null}})"
    ));
}

TEST_CASE("an access point beside a station follows it in the frame") {
    Telemetry telemetry{};
    telemetry.device.network.wifi = Telemetry::Wifi{
        .mode = "apsta",
        .channel = 6,
        .station = Telemetry::Station{},
        .access_point =
            Telemetry::AccessPoint{.clients = 2, .ipv4 = net_utils::format_ipv4({192, 168, 4, 1})},
    };
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    CHECK(text_of(payload).contains(
        R"("ipv4":null},"accessPoint":{"clients":2,"ipv4":"192.168.4.1"}})"
    ));
}

TEST_CASE("no PSRAM in the heap goes out as null, the key still there") {
    const Telemetry telemetry{};
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    CHECK(text_of(payload).contains(R"("psram":null},)"));
}

TEST_CASE("the chip temperature goes out to one decimal") {
    Telemetry telemetry{};
    telemetry.device.chip.temperature = -3.3f;
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    CHECK(text_of(payload).contains(R"("temperature":-3.3},)"));
}

TEST_CASE("an empty chip temperature goes out as null, the key still there") {
    const Telemetry telemetry{};
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    CHECK(text_of(payload).contains(R"("temperature":null},)"));
}

TEST_CASE("without WiFi the network group is there, and empty") {
    const Telemetry telemetry{};
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    CHECK(text_of(payload).contains(R"("network":{},)"));
}

TEST_CASE("without MQTT the messaging group is there, and empty") {
    const Telemetry telemetry{};
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    CHECK(text_of(payload).contains(R"("messaging":{}},)"));
}

TEST_CASE("a broker that is out of reach goes out as not connected") {
    Telemetry telemetry{};
    telemetry.device.messaging.mqtt = Telemetry::Mqtt{.connected = false, .reconnects = 2};
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    CHECK(
        text_of(payload).contains(R"("messaging":{"mqtt":{"connected":false,"reconnects":2}}},)")
    );
}

TEST_CASE("a clean device still carries the faults key, empty") {
    const Telemetry telemetry{};
    Payload payload{};
    REQUIRE(json_codec::encode_telemetry(telemetry, payload));

    CHECK(text_of(payload).ends_with(R"("faults":[]})"));
}

TEST_CASE("a frame that overruns the payload is dropped whole") {
    // Real codes are six characters. A full store of these overruns the payload
    // whatever either is sized at.
    constexpr std::size_t code_length{Payload::BUFFER_SIZE / Telemetry::MAX_FAULTS};
    std::array<char, code_length + 1> long_code{};
    long_code.fill('0');
    long_code.back() = '\0';

    Telemetry telemetry{};
    telemetry.fault_count = Telemetry::MAX_FAULTS;
    for (Telemetry::Fault& fault : telemetry.faults) {
        fault = {.code = long_code.data()};
    }

    Payload payload{};
    CHECK_FALSE(json_codec::encode_telemetry(telemetry, payload));
    // Nothing half-written is handed on.
    CHECK(payload.length == 0);
}

TEST_CASE("a device name JSON cannot carry unescaped refuses the frame") {
    Telemetry telemetry{};
    telemetry.device.name = R"(pot "3")";

    Payload payload{};
    CHECK_FALSE(json_codec::encode_telemetry(telemetry, payload));
    CHECK(payload.length == 0);
}

TEST_CASE("a reset reason JSON cannot carry unescaped refuses the frame") {
    Telemetry telemetry{};
    telemetry.device.reset_reason = "pan\"ic";

    Payload payload{};
    CHECK_FALSE(json_codec::encode_telemetry(telemetry, payload));
    CHECK(payload.length == 0);
}

TEST_CASE("a firmware version JSON cannot carry unescaped refuses the frame") {
    Telemetry telemetry{};
    telemetry.device.firmware.version = R"(1.0\1)";

    Payload payload{};
    CHECK_FALSE(json_codec::encode_telemetry(telemetry, payload));
    CHECK(payload.length == 0);
}

TEST_CASE("a firmware build JSON cannot carry unescaped refuses the frame") {
    Telemetry telemetry{};
    telemetry.device.firmware.build = "b6c3\"172e";

    Payload payload{};
    CHECK_FALSE(json_codec::encode_telemetry(telemetry, payload));
    CHECK(payload.length == 0);
}

TEST_CASE("a chip model JSON cannot carry unescaped refuses the frame") {
    Telemetry telemetry{};
    telemetry.device.chip.model = "esp\"32";

    Payload payload{};
    CHECK_FALSE(json_codec::encode_telemetry(telemetry, payload));
    CHECK(payload.length == 0);
}

TEST_CASE("a chip revision JSON cannot carry unescaped refuses the frame") {
    Telemetry telemetry{};
    telemetry.device.chip.revision = R"(v0\1)";

    Payload payload{};
    CHECK_FALSE(json_codec::encode_telemetry(telemetry, payload));
    CHECK(payload.length == 0);
}

TEST_CASE("a WiFi mode JSON cannot carry unescaped refuses the frame") {
    Telemetry telemetry{};
    telemetry.device.network.wifi = Telemetry::Wifi{
        .mode = "st\\a",
        .channel = std::nullopt,
        .station = std::nullopt,
        .access_point = std::nullopt,
    };

    Payload payload{};
    CHECK_FALSE(json_codec::encode_telemetry(telemetry, payload));
    CHECK(payload.length == 0);
}

TEST_CASE("a fault code JSON cannot carry unescaped refuses the frame") {
    Telemetry telemetry{};
    telemetry.fault_count = 2;
    telemetry.faults[0] = {.code = "ETST01", .active = true};
    telemetry.faults[1] = {.code = "WTST\n1", .active = true};

    Payload payload{};
    CHECK_FALSE(json_codec::encode_telemetry(telemetry, payload));
    CHECK(payload.length == 0);
}

TEST_CASE("decode_command takes out every carried field as sent") {
    const std::optional<Command> command{json_codec::decode_command(make_payload(
        R"({"sensor":{"target":55.5,"deadband":0.5},"pump":{"duration":7,"waitTime":60}})"
    ))};
    REQUIRE(command);
    CHECK(command->sensor.target == 55.5);
    CHECK(command->sensor.deadband == 0.5);
    CHECK(command->pump.duration_s == 7);
    CHECK(command->pump.wait_time_s == 60);
}

TEST_CASE("decode_command takes out the calibration from under the sensor group") {
    const std::optional<Command> command{json_codec::decode_command(
        make_payload(R"({"sensor":{"calibration":{"adcMin":1200,"adcMax":2900.5}}})")
    )};
    REQUIRE(command);
    CHECK(command->sensor.calibration.adc_min == 1200);
    CHECK(command->sensor.calibration.adc_max == 2900.5);
    CHECK_FALSE(command->sensor.target);
}

TEST_CASE("calibration fields outside their object are ignored") {
    const std::optional<Command> command{json_codec::decode_command(
        make_payload(R"({"sensor":{"adcMin":1200},"calibration":{"adcMax":2900}})")
    )};
    REQUIRE(command);
    CHECK_FALSE(command->sensor.calibration.adc_min);
    CHECK_FALSE(command->sensor.calibration.adc_max);
}

TEST_CASE("decode_command passes values on unconverted and unjudged") {
    const std::optional<Command> command{json_codec::decode_command(make_payload(
        R"({"sensor":{"target":150,"deadband":0.26},"pump":{"duration":2.7,"waitTime":-1}})"
    ))};
    REQUIRE(command);
    CHECK(command->sensor.target == 150);
    CHECK(command->sensor.deadband == 0.26);
    CHECK(command->pump.duration_s == 2.7);
    CHECK(command->pump.wait_time_s == -1);
}

TEST_CASE("fields the payload does not carry stay unset") {
    constexpr std::string_view json{R"({"sensor":{"deadband":0.5},"pump":{"duration":7}})"};
    const std::optional<Command> command{json_codec::decode_command(make_payload(json))};
    REQUIRE(command);
    CHECK_FALSE(command->sensor.target);
    CHECK(command->sensor.deadband);
    CHECK(command->pump.duration_s);
    CHECK_FALSE(command->pump.wait_time_s);
}

TEST_CASE("groups the payload does not carry leave their fields unset") {
    const std::optional<Command> command{json_codec::decode_command(make_payload("{}"))};
    REQUIRE(command);
    CHECK_FALSE(command->sensor.target);
    CHECK_FALSE(command->sensor.deadband);
    CHECK_FALSE(command->pump.duration_s);
    CHECK_FALSE(command->pump.wait_time_s);
}

TEST_CASE("fields outside their group are ignored") {
    const std::optional<Command> command{
        json_codec::decode_command(make_payload(R"({"target":55.5,"duration":7})"))
    };
    REQUIRE(command);
    CHECK_FALSE(command->sensor.target);
    CHECK_FALSE(command->pump.duration_s);
}

TEST_CASE("a group carried as a non-object is ignored") {
    const std::optional<Command> command{
        json_codec::decode_command(make_payload(R"({"sensor":55.5,"pump":[7]})"))
    };
    REQUIRE(command);
    CHECK_FALSE(command->sensor.target);
    CHECK_FALSE(command->pump.duration_s);
}

TEST_CASE("non-number fields are ignored") {
    const std::optional<Command> command{json_codec::decode_command(
        make_payload(R"({"sensor":{"target":"high"},"pump":{"duration":true,"waitTime":[60]}})")
    )};
    REQUIRE(command);
    CHECK_FALSE(command->sensor.target);
    CHECK_FALSE(command->pump.duration_s);
    CHECK_FALSE(command->pump.wait_time_s);
}

TEST_CASE("decode_command takes out automatic as sent") {
    const std::optional<Command> off{
        json_codec::decode_command(make_payload(R"({"pump":{"automatic":false}})"))
    };
    REQUIRE(off);
    CHECK(off->pump.automatic == false);
    const std::optional<Command> on{
        json_codec::decode_command(make_payload(R"({"pump":{"automatic":true}})"))
    };
    REQUIRE(on);
    CHECK(on->pump.automatic == true);
}

TEST_CASE("a non-boolean automatic is ignored") {
    for (const std::string_view json :
         {R"({"pump":{"automatic":0}})",
          R"({"pump":{"automatic":"false"}})",
          R"({"pump":{"automatic":null}})"}) {
        const std::optional<Command> command{json_codec::decode_command(make_payload(json))};
        REQUIRE(command);
        CHECK_FALSE(command->pump.automatic);
    }
}

TEST_CASE("decode_command takes out run and stop as sent") {
    const std::optional<Command> command{
        json_codec::decode_command(make_payload(R"({"pump":{"run":true,"stop":false}})"))
    };
    REQUIRE(command);
    CHECK(command->pump.run == true);
    CHECK(command->pump.stop == false);
}

TEST_CASE("a non-boolean run or stop is ignored") {
    for (const std::string_view json :
         {R"({"pump":{"run":1,"stop":1}})",
          R"({"pump":{"run":"true","stop":"true"}})",
          R"({"pump":{"run":null,"stop":null}})"}) {
        const std::optional<Command> command{json_codec::decode_command(make_payload(json))};
        REQUIRE(command);
        CHECK_FALSE(command->pump.run);
        CHECK_FALSE(command->pump.stop);
    }
}

TEST_CASE("a payload that is not valid JSON yields nullopt") {
    CHECK_FALSE(json_codec::decode_command(make_payload("pump.duration=7")));
    CHECK_FALSE(json_codec::decode_command(make_payload("")));
}

// Depth is capped at a command group and an object under it.
TEST_CASE("a payload nested past the limit yields nullopt") {
    CHECK(json_codec::decode_command(nested(3)));
    CHECK_FALSE(json_codec::decode_command(nested(4)));

    // The full command shape sits inside the cap.
    CHECK(
        json_codec::decode_command(make_payload(
            R"({"sensor":{"target":50,"deadband":0.5,"calibration":{"adcMin":1200,"adcMax":2900}},)"
            R"("pump":{"duration":60,"waitTime":3600}})"
        ))
    );

    // An extra field beside a readable one still reads it.
    CHECK(json_codec::decode_command(make_payload(R"({"pump":{"duration":60,"extra":[1]}})")));

    // One nested deeper than the shape takes the payload with it.
    CHECK_FALSE(
        json_codec::decode_command(make_payload(R"({"pump":{"duration":60,"extra":{"x":{}}}})"))
    );
}
