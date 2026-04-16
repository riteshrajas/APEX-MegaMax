#include "MegaMaxController.h"

namespace megamax {

namespace {

#if defined(ARDUINO_ARCH_AVR)
constexpr uint8_t MODEM_RX_PIN = 7;
constexpr uint8_t MODEM_TX_PIN = 8;
#elif defined(ARDUINO_ARCH_RP2040)
constexpr uint8_t MODEM_RX_PIN = 1;
constexpr uint8_t MODEM_TX_PIN = 0;
#else
constexpr uint8_t MODEM_RX_PIN = 16;
constexpr uint8_t MODEM_TX_PIN = 17;
#endif

}  // namespace

MegaMaxController::MegaMaxController(const MegaMaxConfig& config, Stream& debugStream)
#if defined(ARDUINO_ARCH_AVR)
    : modemSerial_(MODEM_RX_PIN, MODEM_TX_PIN),
#else
    : modemSerial_(Serial1),
#endif
      debug_(debugStream),
      config_(config),
      stateMachine_(config_),
      modem_(modemSerial_),
      client_(modem_),
      lastTelemetryMs_(0),
      lastHeartbeatMs_(0),
      lastDataAttemptMs_(0),
      lastSmsPollMs_(0) {}

void MegaMaxController::beginSerials() {
#if defined(ARDUINO_ARCH_AVR)
    modemSerial_.begin(APEX_MODEM_BAUD);
#elif defined(ARDUINO_ARCH_RP2040) || defined(ARDUINO_ARCH_ESP32)
    modemSerial_.begin(APEX_MODEM_BAUD, SERIAL_8N1, MODEM_RX_PIN, MODEM_TX_PIN);
#else
    modemSerial_.begin(APEX_MODEM_BAUD);
#endif
}

void MegaMaxController::begin() {
    beginSerials();
    delay(250);

    snapshot_.modemReady = modem_.init();
    snapshot_.smsAvailable = snapshot_.modemReady;

#if defined(APEX_SIM7000G)
    modem_.enableGPS();
#endif

    sendFrame(protocol_.buildIdentity(config_));
}

bool MegaMaxController::ensureNetworkReady() {
    if (!snapshot_.modemReady) {
        snapshot_.modemReady = modem_.init();
        if (!snapshot_.modemReady) {
            return false;
        }
    }

    if (!modem_.isNetworkConnected()) {
        if (!modem_.waitForNetwork(60000L)) {
            stateMachine_.noteDataFailure();
            return false;
        }
    }

    snapshot_.networkRegistered = true;
    return true;
}

bool MegaMaxController::ensureDataSocket() {
    if ((millis() - lastDataAttemptMs_) < config_.dataRetryIntervalMs) {
        return snapshot_.socketConnected;
    }

    lastDataAttemptMs_ = millis();

    if (!ensureNetworkReady()) {
        return false;
    }

    if (!modem_.isGprsConnected()) {
        if (!modem_.gprsConnect(config_.apn, config_.apnUser, config_.apnPass)) {
            stateMachine_.noteDataFailure();
            snapshot_.dataAttached = false;
            return false;
        }
    }

    snapshot_.dataAttached = true;

    if (!client_.connected()) {
        if (!client_.connect(config_.bridgeHost, config_.bridgePort)) {
            stateMachine_.noteDataFailure();
            snapshot_.socketConnected = false;
            return false;
        }
    }

    snapshot_.socketConnected = true;
    stateMachine_.clearDataFailures();
    return true;
}

void MegaMaxController::refreshSnapshot() {
    snapshot_.networkRegistered = modem_.isNetworkConnected();
    snapshot_.dataAttached = modem_.isGprsConnected();
    snapshot_.socketConnected = client_.connected();
    snapshot_.smsAvailable = snapshot_.modemReady;
    snapshot_.signalQuality = modem_.getSignalQuality();

#if defined(APEX_SIM7000G)
    snapshot_.gps.valid = modem_.getGPS(
        &snapshot_.gps.latitude,
        &snapshot_.gps.longitude,
        &snapshot_.gps.speed,
        &snapshot_.gps.altitude,
        &snapshot_.gps.visibleSatellites,
        &snapshot_.gps.usedSatellites,
        &snapshot_.gps.accuracy,
        &snapshot_.gps.year,
        &snapshot_.gps.month,
        &snapshot_.gps.day,
        &snapshot_.gps.hour,
        &snapshot_.gps.minute,
        &snapshot_.gps.second
    );
#else
    snapshot_.gps.valid = false;
#endif
}

void MegaMaxController::sendFrame(const String& frame) {
    if (client_.connected()) {
        client_.print(frame);
    }
    debug_.print(frame);
}

void MegaMaxController::sendSms(const String& body) {
    modem_.sendSMS(config_.smsController, body);
    debug_.println(F("[MegaMax] SMS sent"));
}

bool MegaMaxController::queryUnreadSms(String& payload) {
    modemSerial_.println(F("AT+CMGF=1"));
    delay(100);
    modemSerial_.println(F("AT+CMGL=\"REC UNREAD\""));

    const uint32_t deadline = millis() + 1500UL;
    String response;
    while (millis() < deadline) {
        while (modemSerial_.available()) {
            const char c = static_cast<char>(modemSerial_.read());
            response += c;
        }
    }

    const int bodyIndex = response.indexOf("\n");
    if (bodyIndex < 0) {
        return false;
    }

    payload = response.substring(bodyIndex + 1);
    payload.trim();
    return !payload.isEmpty();
}

void MegaMaxController::handleInboundSocketFrames() {
    if (!client_.connected()) {
        return;
    }

    while (client_.available()) {
        String frame = client_.readStringUntil('\n');
        frame.trim();
        if (frame.isEmpty()) {
            continue;
        }

        AspCommand command;
        if (protocol_.parseFrame(frame, command)) {
            stateMachine_.noteActivity(millis());
            dispatchCommand(command, "lte");
        }
    }
}

void MegaMaxController::handleInboundSms() {
    if ((millis() - lastSmsPollMs_) < config_.smsPollIntervalMs) {
        return;
    }
    lastSmsPollMs_ = millis();

    String payload;
    if (!queryUnreadSms(payload)) {
        return;
    }

    AspCommand command;
    if (protocol_.parseSmsPayload(payload, command)) {
        stateMachine_.noteActivity(millis());
        snapshot_.wakeRequested = true;
        dispatchCommand(command, "sms");
    }
}

void MegaMaxController::dispatchCommand(const AspCommand& command, const char* transport) {
    String detail = "ok";

    if (command.command == "PING") {
        detail = "pong";
    } else if (command.command == "GET_STATUS") {
        if (strcmp(transport, "lte") == 0) {
            sendFrame(protocol_.buildTelemetry(config_, stateMachine_.currentMode(), snapshot_));
        } else {
            sendSms(protocol_.buildTelemetry(config_, stateMachine_.currentMode(), snapshot_));
        }
    } else if (command.command == "SET_MODE") {
        if (command.requestedMode == "sleep") {
            snapshot_.idleEligible = true;
        } else if (command.requestedMode == "data") {
            snapshot_.wakeRequested = true;
            stateMachine_.clearDataFailures();
        } else if (command.requestedMode == "sms") {
            stateMachine_.noteDataFailure();
            stateMachine_.noteDataFailure();
            stateMachine_.noteDataFailure();
        } else {
            detail = "unknown mode";
        }
    } else if (command.command == "SLEEP") {
        snapshot_.idleEligible = true;
    } else if (command.command == "WAKE") {
        snapshot_.wakeRequested = true;
        snapshot_.idleEligible = false;
    } else {
        detail = "unknown command";
    }

    const String ack = protocol_.buildAck(
        command.id,
        detail == "unknown command" || detail == "unknown mode" ? "error" : "ok",
        stateMachine_.currentMode(),
        transport,
        detail
    );

    if (strcmp(transport, "lte") == 0) {
        sendFrame(ack);
    } else {
        sendSms(ack);
    }
}

void MegaMaxController::publishTelemetry(uint32_t nowMs) {
    if ((nowMs - lastTelemetryMs_) < config_.telemetryIntervalMs) {
        return;
    }
    lastTelemetryMs_ = nowMs;

    const String payload = protocol_.buildTelemetry(config_, stateMachine_.currentMode(), snapshot_);
    if (stateMachine_.currentMode() == LinkMode::Data) {
        sendFrame(payload);
    } else if (stateMachine_.currentMode() == LinkMode::SmsFallback) {
        sendSms(payload);
    }
}

void MegaMaxController::publishHeartbeat(uint32_t nowMs) {
    if ((nowMs - lastHeartbeatMs_) < config_.heartbeatIntervalMs) {
        return;
    }
    lastHeartbeatMs_ = nowMs;

    const String heartbeat = protocol_.buildHeartbeat(config_, stateMachine_.currentMode());
    if (stateMachine_.currentMode() == LinkMode::Data) {
        sendFrame(heartbeat);
    } else if (stateMachine_.currentMode() == LinkMode::SmsFallback) {
        sendSms(heartbeat);
    }
}

void MegaMaxController::enterSleep() {
    client_.stop();
    modem_.gprsDisconnect();
    modemSerial_.println(F("AT+CSCLK=2"));
    debug_.println(F("[MegaMax] Entering sleep mode"));
}

void MegaMaxController::exitSleep() {
    modemSerial_.println(F("AT"));
    snapshot_.wakeRequested = false;
    snapshot_.idleEligible = false;
    debug_.println(F("[MegaMax] Waking modem"));
}

void MegaMaxController::onModeChanged(LinkMode nextMode) {
    if (nextMode == LinkMode::Data) {
        ensureDataSocket();
    } else if (nextMode == LinkMode::SmsFallback) {
        client_.stop();
    } else if (nextMode == LinkMode::Sleep) {
        enterSleep();
    } else if (nextMode == LinkMode::Connecting) {
        exitSleep();
    }
}

void MegaMaxController::update(uint32_t nowMs) {
    snapshot_.idleEligible = (nowMs - lastHeartbeatMs_) > config_.sleepIdleMs;

    ensureNetworkReady();
    refreshSnapshot();

    if (stateMachine_.currentMode() != LinkMode::Sleep) {
        ensureDataSocket();
        handleInboundSocketFrames();
        handleInboundSms();
    }

    const LinkMode nextMode = stateMachine_.update(snapshot_, nowMs);
    if (stateMachine_.transitionOccurred()) {
        onModeChanged(nextMode);
        stateMachine_.clearTransitionFlag();
    }

    publishHeartbeat(nowMs);
    publishTelemetry(nowMs);
}

}  // namespace megamax
