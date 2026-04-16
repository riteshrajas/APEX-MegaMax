/**
 * @file main.cpp
 * @brief APEX MegaMax Level 3 cellular node entry point.
 */

#include <Arduino.h>
#include "MegaMaxController.h"

namespace {

megamax::MegaMaxConfig config;
megamax::MegaMaxController controller(config, Serial);

}  // namespace

void setup() {
    Serial.begin(APEX_HOST_BAUD);
    delay(250);
    controller.begin();
}

void loop() {
    controller.update(millis());
    delay(50);
}
