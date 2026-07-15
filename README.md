# Animus

**Give your AI agent a body.**

Animus turns a $5 ESP32 into a native [MCP](https://modelcontextprotocol.io) server. No bridge, no Raspberry Pi, no Home Assistant, no cloud — the microcontroller **is** the server. Flash it, join your WiFi, and Claude (or any MCP client) can read sensors and switch real-world things — within hard limits compiled into the firmware.

[![Build firmware](https://github.com/OWNER/animus/actions/workflows/build.yml/badge.svg)](https://github.com/OWNER/animus/actions/workflows/build.yml)
[![License: MIT](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)
![MCP](https://img.shields.io/badge/MCP-2025--11--25-6E56CF)
![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.5-E7352C)

<!-- TODO: demo GIF — "Claude, water my plant" -> pump runs 5 s -> firmware cuts it -->

```
You:    Is my plant thirsty? Water it if so.
Claude: [gpio_read soil_sensor] → dry
        [gpio_set water_pump ON]
Device: OK: output 'water_pump' (pin 5) is now ON.
        Firmware safety limit: auto-OFF after 5.0 s.
```

## Why Animus

Every "LLM controls hardware" demo so far has the same two flaws:

1. **A middleman.** Your laptop or a Pi runs a Python/Node MCP server that talks serial to the chip. Animus removes the middleman: MCP over Streamable HTTP runs directly on the ESP32.
2. **No guardrails.** A raw `gpio_write(pin, value)` tool means one hallucinated pin number can fry a board — or leave a pump running all night. Animus is built the opposite way: **safety is the architecture, not a prompt.**

## Safety by architecture

The safety layer is the only code path that touches GPIO, and its rules are **compiled into the firmware** — nothing an LLM sends over the network can change them at runtime:

- **Pin allow-list.** Only pins you name in `animus_config.h` exist. Everything else is refused with a clear message the agent can relay.
- **Auto-off (`max_on_ms`).** A hardware timer forces every output back to its safe state after a per-output time limit — even if the OFF command never arrives, the WiFi drops, or the client crashes mid-conversation.
- **Audit log.** Every actuation, refusal, and auto-off is logged under the `animus.audit` tag.
- **DNS-rebinding protection.** Non-local `Origin` headers are rejected, per the MCP Streamable HTTP spec.
- **Optional bearer token.** Set one in `menuconfig` and every request must carry `Authorization: Bearer <token>`.
- **Safe boot.** All outputs initialize to their inactive level before the network comes up.

The model is told all of this in the MCP `instructions` field and in tool descriptions that list your *actual* pins — so the agent knows exactly (and only) what exists.

## Quickstart (~5 minutes after ESP-IDF is installed)

Requires [ESP-IDF v5.5](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/) and any ESP32 board (ESP32, S2, S3, C3...).

```bash
git clone https://github.com/OWNER/animus && cd animus/firmware
idf.py set-target esp32s3          # or esp32, esp32c3, ...
idf.py menuconfig                  # Animus Configuration → WiFi SSID/password
idf.py flash monitor
```

The serial monitor prints your endpoint:

```
I (5324) animus:  MCP endpoint : http://192.168.1.50/mcp
```

Smoke-test it:

```bash
curl -s http://192.168.1.50/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list"}'
```

Connect Claude Code:

```bash
claude mcp add --transport http animus http://192.168.1.50/mcp
```

Then just ask: *"What tools does my animus device have? Blink the demo LED."*
More clients (Claude Desktop, MCP Inspector) in [docs/connect-claude.md](docs/connect-claude.md).

## Built-in tools (v0.1)

| Tool | What it does |
|---|---|
| `device_info` | Identity, IP, uptime, free heap, safety config summary |
| `temperature_read` | Chip's internal die temperature (where supported) |
| `gpio_read` | Read an allow-listed input or output pin |
| `gpio_set` | Switch an allow-listed output ON/OFF, auto-off enforced |

Adding your own hardware = editing two small tables in [`firmware/main/animus_config.h`](firmware/main/animus_config.h) and reflashing.

## How it compares

| | One-off ESP32+MCP demos | Home Assistant bridge | **Animus** |
|---|---|---|---|
| Runs MCP on the chip itself | sometimes | no | **yes** |
| Extra hardware needed | usually a PC | HA server | **none** |
| Firmware-enforced pin limits | no | n/a | **yes** |
| Auto-off dead-man timers | no | automations | **built-in** |
| Made to be forked & configured | no | — | **yes** |

## Roadmap

Web-based flashing (no toolchain needed), captive-portal WiFi setup, YAML → config codegen, more peripherals (DHT22, BME280, servo, WS2812, ADC/soil), a physical confirm-to-actuate button, and first-class support for the stateless MCP core landing in revision 2026-07-28 — embedded devices are exactly why stateless matters. Details in [ROADMAP.md](ROADMAP.md).

## Contributing

PRs welcome — but read [CONTRIBUTING.md](CONTRIBUTING.md) first: anything that weakens the safety layer gets rejected no matter how cool it is.

## License

[MIT](LICENSE) © 2026 The Animus Authors
