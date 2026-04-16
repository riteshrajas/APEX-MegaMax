#include "ApexProtocol.h"

namespace megamax {

namespace {

template <typename T>
void setIfPresent(JsonVariantConst source, const char* key, T& target) {
    if (!source[key].isNull()) {
        target = source[key].as<T>();
    }
}

}  // namespace

bool ApexProtocol::parseFrame(const String& frame, AspCommand& outCommand) const {
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, frame);
    if (error) {
        return false;
    }

    setIfPresent(doc.as<JsonVariantConst>(), "id", outCommand.id);
    setIfPresent(doc.as<JsonVariantConst>(), "type", outCommand.type);
    setIfPresent(doc.as<JsonVariantConst>(), "command", outCommand.command);
    setIfPresent(doc.as<JsonVariantConst>(), "transport", outCommand.transport);

    JsonVariantConst config = doc["config"];
    if (!config.isNull()) {
        setIfPresent(config, "mode", outCommand.requestedMode);
        setIfPresent(config, "powerSave", outCommand.powerSave);
        setIfPresent(config, "telemetryIntervalMs", outCommand.telemetryIntervalMs);
    }

    if (outCommand.command.isEmpty() && !doc["query"].isNull()) {
        outCommand.command = doc["query"].as<String>();
    }

    return !outCommand.command.isEmpty();
}

bool ApexProtocol::parseSmsPayload(const String& payload, AspCommand& outCommand) const {
    String frame = payload;
    frame.trim();

    if (frame.startsWith("ASP ")) {
        frame.remove(0, 4);
        frame.trim();
    }

    if (frame.startsWith("{")) {
        return parseFrame(frame, outCommand);
    }

    outCommand.type = "command";
    outCommand.command = frame;
    outCommand.transport = "sms";
    return !outCommand.command.isEmpty();
}

String ApexProtocol::buildIdentity(const MegaMaxConfig& config) const {
    JsonDocument doc;
    doc["type"] = "identity";
    doc["protocol"] = "ASP v2.0";
    doc["node"] = config.nodeId;
    doc["level"] = 3;
    doc["transport"] = "cellular";
#if defined(APEX_SIM7000G)
    doc["modem"] = "SIM7000G";
#else
    doc["modem"] = "SIM800L";
#endif
    String output;
    serializeJson(doc, output);
    output += "\n";
    return output;
}

String ApexProtocol::buildTelemetry(const MegaMaxConfig& config, LinkMode mode, const ModemSnapshot& snapshot) const {
    JsonDocument doc;
    doc["type"] = "telemetry";
    doc["protocol"] = "ASP v2.0";
    doc["node"] = config.nodeId;
    doc["transport"] = (mode == LinkMode::Data) ? "lte" : ((mode == LinkMode::SmsFallback) ? "sms" : "idle");
    doc["linkMode"] = toString(mode);
    doc["signalQuality"] = snapshot.signalQuality;
    doc["networkRegistered"] = snapshot.networkRegistered;
    doc["dataAttached"] = snapshot.dataAttached;
    doc["socketConnected"] = snapshot.socketConnected;

    JsonObject gps = doc["gps"].to<JsonObject>();
    gps["fix"] = snapshot.gps.valid;
    if (snapshot.gps.valid) {
        gps["lat"] = snapshot.gps.latitude;
        gps["lon"] = snapshot.gps.longitude;
        gps["speed"] = snapshot.gps.speed;
        gps["alt"] = snapshot.gps.altitude;
        gps["vsat"] = snapshot.gps.visibleSatellites;
        gps["usat"] = snapshot.gps.usedSatellites;
        gps["accuracy"] = snapshot.gps.accuracy;

        char utc[24];
        snprintf(
            utc,
            sizeof(utc),
            "%04d-%02d-%02dT%02d:%02d:%02dZ",
            snapshot.gps.year,
            snapshot.gps.month,
            snapshot.gps.day,
            snapshot.gps.hour,
            snapshot.gps.minute,
            snapshot.gps.second
        );
        gps["timestamp"] = utc;
    }

    String output;
    serializeJson(doc, output);
    output += "\n";
    return output;
}

String ApexProtocol::buildAck(
    const String& id,
    const String& status,
    LinkMode mode,
    const char* transport,
    const String& detail
) const {
    JsonDocument doc;
    doc["type"] = "ack";
    doc["id"] = id;
    doc["status"] = status;
    doc["transport"] = transport;
    doc["linkMode"] = toString(mode);
    doc["detail"] = detail;

    String output;
    serializeJson(doc, output);
    output += "\n";
    return output;
}

String ApexProtocol::buildHeartbeat(const MegaMaxConfig& config, LinkMode mode) const {
    JsonDocument doc;
    doc["type"] = "event";
    doc["event"] = "heartbeat";
    doc["protocol"] = "ASP v2.0";
    doc["node"] = config.nodeId;
    doc["linkMode"] = toString(mode);

    String output;
    serializeJson(doc, output);
    output += "\n";
    return output;
}

}  // namespace megamax
