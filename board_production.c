#include <stdint.h>
#include "pic32c.h"
#include "board.h"

#define LED_MASK (1UL << 20)
#define SAMPLE_GCLK_HZ 48000000U

board_status_t board_status;

static uint32_t raw[2][BOARD_RAW_SAMPLES];
static volatile int32_t ready = -1;
static volatile uint32_t generation[2];
static volatile uint32_t active_dma_buffer;

static int wait_bits(const volatile uint32_t *reg, uint32_t mask, uint32_t expected)
{
    uint32_t timeout = 1000000U;
    while ((*reg & mask) != expected)
        if (--timeout == 0U) return 0;
    return 1;
}

static int clock_init(void)
{
    /* The reset DFLL drives GCLK1 at 48 MHz for sampling. PLL0 output 1
     * drives GCLK0/CPU at 120 MHz: 48 * 25 / 10. */
    GCLK_REGS->GCLK_GENCTRL[1] = GCLK_GENCTRL_SRC_DFLL |
        GCLK_GENCTRL_DIV(1U) | GCLK_GENCTRL_GENEN_Msk;
    if (!wait_bits(&GCLK_REGS->GCLK_SYNCBUSY,
                   GCLK_SYNCBUSY_GENCTRL1_Msk, 0U)) return 6;
    OSCCTRL_REGS->OSCCTRL_PLL0REFDIV = OSCCTRL_PLL0REFDIV_REFDIV(1U);
    OSCCTRL_REGS->OSCCTRL_PLL0FBDIV = OSCCTRL_PLL0FBDIV_FBDIV(25U);
    OSCCTRL_REGS->OSCCTRL_PLL0POSTDIVA =
        OSCCTRL_PLL0POSTDIVA_POSTDIV1(10U) |
        OSCCTRL_PLL0POSTDIVA_OUTEN1_Msk;
    OSCCTRL_REGS->OSCCTRL_PLL0CTRL =
        OSCCTRL_PLL0CTRL_REFSEL_DFLL48M |
        OSCCTRL_PLL0CTRL_BWSEL_BWSEL4 |
        OSCCTRL_PLL0CTRL_ENABLE_Msk;
    if (!wait_bits(&OSCCTRL_REGS->OSCCTRL_INTFLAG,
                   OSCCTRL_INTFLAG_PLL0LOCKR_Msk,
                   OSCCTRL_INTFLAG_PLL0LOCKR_Msk)) return 7;
    FCR_REGS->FCR_CTRLA =
        (FCR_REGS->FCR_CTRLA & ~(FCR_CTRLA_AUTOWS_Msk | FCR_CTRLA_FWS_Msk)) |
        FCR_CTRLA_FWS(5U) | FCR_CTRLA_ADRWS_Msk;
    GCLK_REGS->GCLK_GENCTRL[0] = GCLK_GENCTRL_SRC_PLL0_1 |
        GCLK_GENCTRL_DIV(1U) | GCLK_GENCTRL_GENEN_Msk;
    if (!wait_bits(&GCLK_REGS->GCLK_SYNCBUSY,
                   GCLK_SYNCBUSY_GENCTRL0_Msk, 0U)) return 8;
    SystemCoreClock = 120000000U;
    return 0;
}

static void dma_arm(uint32_t index)
{
    dma_channel_registers_t *ch = &DMA0_REGS->CHANNEL[0];
    ch->DMA_CHDSA = (uint32_t)(uintptr_t)raw[index];
    ch->DMA_CHXSIZ = DMA_CHXSIZ_BLKSZ(sizeof(raw[0])) |
                     DMA_CHXSIZ_CSZ(sizeof(uint32_t));
    ++generation[index];
    active_dma_buffer = index;
    ch->DMA_CHCTRLA = DMA_CHCTRLA_ENABLE_Msk;
}

