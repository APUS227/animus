# Contributing to Animus

Thanks for helping give AI agents a body — safely.

- **Safety first.** Any PR that lets network input bypass the allow-list,
  the auto-off timers or the audit log will be rejected, no matter how cool.
- Keep the firmware dependency-free (ESP-IDF built-ins only) unless discussed
  in an issue first.
- New peripherals belong behind the safety layer: add a config entry type,
  wire it through `safety.c`, expose it as a tool in `tools.c`.
- Run `idf.py build` for both `esp32s3` and `esp32` before opening a PR
  (CI checks both).
- One feature per PR. Include a short demo (serial log or GIF) when it
  touches hardware behaviour.

Good first issues are labeled `good-first-issue`.
