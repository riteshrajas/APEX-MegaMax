#ifndef APEX_MEGAMAX_STATE_MACHINE_H
#define APEX_MEGAMAX_STATE_MACHINE_H

#include <Arduino.h>
#include "MegaMaxConfig.h"

namespace megamax {

class MegaMaxStateMachine {
public:
    explicit MegaMaxStateMachine(const MegaMaxConfig& config);

    LinkMode currentMode() const;
    bool transitionOccurred() const;
    void clearTransitionFlag();
    LinkMode update(const ModemSnapshot& snapshot, uint32_t nowMs);
    void noteActivity(uint32_t nowMs);
    void noteDataFailure();
    void clearDataFailures();

private:
    const MegaMaxConfig& config_;
    LinkMode mode_;
    bool transitioned_;
    uint32_t modeSinceMs_;
    uint32_t lastActivityMs_;
    uint8_t dataFailures_;
};

}  // namespace megamax

#endif