void DMA0_PRI0_Handler(void)
{
    dma_channel_registers_t *ch = &DMA0_REGS->CHANNEL[0];
    uint32_t flags = ch->DMA_CHINTF;
    ch->DMA_CHINTF = flags;
    if (flags & (DMA_CHINTF_RDE_Msk | DMA_CHINTF_WRE_Msk))
        ++board_status.dma_errors;
    if (flags & DMA_CHINTF_BC_Msk)
    {
        uint32_t finished = active_dma_buffer;
        if (ready >= 0) ++board_status.capture_overruns;
        ready = (int32_t)finished;
        ++board_status.capture_frames;
        dma_arm(finished ^ 1U);
    }
}

static int capture_init(void)
{
    /* Arduino A0 is PA12 / ADC_VINP8, peripheral function B. */
    PORT_REGS->GROUP[0].PORT_DIRCLR = 1UL << 12;
    PORT_REGS->GROUP[0].PORT_PMUX[6] =
        (PORT_REGS->GROUP[0].PORT_PMUX[6] & 0xF0U) | MUX_PA12B_ADC_VINP8;
    PORT_REGS->GROUP[0].PORT_PINCFG[12] = PORT_PINCFG_PMUXEN_Msk;
    GCLK_REGS->GCLK_PCHCTRL[ADC_GCLK_ID] =
        GCLK_PCHCTRL_GEN_GCLK1 | GCLK_PCHCTRL_CHEN_Msk;
    GCLK_REGS->GCLK_PCHCTRL[TCC0_GCLK_ID] =
        GCLK_PCHCTRL_GEN_GCLK1 | GCLK_PCHCTRL_CHEN_Msk;

    ADC_REGS->CONFIG[0].ADC_CALCTRL = *(const uint32_t *)0x0A007184UL;
    ADC_REGS->ADC_CTRLA = ADC_CTRLA_ANAEN_Msk;
    ADC_REGS->ADC_CTRLD = ADC_CTRLD_VREFSEL_AVDD_AVSS |
        ADC_CTRLD_WKUPEXP(12U) | ADC_CTRLD_ANLEN(1U) |
        ADC_CTRLD_CHNEN(1U);
    ADC_REGS->CONFIG[0].ADC_CORCTRL = ADC_CORCTRL_ADCDIV(1U) |
        ADC_CORCTRL_SAMC(16U) | ADC_CORCTRL_SELRES(3U);
    ADC_REGS->CONFIG[0].ADC_CHNCFG5 = ADC_CHNCFG5_TRGSRC8_EVENT_USER0;
    ADC_REGS->CONFIG[0].ADC_EVCTRL = ADC_EVCTRL_STARTEI_Msk;
    ADC_REGS->ADC_PFFCTRL = ADC_PFFCTRL_PFFEN_Msk |
        ADC_PFFCTRL_PFFCR(1U) |
        ADC_PFFCTRL_PFFRDYDM_CTLINTFLAG_PFFRDY;

    EVSYS_REGS->CHANNEL[0].EVSYS_CHANNEL =
        EVSYS_CHANNEL_EVGEN(EVENT_ID_GEN_TCC0_OVF) |
        EVSYS_CHANNEL_PATH_ASYNCHRONOUS;
    EVSYS_REGS->EVSYS_USER[EVENT_ID_USER_ADC_TRIGGERS_0] =
        EVSYS_USER_CHANNEL(1U);

    DMA0_REGS->DMA_CTRLA = DMA_CTRLA_ENABLE_Msk;
    dma_channel_registers_t *ch = &DMA0_REGS->CHANNEL[0];
    ch->DMA_CHCTRLB = DMA_CHCTRLB_RAS_FIXED_WORD_ADDR_INCR |
        DMA_CHCTRLB_WAS_AUTO_ADDR_INCR | DMA_CHCTRLB_PRI_PRI_1 |
        DMA_CHCTRLB_TRIG(ADC_DMAC_ID_PFFRDY);
    ch->DMA_CHSSA = (uint32_t)(uintptr_t)&ADC_REGS->ADC_PFFDATA;
    ch->DMA_CHSSTRD = 0U;
    ch->DMA_CHDSTRD = 0U;
    ch->DMA_CHINTF = DMA_CHINTF_BC_Msk |
        DMA_CHINTF_RDE_Msk | DMA_CHINTF_WRE_Msk;
    ch->DMA_CHINTENSET = DMA_CHINTENSET_BC_Msk;
    NVIC_SetPriority(DMA0_PRI0_IRQn, 2U);
    NVIC_EnableIRQ(DMA0_PRI0_IRQn);
    dma_arm(0U);

    ADC_REGS->ADC_CTRLA |= ADC_CTRLA_ENABLE_Msk;
    if (!wait_bits(&ADC_REGS->ADC_CTLINTFLAG,
                   ADC_CTLINTFLAG_VREFRDY_Msk, ADC_CTLINTFLAG_VREFRDY_Msk)) return 1;
    if (!wait_bits(&ADC_REGS->ADC_CTLINTFLAG,
                   ADC_CTLINTFLAG_CRRDY(1U), ADC_CTLINTFLAG_CRRDY(1U))) return 2;

    TCC0_REGS->TCC_CTRLA = TCC_CTRLA_PRESCALER_DIV1;
    TCC0_REGS->TCC_WAVE = TCC_WAVE_WAVEGEN_NFRQ;
    TCC0_REGS->TCC_PER = SAMPLE_GCLK_HZ / BOARD_RAW_RATE_HZ - 1U;
    if (!wait_bits(&TCC0_REGS->TCC_SYNCBUSY,
                   TCC_SYNCBUSY_PER_Msk | TCC_SYNCBUSY_WAVE_Msk, 0U)) return 3;
    TCC0_REGS->TCC_EVCTRL = TCC_EVCTRL_OVFEO_Msk;
    TCC0_REGS->TCC_CTRLA |= TCC_CTRLA_ENABLE_Msk;
    if (!wait_bits(&TCC0_REGS->TCC_SYNCBUSY,
                   TCC_SYNCBUSY_ENABLE_Msk, 0U)) return 4;
    return 0;
}

