# Exercise 03 — I2C Sensors (Bit-bang)

## Required reading

> **You must consult the sensor datasheets before implementing this exercise.**
> The datasheets are located in the `doc/` folder of this repository:
>
> | File | Sensor | Contains |
> |------|--------|----------|
> | `doc/tmp64_datasheet.pdf` | TMP64 (temperature) | I²C address, register map, data encoding, protocol timing |
> | `doc/hum_datasheet.pdf`   | HMD10 (humidity)   | Register map, data encoding, bus scan procedure |
>
> Register addresses, data encoding, I²C timing diagrams, and the bus scan
> procedure are **not repeated here** — they are defined exclusively in the datasheets.

## Context

An industrial monitoring unit must continuously track the **temperature** and
**relative humidity** of a process environment and show both readings live on an
LCD display.

Two sensors share the same I²C bus:

- **TMP64** — a temperature sensor at a known address.
- **HMD10** — a humidity sensor whose I²C address is **not known in advance** and
  must be discovered at runtime by scanning the bus.

Your MCU has **no hardware I²C peripheral**, so you must implement the full I²C
protocol in software (bit-bang) using two general-purpose GPIO ports.

## Hardware connections

```
        ┌──────────────────────────┐
        │           MCU            │
        │                          │
        │  P8 ─────────────────────┼──────── SCL ────┬──────────────────┐
        │  P9 ─────────────────────┼──────── SDA ────┤                  │
        │                          │              ┌───┴──────────┐  ┌───┴──────────┐
        └──────────────────────────┘              │    TMP64      │  │  ??? sensor  │
                                                  │   (0x48)      │  │  (unknown    │
                                                  └───────────────┘  │   address)   │
                                                                     └──────────────┘
```

> Both SDA and SCL are **open-drain** lines.
> Use `digital_write(port, 0)` to pull the line **low** and
> `digital_write(port, 1)` to **release** it.

## Virtual hardware

| Port    | Signal      | API                                              |
|---------|-------------|--------------------------------------------------|
| P8      | SCL         | `io.digital_write(8, …)` / `io.digital_read(8)` |
| P9      | SDA         | `io.digital_write(9, …)` / `io.digital_read(9)` |
| Display | Temperature | `io.write_reg(6, …)` / `io.write_reg(7, …)`     |
| Display | Humidity    | `io.write_reg(4, …)` / `io.write_reg(5, …)`     |

> The virtual board LCD shows two lines:
> **Line 0** = OUT[6–7] (temperature) · **Line 1** = OUT[4–5] (humidity)

## Display format

Temperature (8 characters, e.g. `"  25.000"`):

```cpp
char buf[9] = {};
std::snprintf(buf, sizeof(buf), "%8.3f", temp_c);
uint32_t r6, r7;
std::memcpy(&r6, buf + 0, 4);
std::memcpy(&r7, buf + 4, 4);
io.write_reg(6, r6);
io.write_reg(7, r7);
```

Humidity (8 characters, e.g. `" 60.123%"`):

```cpp
char buf[9] = {};
std::snprintf(buf, sizeof(buf), "%7.3f%%", hum_pct);
uint32_t r4, r5;
std::memcpy(&r4, buf + 0, 4);
std::memcpy(&r5, buf + 4, 4);
io.write_reg(4, r4);
io.write_reg(5, r5);
```

## Acceptance criteria

- [ ] WHO_AM_I of TMP64 reads the expected value and is printed on startup
- [ ] Temperature value is correct
- [ ] Temperature is printed every ~1 s via `printf`
- [ ] Display registers 6–7 updated with each temperature reading (LCD line 0)
- [ ] Bus scan runs once at startup and prints all responding addresses
- [ ] Humidity sensor is identified and its WHO_AM_I is printed
- [ ] Humidity value is printed every ~1 s via `printf`
- [ ] Display registers 4–5 updated with each humidity reading (LCD line 1)
- [ ] No busy-waiting — use `io.millis()` for timing

## How to build

```bash
cmake -S . -B build
cmake --build build --target ex03
./build/ex03
```
