# Exercise 01 — Parts Counter

## Context

On a production line, an inductive sensor detects parts passing on a conveyor belt.
The firmware must count each part that passes and show the running total on the display.

> Think of this challenge as if you were implementing the solution in the real world,
> with hardware in hand — pay attention to the details that matter in practice.

## Virtual hardware

| Peripheral | Function | API |
|---|---|---|
| SW 0 | Inductive sensor input | `io.digital_read(0)` |
| Display | Debug — registers 6 and 7 | `io.write_reg(6, …)` / `io.write_reg(7, …)` |

## LCD display format

Registers 6 and 7 are each a `uint32_t` (4 bytes). The LCD interprets them as
**8 ASCII characters** — 4 from register 6, 4 from register 7. Example:

```cpp
char buf[9] = {};
std::snprintf(buf, sizeof(buf), "%8u", count);  // right-aligned, 8 chars
uint32_t r6, r7;
std::memcpy(&r6, buf + 0, 4);
std::memcpy(&r7, buf + 4, 4);
io.write_reg(6, r6);
io.write_reg(7, r7);
```

## Expected behaviour

- Each part that passes the sensor → counter increments **exactly once**
- Display shows the updated count in real time

## Acceptance criteria

- [ ] Part passes → counter increments **exactly once**
- [ ] No duplicate or missed counts
- [ ] Display updated on every count change
- [ ] No aggressive busy-waiting

## How to build

```bash
cmake -S . -B build
cmake --build build --target ex01
./build/ex01
```
