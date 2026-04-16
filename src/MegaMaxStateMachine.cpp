#include "MegaMaxStateMachine.h"

namespace megamax {

MegaMaxStateMachine::MegaMaxStateMachine(const MegaMaxConfig& config)
    : config_(config),
      mode_(LinkMode::Boot),
      transitioned_(false),
      modeSinceMs_(0),
      lastActivityMs_(0),
      dataFailures_(0) {}

LinkMode MegaMaxStateMachine::currentMode() const {
    return mode_;
}

bool MegaMaxStateMachine::transitionOccurred() const {
    return transitioned_;
}

void MegaMaxStateMachine::clearTransitionFlag() {
    transitioned_ = false;
}

void MegaMaxStateMachine::noteActivity(uint32_t nowMs) {
    lastActivityMs_ = nowMs;
}

void MegaMaxStateMachine::noteDataFailure() {
    if (dataFailures_ < 255) {
        ++dataFailures_;
    }
}

void MegaMaxStateMachine::clearDataFailures() {
    dataFailures_ = 0;
}

LinkMode MegaMaxStateMachine::update(const ModemSnapshot& snapshot, uint32_t nowMs) {
    LinkMode next = mode_;
    const bool idleExpired = (nowMs - lastActivityMs_) >= config_.sleepIdleMs;

    switch (mode_) {
        case LinkMode::Boot:
            if (snapshot.modemReady) {
                next = LinkMode::Connecting;
            }
            break;

        case LinkMode::Connecting:
            if (snapshot.networkRegistered && snapshot.dataAttached && snapshot.socketConnected) {
                next = LinkMode::Data;
            } else if (snapshot.networkRegistered && snapshot.smsAvailable && dataFailures_ >= config_.maxDataFailures) {
                next = LinkMode::SmsFallback;
            }
            break;

        case LinkMode::Data:
            if (!snapshot.socketConnected || !snapshot.dataAttached || dataFailures_ >= config_.maxDataFailures) {
                next = snapshot.smsAvailable ? LinkMode::SmsFallback : LinkMode::Connecting;
            } else if (config_.allowSleep && snapshot.idleEligible && idleExpired) {
                next = LinkMode::Sleep;
            }
            break;

        case LinkMode::SmsFallback:
            if (snapshot.networkRegistered && snapshot.dataAttached && snapshot.socketConnected) {
                next = LinkMode::Data;
            } else if (config_.allowSleep && snapshot.idleEligible && idleExpired) {
                next = LinkMode::Sleep;
            }
            break;

        case LinkMode::Sleep:
            if (snapshot.wakeRequested) {
                next = LinkMode::Connecting;
            }
            break;
    }

    if (next != mode_) {
        mode_ = next;
        transitioned_ = true;
        modeSinceMs_ = nowMs;
        if (next == LinkMode::Data) {
            dataFailures_ = 0;
        }
    }

    return mode_;
}

}  // namespace megamax
