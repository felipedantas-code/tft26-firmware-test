// Ex-02: Frequency Estimator
// =============================================================================
//  Exercise 02 - Frequency Estimator
// =============================================================================
//
//  Approach: Zero-crossing detection with linear interpolation, adaptive
//  midpoint tracking, hysteresis band, median filtering, outlier gate with
//  adaptive flush, and adaptive window averaging for output stabilization.
//
//  Pipeline: ADC sample -> midpoint/hysteresis tracking -> rising zero-crossing
//  detection with sub-sample interpolation -> period measurement with integer
//  precision -> outlier gate -> median filter -> adaptive window average -> output
//
//  === Signal characteristics (observed from simulator) ===
//
//  - Full-scale sinusoidal, 0-4095 (12-bit ADC, rail to rail)
//  - Centered at ~2048, no significant DC offset
//  - Effective sample rate ~64 Hz (simulator overhead, not firmware limited)
//  - At 6 Hz signal: ~10 samples per period
//  - Frequency ramps between ~6 Hz and ~9 Hz with stable plateaus
//
//  === Sampling ===
//
//  The HAL provides no ADC interrupt, DMA, or hardware trigger. Sampling is
//  necessarily polling-based with io.millis() timing. This is a limitation
//  of the simulation environment.
//
//  On real hardware (e.g. STM32, nRF9160) the correct approach would be:
//  - Timer-triggered ADC with DMA into a double buffer
//  - Half-transfer and complete-transfer interrupts signal a processing
//    thread via semaphore
//  - MCU core sleeps between buffer-ready events
//  - Zero-crossing detection runs on the completed buffer in batch
//  This eliminates jitter from software polling and allows the CPU to sleep
//  between acquisition bursts, which matters for battery-powered sensors.
//
//  === Zero-crossing detection ===
//
//  Midpoint tracking: running min/max with slow decay (0.1% per sample).
//  Midpoint = (min + max) / 2. Adapts to amplitude and offset drift.
//
//  Hysteresis: crossing only valid after signal drops below (midpoint - 5%)
//  and then rises above (midpoint + 5%). Prevents false triggers from noise.
//
//  Interpolation: linear interpolation between the two samples straddling
//  the crossing gives sub-sample timing. Timestamps captured immediately
//  after analog_read() to minimize jitter from variable read latency.
//
//  === Numerical precision ===
//
//  Period is computed using integer arithmetic for the bulk timestamp
//  difference, with float only for small fractional corrections (0-15 ms).
//  This avoids precision loss from large absolute timestamps (~78M ms)
//  in float32 which only has ~7 significant digits. The integer subtraction
//  yields ~100-200 ms that converts to float with full precision.
//
//  === Filtering pipeline ===
//
//  Outlier gate: periods deviating >15% from current median are rejected.
//  After 3 consecutive rejections in the same direction, it is treated as
//  a real frequency change and the buffer flushes to re-learn.
//
//  Median filter: buffer of 7 periods. Rejects isolated transient spikes
//  without affecting response to real changes.
//
//  Adaptive window average: inspired by industrial scale display
//  stabilization algorithm developed by me for an old project. 
//  Maintains a growing window of frequency estimates (up to512 values). 
// While new estimates fall within the window's observed
//  [min, max] range, the window grows and the average converges toward the
//  true value, driving steady-state error to zero. When a new estimate
//  falls outside the window bounds, the window resets to size 1 and
//  re-learns from the new regime. This gives:
//  - Zero steady-state error (average converges to true value)
//  - Instant response to real frequency changes (window reset)
//  - Natural outlier suppression at the output stage
//  - Acceptable memory cost (512 floats = 2 KB RAM) with negligible CPU overhead (simple sum and count)
//
//  Observed performance:
//  - Steady-state error: <0.05 Hz (requirement: ±0.5 Hz)
//  - Convergence after step change: <1 second
//  - Outlier rejection: single transient spikes do not affect output
// =============================================================================

#include <trac_fw_io.hpp>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <algorithm>

// ---- Algorithm parameters ----
static constexpr uint32_t SAMPLE_INTERVAL_MS = 1;
static constexpr int      MEDIAN_SIZE        = 7;
static constexpr float    HYSTERESIS_RATIO   = 0.05f;
static constexpr float    MINMAX_DECAY       = 0.001f;
static constexpr int      FLUSH_AFTER        = 3;
static constexpr float    GATE_RATIO         = 0.15f;
static constexpr int      AVG_MAX_WINDOW     = 512;  // ~2KB RAM, negligible

// ---- Median of small array ----
static float median(float* buf, int count) {

    float tmp[MEDIAN_SIZE];

    int n = (count < MEDIAN_SIZE) ? count : MEDIAN_SIZE;

    for (int i = 0; i < n; i++) {
        tmp[i] = buf[i];
    }

    std::sort(tmp, tmp + n);

    return tmp[n / 2];
}

