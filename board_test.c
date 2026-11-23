#include <math.h>
#include <stdint.h>
#include "pic32c.h"
#include "board.h"

#define TEST_FRAMES 100U
#define TEST_TONE_HZ 1500U
#define TEST_TONE_BIN (TEST_TONE_HZ * BOARD_FFT_SAMPLES / BOARD_OUTPUT_RATE_HZ)
#define TEST_TONE_PERIOD (BOARD_RAW_RATE_HZ / TEST_TONE_HZ)
#define TEST_TONE_AMPLITUDE 1024.0f
#define TWO_PI 6.2831853071795864769f

board_status_t board_status;

static uint16_t synthetic_frame[BOARD_RAW_SAMPLES];
static uint32_t issued_frames;

int board_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;

    /* 1500 Hz is bin 32 after decimation, and 4096 raw samples contain
     * exactly 32 cycles: repeating the frame has no phase discontinuity. */
    for (uint32_t i = 0; i < BOARD_RAW_SAMPLES; ++i)
    {
        float phase = TWO_PI *
            ((float)(i % TEST_TONE_PERIOD) / TEST_TONE_PERIOD);
        synthetic_frame[i] =
            (uint16_t)(BOARD_ADC_MIDPOINT +
                       TEST_TONE_AMPLITUDE * sinf(phase) + 0.5f);
    }
    return 0;
}

int board_next_frame(board_frame_t *frame)
{
    if (issued_frames >= TEST_FRAMES) return 0;
    frame->index = 0U;
    frame->generation = issued_frames;
    ++issued_frames;
    ++board_status.capture_frames;
    return 1;
}

uint16_t board_sample(const board_frame_t *frame, uint32_t index)
{
    (void)frame;
    return synthetic_frame[index];
}

int board_frame_valid(const board_frame_t *frame)
{
    (void)frame;
    return 1;
}

void board_poll(void) {}

void board_idle(void)
{
    __WFI();
}

void board_result(uint32_t peak_bin, float peak_amplitude_fs)
{
    if (peak_bin == TEST_TONE_BIN &&
        peak_amplitude_fs >= 0.45f && peak_amplitude_fs <= 0.55f)
        ++board_status.test_pass_frames;
    else
        ++board_status.test_fail_frames;
}

uint32_t board_cycle_count(void)
{
    return DWT->CYCCNT;
}
