# Animus roadmap

## v0.1 — Proof of life (this release)

- [x] MCP Streamable HTTP server on-device (spec rev 2025-11-25)
- [x] Safety layer: compiled-in pin allow-list, `max_on_ms` auto-off, audit log
- [x] Origin validation + optional bearer token
- [x] Built-in tools: `device_info`, `temperature_read`, `gpio_read`, `gpio_set`
- [x] CI builds for esp32s3 and esp32

## v0.2 — Zero-toolchain onboarding

- [ ] Web flasher (ESP Web Tools): flash from the browser, no ESP-IDF install
- [ ] Captive-portal WiFi provisioning (no menuconfig for end users)
- [ ] mDNS: `http://animus-1.local/mcp`
- [ ] Prebuilt release binaries per target

## v0.3 — Declarative hardware

- [ ] `animus.yaml` → `animus_config.h` codegen CLI (the ESPHome moment)
- [ ] Peripheral drivers behind the safety layer: DHT22, BME280, ADC
      (soil moisture), servo (with angle limits), WS2812, PWM dimming
- [ ] First complete recipes: plant waterer, desk-light, thermometer

## v0.4 — Human in the loop

- [ ] `requires_confirm` outputs: physical BOOT-button confirmation
- [ ] Per-output rate limiting
- [ ] Tiny status dashboard served at `/` (state, audit tail)

## v0.5 — Reach the world (no port forwarding, no extra hardware)

- [ ] **Animus Relay**: the device holds a persistent *outbound* WebSocket to
      a tiny serverless relay (e.g. a free-tier Cloudflare Worker) which
      terminates public HTTPS and forwards JSON-RPC down the socket —
      claude.ai custom connectors work with nothing but the ESP32 itself
- [ ] Husarnet integration option (native ESP-IDF P2P VPN component):
      private access to the device from your own machines anywhere,
      end-to-end encrypted, no public exposure at all
- [ ] TLS support (esp_https_server) for advanced setups
- [ ] Adopt the stateless MCP core (spec rev 2026-07-28) as the default —
      single self-contained requests are a perfect fit for microcontrollers
- [ ] Listing in the MCP server registry

## v0.6 — The nervous system (multi-device)

- [ ] ESP-NOW sensor nodes ("limbs"): battery-powered, deep-sleep, report
      readings to the gateway — one Animus exposes a whole swarm over MCP
- [ ] BLE ingest of commercial sensors: passive scanning of Xiaomi MiFlora
      (plant sensor) and LYWSD03MMC (thermometer) advertisements —
      real-world data with zero soldering
- [ ] Gateway tools: `sensors_list`, `sensor_read` (value + freshness/age)
- [ ] Remote actuation on nodes with safety enforced in the *node's own*
      firmware — a compromised gateway still can't exceed a limb's limits
- [ ] Node pairing/provisioning (incl. WiFi-channel sync for ESP-NOW)

## v1.0 — The body shop

- [ ] Recipe gallery with wiring diagrams and one-click configs
- [ ] Docs site
- [ ] Stability promise for the config format
