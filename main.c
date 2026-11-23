#include <math.h>
#include <stdint.h>
#include "arm_math.h"
#include "board.h"
#include "fir_coeffs.h"

#define MAX_PASS_BIN 426U /* floor(20 kHz / 46.875 Hz) */

static float samples[BOARD_RAW_SAMPLES];
static float decimated[BOARD_FFT_SAMPLES];
static float fir_state[BOARD_RAW_SAMPLES + FIR_TAPS];
static float fft_input[BOARD_FFT_SAMPLES];
static float fft_output[BOARD_FFT_SAMPLES];
static float window[BOARD_FFT_SAMPLES];
static float magnitudes[BOARD_FFT_SAMPLES / 2U + 1U];
static arm_fir_decimate_instance_f32 fir;
static arm_rfft_fast_instance_f32 rfft;

/* Common application results, available in both board targets. */
volatile uint32_t fft_frames;
volatile uint32_t setup_error;
volatile uint32_t fft_cycles;
volatile uint32_t fir_cycles;
volatile uint32_t process_attempts;
volatile uint32_t sample_min = 4095U;
volatile uint32_t sample_max;
volatile uint32_t peak_bin;
volatile float peak_hz;
volatile float peak_amplitude_fs;
volatile float dc_offset_fs;
float spectrum[BOARD_FFT_SAMPLES / 2U + 1U];

static void process_frame(const board_frame_t *frame)
{
    uint32_t start = board_cycle_count();
    ++process_attempts;
    uint32_t min_code = 4095U;
    uint32_t max_code = 0U;
    for (uint32_t i = 0; i < BOARD_RAW_SAMPLES; ++i)
    {
        uint32_t code = board_sample(frame, i);
        if (code < min_code) min_code = code;
        if (code > max_code) max_code = code;
        samples[i] = (float)code - BOARD_ADC_MIDPOINT;
    }
    arm_fir_decimate_f32(&fir, samples, decimated, BOARD_RAW_SAMPLES);
    fir_cycles = board_cycle_count() - start;

    float sum = 0.0f;
    for (uint32_t i = 0; i < BOARD_FFT_SAMPLES; ++i)
        sum += decimated[i];
    float dc = sum / BOARD_FFT_SAMPLES;
    for (uint32_t i = 0; i < BOARD_FFT_SAMPLES; ++i)
        fft_input[i] = (decimated[i] - dc) * window[i];
    arm_rfft_fast_f32(&rfft, fft_input, fft_output, 0U);
    magnitudes[0] = fabsf(fft_output[0]);
    arm_cmplx_mag_f32(&fft_output[2], &magnitudes[1],
                      BOARD_FFT_SAMPLES / 2U - 1U);
    magnitudes[BOARD_FFT_SAMPLES / 2U] = fabsf(fft_output[1]);
    fft_cycles = board_cycle_count() - start;

    if (!board_frame_valid(frame)) return;
    const float scale = 2.0f /
        (BOARD_FFT_SAMPLES * BOARD_ADC_MIDPOINT * 0.5f);
    float best = 0.0f;
    uint32_t best_bin = 0U;
    sample_min = min_code;
    sample_max = max_code;
    dc_offset_fs = dc / BOARD_ADC_MIDPOINT;
    for (uint32_t i = 0; i <= BOARD_FFT_SAMPLES / 2U; ++i)
    {
        spectrum[i] = (i == 0U) ? fabsf(dc_offset_fs) :
            magnitudes[i] *
            ((i == BOARD_FFT_SAMPLES / 2U) ? scale * 0.5f : scale);
        if (i <= MAX_PASS_BIN && spectrum[i] > best)
        {
            best = spectrum[i];
            best_bin = i;
        }
    }
    peak_bin = best_bin;
    peak_hz = (float)best_bin *
        ((float)BOARD_OUTPUT_RATE_HZ / BOARD_FFT_SAMPLES);
    peak_amplitude_fs = best;
    ++fft_frames;
    board_result(best_bin, best);
}

int main(void)
{
    if (arm_fir_decimate_init_f32(&fir, FIR_TAPS, BOARD_DECIMATION,
                                  fir_coeffs, fir_state,
                                  BOARD_RAW_SAMPLES) != ARM_MATH_SUCCESS ||
        arm_rfft_fast_init_f32(&rfft,
                               BOARD_FFT_SAMPLES) != ARM_MATH_SUCCESS)
        setup_error = 5U;
    else
    {
        arm_hanning_f32(window, BOARD_FFT_SAMPLES);
        setup_error = (uint32_t)board_init();
    }

    while (1)
    {
        if (setup_error != 0U) continue;
        board_poll();
        board_frame_t frame;
        if (board_next_frame(&frame)) process_frame(&frame);
        else board_idle();
    }
}
