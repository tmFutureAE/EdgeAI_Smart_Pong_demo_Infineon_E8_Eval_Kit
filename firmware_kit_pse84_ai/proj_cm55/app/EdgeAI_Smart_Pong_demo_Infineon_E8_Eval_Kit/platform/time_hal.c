#include "platform/time_hal.h"

#include "cy_pdl.h"

void time_hal_init(void)
{
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0u;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

uint32_t time_hal_cycles(void)
{
    return DWT->CYCCNT;
}

uint32_t time_hal_cycles_to_us(uint32_t cycles)
{
    uint32_t hz = SystemCoreClock;
    if (hz == 0u) return 0u;
    return (uint32_t)(((uint64_t)cycles * 1000000ull) / (uint64_t)hz);
}

uint32_t time_hal_elapsed_us(uint32_t start_cycles)
{
    uint32_t now = DWT->CYCCNT;
    return time_hal_cycles_to_us(now - start_cycles);
}

void time_hal_delay_us(uint32_t us)
{
    if (us == 0u) return;
    Cy_SysLib_DelayUs(us);
}
