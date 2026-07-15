# Recipe: plant waterer (coming in v0.3)

Ask your agent: *"check if my plant is thirsty, and if so, water it for 5 seconds."*

Planned bill of materials (~$8):
- ESP32 dev board
- capacitive soil moisture sensor (ADC)
- 5V relay + small pump

The config will look like:

```c
static const animus_output_t ANIMUS_OUTPUTS[] = {
    { .pin = 5, .name = "water_pump", .max_on_ms = 5000, .active_level = 1 },
};
```

`max_on_ms = 5000` means the firmware physically cuts the pump after 5 s,
even if the agent (or anyone else) never sends the OFF command. That is the
whole point of Animus.
