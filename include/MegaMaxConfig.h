#ifndef APEX_MEGAMAX_CONFIG_H
#define APEX_MEGAMAX_CONFIG_H

#include <Arduino.h>

namespace megamax {

struct MegaMaxConfig {
    const char* nodeId = "megamax-l3";
    const char* apn = "internet";
    const char* apnUser = "";
    const char* apnPass = "";
    const char* bridgeHost = "apex-core.local";
    uint16_t bridgePort = APEX_BRIDGE_PORT;
    const char* smsController = "+10000000000";

    uint32_t telemetryIntervalMs = APEX_TELEMETRY_INTERVAL_MS;
    uint32_t heartbeatIntervalMs = APEX_HEARTBEAT_INTERVAL_MS;
    uint32_t dataRetryIntervalMs = APEX_DATA_RETRY_INTERVAL_MS;
    uint32_t smsPollIntervalMs = APEX_SMS_POLL_INTERVAL_MS;
    uint32_t sleepIdleMs = APEX_SLEEP_IDLE_MS;
    uint8_t maxDataFailures = APEX_DATA_FAIL_THRESHOLD;
    bool allowSleep = true;
};

struct GpsFix {
    bool valid = false;
    float latitude = 0.0F;
    float longitude = 0.0F;
    float speed = 0.0F;
    float altitude = 0.0F;
    int visibleSatellites = 0;
    int usedSatellites = 0;
    float accuracy = 0.0F;
    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
};

struct ModemSnapshot {
    bool modemReady = false;
    bool networkRegistered = false;
    bool dataAttached = false;
    bool socketConnected = false;
    bool smsAvailable = false;
    bool wakeRequested = false;
    bool idleEligible = false;
    int signalQuality = 0;
    GpsFix gps;
};

enum class LinkMode {
    Boot,
    Connecting,
    Data,
    SmsFallback,
    Sleep
};

inline const char* toString(LinkMode mode) {
    switch (mode) {
        case LinkMode::Boot: return "boot";
        case LinkMode::Connecting: return "connecting";
        case LinkMode::Data: return "data";
        case LinkMode::SmsFallback: return "sms";
        case LinkMode::Sleep: return "sleep";
        default: return "unknown";
    }
}

}  // namespace megamax

#endif
