// =============================================================================
//  Example — Button controls LED
// =============================================================================
//  Read this file before starting the exercises.
//
//  ── Required pattern (all solutions must follow this) ──────────────────────
//    1. Include <trac_fw_io.hpp>
//    2. Instantiate exactly ONE trac_fw_io_t object at the start of main().
//    3. Keep the object alive for the entire firmware execution.
//    4. The destructor handles graceful teardown of all I/O threads (RAII).
//
//  ── Virtual hardware ───────────────────────────────────────────────────────
//    SW 0   →  io.digital_read(0)     Input button
//    LED 0  →  io.digital_write(0, …) Output LED
//
//  ── Behaviour ──────────────────────────────────────────────────────────────
//    Button pressed (SW 0 = ON)  → LED 0 turns ON
//    Button released (SW 0 = OFF) → LED 0 turns OFF
// =============================================================================

#include <trac_fw_io.hpp>
#include <cstdio>

int main() {
    trac_fw_io_t io;

    bool last_state = false;

    while (true) {
        bool btn = io.digital_read(0);
        io.digital_write(0, btn);

        if (btn != last_state) {
            std::printf("Button %-3s → LED %s\n",
                btn ? "ON" : "OFF",
                btn ? "ON" : "OFF");
            last_state = btn;
        }

        io.delay(10);
    }
}
