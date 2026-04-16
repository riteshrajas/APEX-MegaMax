#ifndef APEX_PROTOCOL_H
#define APEX_PROTOCOL_H

#include <Arduino.h>
#include <ArduinoJson.h>
#include "MegaMaxConfig.h"

namespace megamax {

struct AspCommand {
    String id;
    String type;
    String command;
    String requestedMode;
    String transport;
    bool powerSave = false;
    uint32_t telemetryIntervalMs = 0;
};

class ApexProtocol {
public:
    bool parseFrame(const String& frame, AspCommand& outCommand) const;
    bool parseSmsPayload(const String& payload, AspCommand& outCommand) const;
    String buildIdentity(const MegaMaxConfig& config) const;
    String buildTelemetry(const MegaMaxConfig& config, LinkMode mode, const ModemSnapshot& snapshot) const;
    String buildAck(const String& id, const String& status, LinkMode mode, const char* transport, const String& detail) const;
    String buildHeartbeat(const MegaMaxConfig& config, LinkMode mode) const;
};

}  // namespace megamax

#endif
