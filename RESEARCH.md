# MegaMax Research

## Scope
This research covers the MegaMax Level 3 cellular node requirements:
- TinyGSM integration for SIM7000G and SIM800L
- LTE data socket management
- SMS fail-over control
- GPS telemetry collection
- power-saving sleep behavior

## Findings

### 1. TinyGSM is a workable portability layer, but only for part of the problem
- TinyGSM supports both SIM800-series and SIM7000-series modems and exposes a shared Arduino `Client`-style interface for TCP data links.
- The library guidance is consistent: production firmware should use a fixed UART baud rate, not auto-bauding.
- The normal connection flow is: initialize modem, unlock SIM if needed, wait for network registration, attach GPRS/EPS using the APN, then open the TCP client socket.
- TinyGSM is blocking. `waitForNetwork()`, `gprsConnect()`, and `client.connect()` can stall for long periods in poor radio conditions, so MegaMax should isolate those calls behind a coarse state machine and retry scheduler instead of scattering them through `loop()`.

Design implication:
- Use TinyGSM for modem init, data attach, TCP client sockets, outbound SMS, and GNSS access.
- Keep timing and failure policy in a MegaMax-owned state machine.

### 2. SIM7000G and SIM800L have different strengths, so the firmware should degrade cleanly
- SIM7000 supports LTE Cat-M1 / NB-IoT and embedded GNSS. TinyGSM notes that the non-SSL SIM7000 mode supports more simultaneous connections than the SSL mode.
- SIM800 is a 2G GSM/GPRS modem. It supports TCP and SMS, but LTE and embedded GNSS are not universally available on SIM800L-class boards.
- TinyGSM documents SIM7000 GNSS support and SIM800 GSM location service support, but only outbound SMS is supported directly by the library.

Design implication:
- MegaMax should treat GPS as capability-based, not mandatory.
- The main transport policy should be `data-first -> SMS fallback -> sleep`.
- SIM800 builds should still compile and operate without GNSS.

### 3. Inbound SMS control should not rely on TinyGSM alone
- TinyGSM explicitly supports only sending SMS, not receiving it.
- SIMCom modems expose SMS receive flows through AT command URCs and mailbox queries (`+CMTI`, `CMGL`, `CMGR`) rather than TinyGSM’s high-level API.

Design implication:
- Implement inbound SMS fail-over control with direct AT commands on the modem UART.
- Keep SMS commands constrained to a small ASP-compatible subset such as `PING`, `WAKE`, `SLEEP`, `SET_MODE`, and `GET_STATUS`.
- Require SMS commands to be either raw ASP JSON or a short `ASP <json>` envelope to reduce parser ambiguity.

### 4. GPS telemetry should include quality metadata, not only coordinates
- TinyGSM examples and downstream references show `enableGPS()` plus `getGPS(...)` patterns that can return latitude, longitude, speed, altitude, visible/used satellites, accuracy-like fields, and UTC date/time elements.
- GPS fix time and precision are highly environment-dependent. Outdoor sky view matters more than firmware alone.

Design implication:
- Publish GPS telemetry with `fix`, `lat`, `lon`, `speed`, `alt`, `vsat`, `usat`, and `accuracy` fields when available.
- Do not claim a valid location if no fix is present.
- Prefer including UTC timestamp and fix metadata so the Core can reason about stale or low-confidence positions.

### 5. Cellular resilience is mostly power, retry policy, and heartbeat policy
- TinyGSM’s own troubleshooting emphasizes stable power delivery as a top reliability requirement, often around 2A peak current for cellular bursts.
- SIM7000-family modules support low-power modes such as PSM/eDRX in vendor documentation, and SIMCom markets the LPWA family around long battery life.
- Long-lived sockets can drop silently, especially with weak service or remote peer closes, so application heartbeats are required even when the modem still reports network attachment.

Design implication:
- MegaMax should track network registration, packet data attach, and application socket health separately.
- The state machine should demote from LTE Data to SMS when the TCP bridge misses heartbeats or reconnect retries exceed budget.
- Sleep entry must be deliberate and reversible: only after an idle window and never while commands are pending.

## Firmware Strategy

### Transport model
1. Bring up the modem with fixed UART settings.
2. Register on the carrier network.
3. Attach packet data with APN credentials.
4. Open a TCP socket to the APEX Core bridge.
5. Exchange newline-delimited ASP v2 JSON frames.
6. If data attachment or socket health degrades past threshold, switch to SMS fail-over.
7. If the node is idle and power-save is allowed, enter sleep and wake on timer or external event.

### ASP v2 usage on MegaMax
- Keep framing newline-delimited JSON for parity with existing ASP patterns.
- Add transport metadata instead of inventing a second protocol:
  - `transport: "lte"` for data socket traffic
  - `transport: "sms"` for fail-over control
  - `type: "telemetry" | "ack" | "event" | "command"`
- Suggested control commands:
  - `PING`
  - `GET_STATUS`
  - `SET_MODE`
  - `SLEEP`
  - `WAKE`

### Sleep policy
- Sleep is a top-level state, not a background flag.
- Enter sleep only when:
  - no inbound control is pending
  - the idle timer has expired
  - fail-over delivery is not actively needed
- On wake, restart at cellular registration rather than assuming the previous data session is still valid.

## Sources
- TinyGSM README: https://github.com/vshymanskyy/TinyGSM
- SIMCom SIM7070E product page: https://en.simcom.com/product/sim7070e.html
- SIMCom SIM800C product page: https://en.simcom.com/product/SIM800C.html
- TinyGSM GPS usage example discussion: https://community.thinger.io/t/how-to-access-gps-when-using-thingertinygsm/4475
- Additional TinyGSM GPS signature reference: https://ocw.cs.pub.ro/courses/iothings/proiecte/2022/pettracker
