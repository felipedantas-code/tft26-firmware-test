// Ex-01: Parts Counter
// =============================================================================
//  Exercise 01 - Parts Counter
// =============================================================================
//
//  Approach: Interrupt-driven detection with suppression-based debounce.
//
//  The ISR disables its own interrupt on the very first edge, making all
//  subsequent bounce invisible to firmware. Main thread blocks on a binary
//  semaphore (zero CPU while idle) and wakes only when the ISR signals.
//  After wake, a fixed debounce delay lets the contact stabilize, then a
//  single pin read determines the real state.
//
//  This is the same pattern used in battery-powered industrial sensors where
//  the MCU sleeps between events via WFI or equivalent. The HAL runs callbacks
//  in a separate OS thread, so we use a lightweight binary semaphore built on
//  C++ mutex + condition_variable as the blocking primitive. Functionally
//  identical to k_sem in Zephyr or xSemaphore in FreeRTOS.
//
//  Sensor polarity: configured via hardware jumper on port 1.
//  In real products, sensor type (NPN/PNP) is a known installation parameter.
//  A physical jumper lets the field technician configure it during setup
//  without reflashing firmware or using a configuration tool.
//
//    Jumper open   (LOW)  -> PNP: HIGH when part present (most common)
//    Jumper closed (HIGH) -> NPN: LOW when part present (normally closed)
//
//  This avoids the fragile approach of auto-detecting idle state at boot,
//  which would break if a part happens to be on the sensor during power-up.
//
//  Debounce window: 40 ms fixed delay after interrupt suppression.
//  Calibrated from simulator observations: entry bounce spans 15-30 ms
//  with individual pulses of 1-20 ms, exit bounce spans up to 35 ms.
//  40 ms covers both with margin. Since the interrupt is disabled during
//  this window, no bounce reaches the firmware regardless of pattern.
//
//  Missed edge recovery: after re-arming the interrupt, we check if the
//  sensor is already in a state that should have triggered an edge. If so,
//  we self-signal the semaphore to avoid blocking forever. This handles
//  fast consecutive parts where the entry edge of part N+1 arrives while
//  we are still processing the exit of part N.
//
//  Display: updated only on actual count change.
//
//  Simulator observations:
//  - Bounce is non-deterministic: sometimes heavy (10+ transitions per
//    event), sometimes clean (single toggle pair per part).
//  - The simulator may not generate pin transitions for the very first part
//    if it is already in transit when the firmware connects. This is a
//    simulation environment limitation, not a firmware issue. In real
//    hardware the sensor is active before the conveyor starts, so this
//    startup race condition does not exist in production.
//  - Idle state is LOW, presence is HIGH (PNP behavior with default config).
//  - For detailed signal analysis and simulator observations,
//    see ANALYSIS.md in this exercise folder.
// =============================================================================

#include <trac_fw_io.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <condition_variable>

static constexpr uint32_t DEBOUNCE_MS = 40;   // Calibrated from simulator bounce data
static constexpr uint8_t  SENSOR_PORT = 0;    // SW 0: inductive sensor
static constexpr uint8_t  CONFIG_PORT = 1;    // SW 1: polarity config jumper

// =============================================================================
//  bin_sem_t - Binary semaphore
// =============================================================================
//  Minimal binary semaphore on top of C++ std primitives.
//  Same behavior as k_sem_init(&sem, 0, 1) in Zephyr or
//  xSemaphoreCreateBinary() in FreeRTOS.
//  take() blocks the calling thread until give() is called from another.
//  Multiple give() before take() result in a single wake (binary, not counting).
// =============================================================================
class bin_sem_t {
public:
    void take() {
        std::unique_lock<std::mutex> lk(mtx_);
        cv_.wait(lk, [this] { return flag_; });
        flag_ = false;
    }

    void give() {
        {
            std::lock_guard<std::mutex> lk(mtx_);
            flag_ = true;
        }
        cv_.notify_one();
    }

private:
    std::mutex              mtx_;
    std::condition_variable cv_;
    bool                    flag_ = false;
};

static bin_sem_t sensor_sem;

// ---- Display helper ----
// Packs count as right-aligned 8-char ASCII into LCD registers 6 and 7.
static void display_count(trac_fw_io_t &io, uint32_t count) {
    char buf[9] = {};
    std::snprintf(buf, sizeof(buf), "%8u", count);
    uint32_t r6, r7;
    std::memcpy(&r6, buf + 0, 4);
    std::memcpy(&r7, buf + 4, 4);
    io.write_reg(6, r6);
    io.write_reg(7, r7);
}

// ---- ISR callback ----
// Disables interrupt to suppress bounce, then signals main thread.
static void sensor_isr(trac_fw_io_t &io) {
    io.detach_interrupt(SENSOR_PORT);
    sensor_sem.give();
}

// ---- Arm sensor interrupt ----
static void arm_sensor(trac_fw_io_t &io) {
    io.attach_interrupt(SENSOR_PORT, [&io]() {
        sensor_isr(io);
    }, InterruptMode::CHANGE);
}

int main() {
    trac_fw_io_t io;

    // ---- Read polarity jumper ----
    // Read once at boot. Sensor type is a hardware installation parameter
    // that does not change at runtime. Reboot after jumper change is
    // standard procedure in industrial equipment.
    bool npn_mode = io.digital_read(CONFIG_PORT);
    bool idle_state = npn_mode ? true : false;
    // NPN: sensor pulls LOW on detect, idle is HIGH
    // PNP: sensor pulls HIGH on detect, idle is LOW

    std::printf("Boot: jumper=%s mode=%s idle=%s\n",
                npn_mode ? "CLOSED" : "OPEN",
                npn_mode ? "NPN" : "PNP",
                idle_state ? "HIGH" : "LOW");

    uint32_t count = 0;
    bool armed = true;

    display_count(io, count);
    arm_sensor(io);

    while (true) {

        // Block until sensor edge. Zero CPU usage while waiting.
        sensor_sem.take();

        // Interrupt is off at this point (disabled by ISR callback).
        // Fixed delay to let bounce settle. Nothing to do in the meantime
        // and no bounce can reach us with interrupt disabled.
        io.delay(DEBOUNCE_MS);

        bool sensor = io.digital_read(SENSOR_PORT);

        if (armed && sensor != idle_state) {
            // Part entered detection zone
            count++;
            armed = false;
            display_count(io, count);
        }
        else if (!armed && sensor == idle_state) {
            // Part exited, ready for next
            armed = true;
        }

        // Re-enable interrupt
        arm_sensor(io);

        // If sensor already changed while interrupt was off, no new edge
        // will fire. Self-signal to process it on next iteration.
        io.delay(5);
        bool check = io.digital_read(SENSOR_PORT);
        if ((armed && check != idle_state) || (!armed && check == idle_state)) {
            sensor_sem.give();
        }
    }
}