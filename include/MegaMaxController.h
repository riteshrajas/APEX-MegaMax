#ifndef APEX_MEGAMAX_CONTROLLER_H
#define APEX_MEGAMAX_CONTROLLER_H

#include <Arduino.h>
#include "ApexProtocol.h"
#include "MegaMaxStateMachine.h"

#if defined(APEX_SIM7000G)
  #define TINY_GSM_MODEM_SIM7000
#elif defined(APEX_SIM800L)
  #define TINY_GSM_MODEM_SIM800
#else
  #define TINY_GSM_MODEM_SIM800
#endif

#include <TinyGsmClient.h>

#if defined(ARDUINO_ARCH_AVR)
  #include <SoftwareSerial.h>
#endif

namespace megamax {

class MegaMaxController {
public:
    MegaMaxController(const MegaMaxConfig& config, Stream& debugStream);
    void begin();
    void update(uint32_t nowMs);

private:
    void beginSerials();
    bool ensureNetworkReady();
    bool ensureDataSocket();
    void refreshSnapshot();
    void handleInboundSocketFrames();
    void handleInboundSms();
    void dispatchCommand(const AspCommand& command, const char* transport);
    void publishTelemetry(uint32_t nowMs);
    void publishHeartbeat(uint32_t nowMs);
    void sendFrame(const String& frame);
    void sendSms(const String& body);
    bool queryUnreadSms(String& payload);
    void enterSleep();
    void exitSleep();
    void onModeChanged(LinkMode nextMode);

#if defined(ARDUINO_ARCH_AVR)
    SoftwareSerial modemSerial_;
#else
    HardwareSerial& modemSerial_;
#endif
    Stream& debug_;
    const MegaMaxConfig config_;
    ApexProtocol protocol_;
    MegaMaxStateMachine stateMachine_;
    TinyGsm modem_;
    TinyGsmClient client_;
    ModemSnapshot snapshot_;
    uint32_t lastTelemetryMs_;
    uint32_t lastHeartbeatMs_;
    uint32_t lastDataAttemptMs_;
    uint32_t lastSmsPollMs_;
};

}  // namespace megamax

#endif
