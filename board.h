#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#define BOARD_RAW_RATE_HZ 192000U
#define BOARD_OUTPUT_RATE_HZ 48000U
#define BOARD_RAW_SAMPLES 4096U
#define BOARD_FFT_SAMPLES 1024U
#define BOARD_DECIMATION 4U
#define BOARD_ADC_MIDPOINT 2048.0f

typedef struct
{
    uint32_t index;
    uint32_t generation;
} board_frame_t;

typedef struct
{
    volatile uint32_t capture_frames;
    volatile uint32_t capture_overruns;
    volatile uint32_t dma_errors;
    volatile uint32_t adc_fifo_overflows;
    volatile uint32_t bad_channel_words;
    volatile uint32_t test_pass_frames;
    volatile uint32_t test_fail_frames;
} board_status_t;

extern board_status_t board_status;

/* Error codes: 0 success; other values identify a clock or ADC setup step. */
int board_init(void);
int board_next_frame(board_frame_t *frame);
uint16_t board_sample(const board_frame_t *frame, uint32_t index);
int board_frame_valid(const board_frame_t *frame);
void board_poll(void);
void board_idle(void);
void board_result(uint32_t peak_bin, float peak_amplitude_fs);
uint32_t board_cycle_count(void);

#endif
