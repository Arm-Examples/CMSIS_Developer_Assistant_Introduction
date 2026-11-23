/* Previous working LED-only application. Excluded from the CMSIS build. */
#include <stdint.h>
#include "pic32c.h"

#define LED0_MASK (1UL << 20)

static volatile uint32_t milliseconds;

void SysTick_Handler(void)
{
    ++milliseconds;
}

int main(void)
{
    PORT_REGS->GROUP[3].PORT_OUTSET = LED0_MASK;
    PORT_REGS->GROUP[3].PORT_DIRSET = LED0_MASK;
    SystemCoreClockUpdate();
    if (SysTick_Config(SystemCoreClock / 1000U) != 0U)
        while (1) {}
    uint32_t last_toggle = milliseconds;
    while (1)
    {
        uint32_t now = milliseconds;
        if ((uint32_t)(now - last_toggle) >= 500U)
        {
            last_toggle = now;
            PORT_REGS->GROUP[3].PORT_OUTTGL = LED0_MASK;
        }
    }
}