int board_init(void)
{
    PORT_REGS->GROUP[3].PORT_OUTSET = LED_MASK;
    PORT_REGS->GROUP[3].PORT_DIRSET = LED_MASK;
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0U;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
    int error = clock_init();
    return error ? error : capture_init();
}

int board_next_frame(board_frame_t *frame)
{
    __disable_irq();
    int32_t index = ready;
    if (index >= 0)
    {
        frame->index = (uint32_t)index;
        frame->generation = generation[index];
        ready = -1;
    }
    __enable_irq();
    return index >= 0;
}

uint16_t board_sample(const board_frame_t *frame, uint32_t index)
{
    uint32_t word = raw[frame->index][index];
    if (((word >> 16) & 0xFU) != 8U) ++board_status.bad_channel_words;
    return (uint16_t)(word & 0x0FFFU);
}

int board_frame_valid(const board_frame_t *frame)
{
    if (generation[frame->index] == frame->generation) return 1;
    ++board_status.capture_overruns;
    return 0;
}

void board_poll(void)
{
    if (ADC_REGS->ADC_CTLINTFLAG & ADC_CTLINTFLAG_PFFOVF_Msk)
    {
        ++board_status.adc_fifo_overflows;
        ADC_REGS->ADC_CTLINTFLAG = ADC_CTLINTFLAG_PFFOVF_Msk;
    }
}

void board_idle(void)
{
    __NOP();
}

void board_result(uint32_t peak_bin, float peak_amplitude_fs)
{
    (void)peak_bin;
    (void)peak_amplitude_fs;
    PORT_REGS->GROUP[3].PORT_OUTTGL = LED_MASK;
}

uint32_t board_cycle_count(void)
{
    return DWT->CYCCNT;
}
