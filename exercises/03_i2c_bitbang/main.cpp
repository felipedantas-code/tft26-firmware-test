// Ex-03: I2C Sensors (Bit-bang)
// =============================================================================
//  Exercise 03 - I2C Sensors (Bit-bang)
// =============================================================================
//
//  Approach: Software I2C master via GPIO bit-bang with open-drain handling.
//
//  The MCU has no hardware I2C peripheral. SDA (P9) and SCL (P8) are
//  open-drain lines: writing 0 pulls the line low, writing 1 releases it
//  (pullup makes it high). The firmware never actively drives either line
//  high, which is critical for multi-device bus operation and would prevent
//  bus contention on real hardware.
//
//  Pullups: the HAL provides set_pullup() to enable internal pullups on GPIO.
//  Without pullups, released open-drain lines float and read as LOW regardless
//  of bus state, causing every address to ACK and every byte to read 0x00.
//  On real hardware, external 4.7k resistors to VDD serve this purpose.
//
//  Two sensors share the bus:
//  - TMP64 at fixed address 0x48 (temperature, milli-Celsius)
//  - HMD10 at unknown address (humidity, milli-percent RH)
//
//  Both sensors have identical register layouts, according to datasheets:
//  - 0x0F: Sensor identification (WHO_AM_I) (1 byte, read-only)
//  - 0x00-0x03: 32-bit measurement, big-endian, sequential read
//
//  The register read protocol follows the standard I2C write-and-read
//  pattern with repeated start, as specified in datasheets:
//  S -> addr+W -> ACK -> reg -> ACK -> Sr -> addr+R -> ACK -> data -> NACK -> P
//
//  Bus scan at startup probes every address in the valid range (0x08-0x77)
//  with a minimal START -> addr+W -> ACK check -> STOP sequence. Any address
//  that ACKs and is not 0x48 (TMP64) is assumed to be the HMD10.
//
//  Main loop is rate-limited to ~1 Hz via io.millis() as required by the
//  acceptance criteria. Both sensors are read in sequence each cycle.
//
//  I2C clock timing: no explicit delay between GPIO transitions. The
//  simulator responds fast enough without artificial delays. On real
//  hardware at 400 kHz, calibrated delays of ~1.25 us per half-period
//  would be inserted between clock edges via timer or NOP loops tuned
//  to the CPU clock frequency.
// =============================================================================

#include <trac_fw_io.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>

static constexpr uint8_t SCL_PORT = 8;
static constexpr uint8_t SDA_PORT = 9;

static constexpr uint8_t TMP64_ADDR    = 0x48;
static constexpr uint8_t TMP64_WHOID   = 0xA5;
static constexpr uint8_t REG_WHO_AM_I  = 0x0F;
static constexpr uint8_t REG_DATA      = 0x00;

static constexpr uint32_t READ_INTERVAL_MS = 1000;  // ~1 Hz sensor reads

// =============================================================================
//  I2C bit-bang primitives
// =============================================================================
//  Open-drain rule: write(1) = release line (pullup brings it high),
//  write(0) = drive line low. We NEVER actively drive high. On real hardware
//  driving SDA or SCL high would fight the pullup and any other device
//  trying to hold the line low, potentially causing bus contention or
//  electrical damage.
// =============================================================================

static trac_fw_io_t* g_io = nullptr;

static void scl_low(){ 
    g_io->digital_write(SCL_PORT, 0); 
}

static void scl_release(){ 
    g_io->digital_write(SCL_PORT, 1); 
}

static void sda_low(){ 
    g_io->digital_write(SDA_PORT, 0); 
}

static void sda_release(){ 
    g_io->digital_write(SDA_PORT, 1); 
}

static bool sda_read(){ 
    return g_io->digital_read(SDA_PORT); 
}

// START condition: SDA is low while SCL is high
static void i2c_start() {
    sda_release();
    scl_release();
    sda_low();      // SDA goes low while SCL is high = START
    scl_low();      // Prepare for first bit
}

