# The Animus safety model

## Philosophy

An LLM agent with hardware access is a new kind of actor: fast, tireless,
occasionally wrong, and — over a network — potentially not the agent you
think it is. Prompt-level rules ("please don't touch pin 13") are not
safety. Animus's position:

> **Anything that must never happen is enforced in firmware,
> below the network boundary, where no token stream can reach.**

The safety layer (`safety.c`) is the only code that touches GPIO. Tools
don't get raw pin access; they get `safety_set_output()` and
`safety_read_pin()`, which check the compiled-in allow-list every time.

## Guarantees in v0.1

| Guarantee | Mechanism |
|---|---|
| Only allow-listed pins can be actuated or read | Compile-time tables in `animus_config.h`; unknown pins are refused with an explanatory message |
| An output can't stay on longer than its limit | Per-output `esp_timer` armed on every ON; fires even if WiFi drops or the client dies |
| Outputs boot into their safe state | `safety_init()` drives every output to its inactive level before networking starts |
| Every actuation/refusal is visible | `animus.audit` log tag on the serial console |
| Browsers can't reach the device via DNS rebinding | `Origin` header validation (only local origins accepted) |
| Optional request authentication | Bearer token checked on every request when configured |
| The agent knows the rules | MCP `instructions` + tool descriptions generated from the real allow-list at boot |

## Known limitations (please read)

- **No TLS on the device (yet).** Traffic on your LAN is plaintext. Treat the
  device as LAN-only, and set a bearer token on any network you don't fully
  control. Do **not** port-forward the device to the internet.
- **Physical-world judgment is yours.** `max_on_ms` limits duration, not
  consequence. Don't wire Animus to anything whose *brief* activation is
  dangerous (heaters, locks, garage doors, vehicles, medical anything).
- **The allow-list protects pins, not semantics.** If you allow-list a relay
  that controls something important, the agent can legitimately switch it.
  Allow-list only what you're comfortable having toggled at a bad moment.
- **One shared token.** The bearer token is a single static secret, not
  per-user auth.

## Roadmap items that harden this further

- **Confirm-to-actuate:** outputs marked `requires_confirm` only switch after
  a physical press of the BOOT button within a timeout — a human in the loop
  that no prompt injection can fake.
- **TLS / proper auth** for use beyond the LAN.
- **Rate limiting** per output (max actuations per minute).
- **Dry-run mode** for testing configs without driving pins.