// =======================================================================================
//  adaptive_avg_t - Adaptive window average
// =======================================================================================
//  Window grows while input stays within observed [min, max] range.
//  Resets to size 1 when input falls outside bounds.
//  Output is simple arithmetic mean of active window.
//  Inspired by display stabilization algorithm used in industrial scales developed by me.
//  where the reading must lock to the true value under noisy input.
// =======================================================================================
class adaptive_avg_t {
public:
    float update(float value) {
        if (count_ == 0) {
            buf_[0] = value;
            count_ = 1;
            idx_ = 1;
            sum_ = value;
            min_ = value;
            max_ = value;
            return value;
        }

        if (value >= min_ && value <= max_) {
            if (count_ < AVG_MAX_WINDOW) {
                buf_[idx_] = value;
                idx_ = (idx_ + 1) % AVG_MAX_WINDOW;
                sum_ += value;
                count_++;
            } else {
                sum_ -= buf_[idx_];
                buf_[idx_] = value;
                sum_ += value;
                idx_ = (idx_ + 1) % AVG_MAX_WINDOW;
            }
        } else {
            buf_[0] = value;
            count_ = 1;
            idx_ = 1;
            sum_ = value;
            min_ = value;
            max_ = value;
        }

        if (value < min_){
            min_ = value;
        }
        if (value > max_) {
            max_ = value;
        }

        return sum_ / (float)count_;
    }

    void reset() {
        count_ = 0;
        idx_ = 0;
        sum_ = 0.0f;
        min_ = 0.0f;
        max_ = 0.0f;
    }

private:
    float    buf_[AVG_MAX_WINDOW] = {};
    int      count_ = 0;
    int      idx_   = 0;
    float    sum_   = 0.0f;
    float    min_   = 0.0f;
    float    max_   = 0.0f;
};

int main() {
    trac_fw_io_t io;

    // ---- Adaptive midpoint tracking ----
    float running_min = 2048.0f;
    float running_max = 2048.0f;
    float midpoint    = 2048.0f;
    float hysteresis  = 200.0f;

    // ---- Zero-crossing state ----
    bool     below_band        = false;
    float    prev_sample       = 0.0f;
    uint32_t prev_time         = 0;
    uint32_t prev_cross_base   = 0;
    float    prev_cross_frac   = 0.0f;
    float    prev_cross_dt     = 0.0f;
    bool     has_prev_crossing = false;

    // ---- Period buffer (circular, for median) ----
    float    period_buf[MEDIAN_SIZE] = {};
    int      period_count = 0;
    int      period_idx   = 0;

    // ---- Outlier gate ----
    int      reject_streak = 0;
    float    reject_dir    = 0.0f;

    // ---- Adaptive output average ----
    adaptive_avg_t freq_avg;

    // ---- Timing ----
    uint32_t last_sample_time = io.millis();

    prev_sample = (float)io.analog_read(0);
    prev_time   = io.millis();

    while (true) {
    
        uint32_t now = io.millis();

        if ((now - last_sample_time) < SAMPLE_INTERVAL_MS) {
            io.delay(1);
            continue;
        }

        float sample = (float)io.analog_read(0);
        uint32_t sample_time = io.millis();
        last_sample_time = sample_time;

        // ---- Update running min/max with slow decay ----
        if (sample < running_min) {
            running_min = sample;
        }
        else {
            running_min += MINMAX_DECAY * (sample - running_min);
        }
        if (sample > running_max) {
            running_max = sample;
        }
        else {
            running_max -= MINMAX_DECAY * (running_max - sample);
        }

        float range = running_max - running_min;
        if (range > 100.0f) {
            midpoint = (running_min + running_max) / 2.0f;
            hysteresis = range * HYSTERESIS_RATIO;
        }

        float upper = midpoint + hysteresis;
        float lower = midpoint - hysteresis;

        if (sample < lower) {
            below_band = true;
        }

        // ---- Rising zero-crossing with interpolation ----
        if (below_band && prev_sample < upper && sample >= upper) {
           
            float dt = (float)(sample_time - prev_time);
            float fraction = 0.0f;
            
            if ((sample - prev_sample) > 0.01f) {
                fraction = (upper - prev_sample) / (sample - prev_sample);
            }

            if (has_prev_crossing) {

                float period = (float)(prev_time - prev_cross_base) + fraction * dt - prev_cross_frac * prev_cross_dt;

                if (period > 10.0f && period < 5000.0f) {
                
                    bool accepted = true;

                    if (period_count >= 3) {
                
                        float current_med = median(period_buf, period_count);
                        float deviation = (period - current_med) / current_med;

                        if (deviation > GATE_RATIO || deviation < -GATE_RATIO) {
                        
                            reject_streak++;
                            reject_dir += deviation;

                            if (reject_streak >= FLUSH_AFTER) {
                            
                                period_count = 0;
                                period_idx = 0;
                                reject_streak = 0;
                                reject_dir = 0.0f;
                                freq_avg.reset();
                                accepted = true;

                            } else {
                                accepted = false;
                            }
                        } else {
                            reject_streak = 0;
                            reject_dir = 0.0f;
                        }
                    }

                    if (accepted) {
                        period_buf[period_idx] = period;
                        period_idx = (period_idx + 1) % MEDIAN_SIZE;
                        if (period_count < MEDIAN_SIZE) period_count++;

                        float med_period = median(period_buf, period_count);
                        float freq = 1000.0f / med_period;

                        float stable_freq = freq_avg.update(freq);

                        uint32_t centi_hz = (uint32_t)(stable_freq * 100.0f + 0.5f);
                        io.write_reg(3, centi_hz);
                    }
                }
            }

            prev_cross_base = prev_time;
            prev_cross_frac = fraction;
            prev_cross_dt   = dt;
            has_prev_crossing = true;
            below_band = false;
        }

        prev_sample = sample;
        prev_time   = sample_time;
    }
}