// STOP condition: SDA rises while SCL is high
static void i2c_stop() {
    sda_low();      // Make sure SDA is low before releasing SCL
    scl_release();  // SCL goes high first
    sda_release();  // SDA rises while SCL is high = STOP
}

// Repeated START: same as START but without a preceding STOP.
// Used between a write (set register) and register read.
// The bus are previously claimed by the master.
static void i2c_repeated_start() {
    sda_release();
    scl_release();
    sda_low();
    scl_low();
}

// Write one byte MSB first, return true if slave ACKed
static bool i2c_write_byte(uint8_t byte) {
    for (int i = 7; i >= 0; i--) {
        if (byte & (1 << i)){
            sda_release();
        }
        else{
            sda_low();
        }
        scl_release();
        scl_low();
    }

    // 9º clock: release SDA and read ACK from slave
    // Slave pulls SDA low = ACK, SDA stays high = NACK
    sda_release();
    scl_release();
    bool ack = !sda_read();  // ACK = SDA low = true
    scl_low();
    return ack;
}

// Read one byte MSB first. If ack=true, master pulls SDA low (ACK) after
// the byte to request more data. If ack=false, master leaves SDA high
// (NACK) to signal this is the last byte before STOP.
static uint8_t i2c_read_byte(bool ack) {
    uint8_t byte = 0;
    sda_release();  // Release SDA so slave can drive it

    for (int i = 7; i >= 0; i--) {
        scl_release();
        if (sda_read()){
            byte |= (1 << i);
        }
        scl_low();
    }

    // 9º clock: master sends ACK or NACK
    if (ack){
        sda_low();     // ACK: pull SDA low (send more)
    }
    else{
        sda_release(); // NACK: leave SDA high (last byte)
    }
    scl_release();
    scl_low();
    sda_release();     // Release SDA after ACK/NACK cycle
    return byte;
}

// =============================================================================
//  Mid-level I2C helpers
// =============================================================================

// Read N bytes from a register address using write-then-read with repeated start.
// Follows the exact transaction sequence from both datasheets (Figure 1):
// S -> addr+W -> ACK -> reg -> ACK -> Sr -> addr+R -> ACK -> data[0..n-1] -> NACK -> P
static bool i2c_read_register(uint8_t dev_addr, uint8_t reg, uint8_t* buf, int len) {
    
    i2c_start();

    if (!i2c_write_byte((dev_addr << 1) | 0)){ 
        i2c_stop(); 
        return false; 
    }

    if (!i2c_write_byte(reg)){
         i2c_stop(); 
         return false; 
    }

    i2c_repeated_start();

    if (!i2c_write_byte((dev_addr << 1) | 1)){ 
        i2c_stop(); 
        return false; 
    }

    for (int i = 0; i < len; i++){
        buf[i] = i2c_read_byte(i < len - 1);  // ACK all except last
    }
        
    i2c_stop();
    return true;
}

// Probe a single address: START -> addr+W -> check ACK -> STOP
static bool i2c_probe(uint8_t dev_addr) {
    i2c_start();
    bool ack = i2c_write_byte((dev_addr << 1) | 0);
    i2c_stop();
    return ack;
}

// Read WHO_AM_I register (1 byte at 0x0F)
static bool i2c_read_who_am_i(uint8_t dev_addr, uint8_t* id) {
    return i2c_read_register(dev_addr, REG_WHO_AM_I, id, 1);
}

// Read 32-bit measurement from register 0x00 (4 bytes, big-endian int32_t)
static bool i2c_read_measurement(uint8_t dev_addr, int32_t* value) {
    uint8_t buf[4];
    if (!i2c_read_register(dev_addr, REG_DATA, buf, 4))
        return false;

    *value = ((int32_t)buf[0] << 24) | ((int32_t)buf[1] << 16) | ((int32_t)buf[2] << 8)  | ((int32_t)buf[3]);
    return true;
}

// =============================================================================
//  Display helpers
// =============================================================================

