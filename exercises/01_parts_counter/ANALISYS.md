# Exercise 01 - Parts Counter: Signal Analysis and Simulator Observations

## Context

During development, the counter exhibited a persistent off-by-one error and
occasional missed counts that could not be explained by debounce logic or
firmware timing. After verifying the counting logic was correct through
multiple iterations with debug instrumentation, I suspected the issue was
upstream of the firmware: in the simulated sensor signal itself.

## Investigation method

To isolate the problem, I wrote a minimal firmware that only logs raw pin
transitions with timestamps and no processing logic at all:
```cpp
while (true) {
    bool now_val = io.digital_read(0);
    if (now_val != last) {
        std::printf("[%u ms] %s -> %s\n", io.millis(), ...);
        last = now_val;
    }
}
```

I then recorded the screen showing both the simulator UI (conveyor belt with
visible parts) and the terminal log simultaneously, so I could correlate
physical part passage with electrical events frame by frame.

## Findings

### 1. Non-deterministic bounce (expected)

The simulated sensor produces realistic bounce patterns on both entry and exit
edges. Individual bounce pulses range from 1 to 30 ms, with total bounce
envelopes of 20-60 ms per transition. This is consistent with real inductive
sensor behavior in industrial environments and was handled in firmware with a
40 ms suppression window after interrupt disable.

Bounce sample from log (entry of a single part):
```
[76299304] LOW->HIGH   (first edge)
[76299312] HIGH->LOW   (+8 ms bounce)
[76299313] LOW->HIGH   (+1 ms bounce)
[76299319] HIGH->LOW   (+6 ms bounce)
[76299331] LOW->HIGH   (+12 ms bounce)
[76299350] HIGH->LOW   (+19 ms bounce)
[76299366] LOW->HIGH   (+16 ms, stabilizes HIGH = part present)
```

### 2. Non-deterministic event loss (unexpected, simulator bug)

Through video analysis, I observed that some parts physically traverse the
sensor detection zone (the sensor icon visually activates in the UI) but
produce zero pin transitions in the firmware. The pin remains LOW throughout
the entire passage as if the part did not exist.

This behavior is non-deterministic: the same part position on the belt
sometimes generates a full bounce sequence and sometimes generates nothing.
Across multiple test runs:

- Most parts generate the expected entry bounce + stable HIGH + exit bounce
- Approximately 1 in 10-20 parts generates no electrical event at all
- The first part after firmware connection is particularly prone to being lost

### 3. Startup race condition

The first pin transition consistently arrives 20-25 seconds after boot,
regardless of when the first part reaches the sensor. In video recordings,
the first part visibly passes the sensor during this window without generating
any transition. This suggests a timing dependency between the firmware
process connecting to the simulator and the simulator beginning to relay
GPIO events.

## Probable root cause

The simulator runs as a separate process communicating with the firmware via
IPC (the HAL library connects to localhost:8080). The event loss pattern is
consistent with a race condition in the event relay pipeline between the
simulator's physics engine and the GPIO state exposed to the firmware process.
Possible mechanisms:

- Event queue overflow or drop under high-frequency bounce generation
- Thread synchronization gap where a state change in the physics simulation
  is not propagated to the GPIO register before the next physics tick
  overwrites it
- On startup specifically: the firmware connects and begins reading GPIO
  before the simulator has finished initializing event forwarding, creating
  a window where physical events are silently discarded

## Impact on firmware design

This simulator limitation does not affect the firmware design for real
hardware, where the sensor is physically wired to a GPIO and every electrical
transition is captured by the interrupt controller with nanosecond precision.

The firmware correctly counts every event that reaches the pin. It cannot
compensate for events that never arrive, and attempting to do so (e.g.
inferring missed parts from timing patterns) would be fragile speculation
that would cause false counts in production.

## Evidence

Screen recordings showing simultaneous simulator UI and terminal log are
available upon request, demonstrating parts passing the sensor with no
corresponding pin transitions.