# Challenge 02 — Frequency Estimator

## Context

A process sensor continuously outputs an analog signal on ADC channel 0.
The signal is periodic. Your firmware must measure its frequency and report
the result in real time.

## Virtual hardware

| Peripheral | Function | API |
|---|---|---|
| ADC Ch 0 | Process sensor signal | `io.analog_read(0)` |
| OUT reg 3 | Frequency estimate output | `io.write_reg(3, …)` |

## Output format

Write your estimate to register 3 as **frequency × 100** (integer, centiHz).

Examples:

| Estimated frequency | Value to write |
|---|---|
| 47.00 Hz | `io.write_reg(3, 4700)` |
| 73.50 Hz | `io.write_reg(3, 7350)` |

Update the register whenever your estimate changes.

## Acceptance criteria

- [ ] Estimate within **±0.5 Hz** of the actual frequency during steady state
- [ ] Tracks frequency changes — estimate converges within **1 second** of a change
- [ ] Robust to signal disturbances — a transient does not permanently corrupt the estimate
- [ ] Fixed-rate sampling loop — no unbounded busy-wait on `analog_read()`

## How to build

```bash
cmake -S . -B build
cmake --build build --target ex02
./build/ex02
```