static void display_temperature(trac_fw_io_t &io, float temp_c) {
    char buf[9] = {};
    std::snprintf(buf, sizeof(buf), "%8.3f", temp_c);
    uint32_t r6, r7;
    std::memcpy(&r6, buf + 0, 4);
    std::memcpy(&r7, buf + 4, 4);
    io.write_reg(6, r6);
    io.write_reg(7, r7);
}

static void display_humidity(trac_fw_io_t &io, float hum_pct) {
    char buf[9] = {};
    std::snprintf(buf, sizeof(buf), "%7.3f%%", hum_pct);
    uint32_t r4, r5;
    std::memcpy(&r4, buf + 0, 4);
    std::memcpy(&r5, buf + 4, 4);
    io.write_reg(4, r4);
    io.write_reg(5, r5);
}

// =============================================================================
//  Main
// =============================================================================

int main() {
    trac_fw_io_t io;
    g_io = &io;

    // Enable internal pullups on I2C bus lines.
    // Without pullups, open-drain lines float LOW when released, causing
    // every address to appear to ACK and all data to read 0x00.
    // On real hardware, external 4.7k resistors to VDD serve this purpose.
    io.set_pullup(SCL_PORT, true);
    io.set_pullup(SDA_PORT, true);
    scl_release();
    sda_release();
    io.delay(10);  // Let bus settle after pullup enable

    // ---- TMP64: read WHO_AM_I ----
    uint8_t tmp64_id = 0;
    if (i2c_read_who_am_i(TMP64_ADDR, &tmp64_id)) {
        std::printf("TMP64 [0x%02X] WHO_AM_I = 0x%02X %s\n", TMP64_ADDR, tmp64_id, (tmp64_id == TMP64_WHOID) ? "(OK)" : "(UNEXPECTED)");
    } else {
        std::printf("TMP64 [0x%02X] not responding\n", TMP64_ADDR);
    }

    // ---- Bus scan: probe 0x08-0x77 ----
    std::printf("I2C bus scan (0x08-0x77):\n");
    uint8_t hmd_addr = 0;

    for (uint8_t addr = 0x08; addr <= 0x77; addr++) {
        if (i2c_probe(addr)) {
            uint8_t who = 0;
            i2c_read_who_am_i(addr, &who);
            std::printf("  0x%02X: ACK (WHO_AM_I = 0x%02X)", addr, who);

            if (addr == TMP64_ADDR)
                std::printf(" [TMP64]\n");
            else {
                std::printf(" [HMD10]\n");
                hmd_addr = addr;
            }
        }
    }

    if (hmd_addr != 0){
        std::printf("Humidity sensor discovered at 0x%02X\n", hmd_addr);
    } else {
        std::printf("WARNING: humidity sensor not found on bus\n");
    }

    // ---- Main loop: read both sensors at ~1 Hz ----
    // Rate limited via io.millis() to avoid flooding the bus and console.
    // Without this, the loop completes in ~1 ms (observed from simulator)
    // and would produce hundreds of redundant readings per second.
    // On real battery-powered hardware, the MCU would sleep between reads
    // via RTC alarm or RTOS timer.
    uint32_t last_read = 0;

    while (true) {
        uint32_t now = io.millis();
        if ((now - last_read) < READ_INTERVAL_MS) {
            io.delay(1);
            continue;
        }
        last_read = now;

        int32_t temp_raw = 0;
        if (i2c_read_measurement(TMP64_ADDR, &temp_raw)) {
            float temp_c = (float)temp_raw / 1000.0f;
            std::printf("[%u ms] Temperature: %.3f C\n", io.millis(), temp_c);
            display_temperature(io, temp_c);
        } else {
            std::printf("[%u ms] TMP64 read failed\n", io.millis());
        }

        if (hmd_addr != 0) {
            int32_t hum_raw = 0;
            if (i2c_read_measurement(hmd_addr, &hum_raw)) {
                float hum_pct = (float)hum_raw / 1000.0f;
                std::printf("[%u ms] Humidity: %.3f %%RH\n", io.millis(), hum_pct);
                display_humidity(io, hum_pct);
            } else {
                std::printf("[%u ms] HMD10 read failed\n", io.millis());
            }
        }
    }
}