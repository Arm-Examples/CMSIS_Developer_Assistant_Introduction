/*
 * ARMCC startup file for PIC32CK2051GC01144
 *
 * Copyright (c) 2026 Microchip Technology Inc. and its subsidiaries.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 */

#include <stdint.h>
#include <stdbool.h>

#include "pic32ck2051gc01144.h"

// Check if DeviceVectors is defined and create a typedef to VECTOR_TABLE_Type
#ifndef DEVICE_VECTORS_DEFINE
    typedef DeviceVectors VECTOR_TABLE_Type; // Defines the toolchain agnostic type of the vector table type defined in the 'pic32ck2051gc01144.h'

    #define DEVICE_VECTORS_DEFINE
#endif

/*---------------------------------------------------------------------------
  External References
 *---------------------------------------------------------------------------*/
extern uint32_t __INITIAL_SP;
extern uint32_t __STACK_LIMIT;
#if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
extern uint32_t __STACK_SEAL;
#endif

extern __NO_RETURN void __PROGRAM_START(void);

/*---------------------------------------------------------------------------
  Internal References
 *---------------------------------------------------------------------------*/
__NO_RETURN void Reset_Handler  (void);
__NO_RETURN void Default_Handler(void);


/*
 * =============================================================================
 * TCM (Tightly Coupled Memory) Initialization — GCC / IAR / ARMCC
 * =============================================================================
 * Provides TCM_Enable() and TCM_Disable() using the standard CMSIS SCB
 * registers SCB->ITCMCR / SCB->DTCMCR.  These registers are present on all
 * Cortex-M7 devices that have ITCM and/or DTCM.  The guard below uses the
 * CMSIS __ITCM_PRESENT / __DTCM_PRESENT feature macros so this file compiles
 * out completely on cores without TCM.
 *
 * ORDERING:
 *   TCM_Enable() MUST run in Reset_Handler BEFORE the .data copy and .bss
 *   zero, because linker sections (e.g. .text_tcm) may be placed in ITCM or
 *   DTCM and the first write to those regions must be to enabled RAM.
 *
 * CONTROL BITS (written together in TCM_Enable):
 *   EN    — brings the TCM online at its architecturally-defined address.
 *   RMW   — "Read-Modify-Write" for narrower-than-word stores.  With RMW set,
 *           byte and half-word writes go through the hardware RMW path so
 *           they behave like normal byte/half-word accesses; without RMW,
 *           narrower stores cannot be performed on ECC-protected TCM because
 *           the syndrome covers the whole word.
 *   RETEN — "Retention".  Holds TCM contents (and their ECC syndromes, where
 *           applicable) across low-power / sleep transitions instead of
 *           powering the RAM off.  Without RETEN, the TCM wakes up
 *           uninitialised and would need to be re-seeded.
 *
 * User override:
 *   Define __STARTUP_TCM_DISABLE on the compiler command line to skip the
 *   TCM_Enable() call in Reset_Handler entirely (e.g. for bootloaders that
 *   must not claim the TCM).
 *
 * MISRA C:2012: the write to SCB->ITCMCR / SCB->DTCMCR combines bit masks
 * from the CMSIS core header; the left-hand side is volatile so the
 * assignment is an I/O access in the MISRA sense.
 */

#if (__ITCM_PRESENT == 1) || (__DTCM_PRESENT == 1)

__STATIC_INLINE void TCM_Enable(void)
{
    __DSB();
    __ISB();
#if (__ITCM_PRESENT == 1)
    SCB->ITCMCR = (SCB_ITCMCR_EN_Msk | SCB_ITCMCR_RMW_Msk | SCB_ITCMCR_RETEN_Msk);
#endif
#if (__DTCM_PRESENT == 1)
    SCB->DTCMCR = (SCB_DTCMCR_EN_Msk | SCB_DTCMCR_RMW_Msk | SCB_DTCMCR_RETEN_Msk);
#endif
    __DSB();
    __ISB();
}

__STATIC_INLINE void TCM_Disable(void)
{
    __DSB();
    __ISB();
#if (__ITCM_PRESENT == 1)
    SCB->ITCMCR &= ~(uint32_t)SCB_ITCMCR_EN_Msk;
#endif
#if (__DTCM_PRESENT == 1)
    SCB->DTCMCR &= ~(uint32_t)SCB_DTCMCR_EN_Msk;
#endif
    __DSB();
    __ISB();
}

#endif /* (__ITCM_PRESENT == 1) || (__DTCM_PRESENT == 1) */

/*
 * =============================================================================
 * CMCC (Cortex-M Cache Controller) Configuration
 * =============================================================================
 * Configures the hardware cache controller present on a number of Cortex-M
 * devices where a fixed-size on-chip SRAM is shared between the cache and
 * the TCM: the larger the TCM, the smaller the cache, and vice-versa.  This
 * file picks the cache size that matches the TCM length the linker has been
 * told to reserve.
 *
 * ALGORITHM (per CMCC instance):
 *   1. If CMCC_SR.CSTS is already set, the cache is enabled — leave the
 *      current configuration in place (this short-circuits warm-reset paths
 *      where the first-boot code has already configured the cache).  On
 *      dual-CMCC devices each instance is wrapped in its own `do { ... }
 *      while (0)` block so that a warm CMCC0 does not skip CMCC1.
 *   2. Clear CMCC_CTRL.CEN to place the cache in a known disabled state; a
 *      disabled cache is required before CMCC_CFG may be reprogrammed.
 *   3. Select a cache size via CMCC_CFG.CSIZESW, derived at compile time
 *      from __STARTUP_<tcm>_LENGTH (the linker script exposes this as 0
 *      when no TCM is reserved, so by default the full SRAM becomes cache).
 *   4. Re-enable the cache by writing CMCC_CTRL.CEN, unless the TCM length
 *      consumes the entire SRAM (in which case the cache is intentionally
 *      left disabled).
 *
 * LENGTH MACROS (from the linker script, default 0):
 *   __STARTUP_TCM_LENGTH    — single-CMCC devices
 *   __STARTUP_ITCM_LENGTH   — dual-CMCC devices (CMCC0 side, ITCM)
 *   __STARTUP_DTCM_LENGTH   — dual-CMCC devices (CMCC1 side, DTCM)
 * Override these on the compiler command line or in the linker script to
 * split the SRAM between cache and TCM.
 *
 * CALL SITE:
 *   CMCC_Configure() is called from SystemInit(), which runs after the
 *   core-level initialisation in Reset_Handler.  The cache is therefore
 *   available from the start of main().
 *
 * REGISTER-LEVEL COMPATIBILITY:
 *   CMCC v6 names the config bit-field CMCC_CFG_CSIZESW; CMCC v7 renames
 *   the same field to CMCC_CFG_PRGCSIZE.  The shim below aliases the v7
 *   macro onto the v6 name so the rest of the file uses one form.
 *
 * MISRA C:2012:
 *   - Rule 11.3 / 11.6: pointers to the CMCC register block come from the
 *     device header (e.g. CMCC_REGS) and are treated here as opaque MMIO.
 *   - Rule 15.5 (single point of exit): the per-instance `do { ... } while
 *     (0)` block uses `break` (not `return`) to short-circuit the
 *     "cache already on, leave it alone" case, so that on dual-CMCC devices
 *     the second instance is still configured when the first is already on.
 */

/* Compatibility shim: CMCC v6 uses CMCC_CFG_CSIZESW, v7 uses CMCC_CFG_PRGCSIZE */
#if !defined(CMCC_CFG_CSIZESW) && defined(CMCC_CFG_PRGCSIZE)
#define CMCC_CFG_CSIZESW CMCC_CFG_PRGCSIZE
#endif

#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_1KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_1KB_Val _UINT32_(0x0)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_2KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_2KB_Val _UINT32_(0x1)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_4KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_4KB_Val _UINT32_(0x2)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_8KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_8KB_Val _UINT32_(0x3)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_16KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_16KB_Val _UINT32_(0x4)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_32KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_32KB_Val _UINT32_(0x5)
#endif
#if !defined(CMCC_CFG_CSIZESW_CONF_CSIZE_64KB_Val)
#define   CMCC_CFG_CSIZESW_CONF_CSIZE_64KB_Val _UINT32_(0x6)
#endif

__STATIC_INLINE void CMCC_Configure(void)
{

#if defined(CMCC_REGS) && defined(CMCC_SR_CSTS_Msk)
    /* Per-instance `do { ... } while (0)` so that a warm cache on one CMCC
     * instance does not short-circuit configuration of any sibling instance.
     * MISRA-C:2012 Rule 15.5 deviation: `break` is used as a single early
     * exit to keep the body flat instead of nesting the remainder in an
     * else-branch. */
    do {
    /* If the cache is already enabled (warm reset / pre-configured by a
     * bootloader), leave the existing configuration untouched. */
    if (CMCC_REGS->CMCC_SR & CMCC_SR_CSTS_Msk) {
        break;
    }

    /* Disable the cache so that CMCC_CFG may be reprogrammed.  Per the CMCC
     * DOS, CMCC_CFG is read-only while the cache is enabled. */
    CMCC_REGS->CMCC_CTRL &= ~(CMCC_CTRL_CEN_Msk);

    /* Select the cache size based on the TCM length the linker script has
     * reserved.  The chain below expands to:
     *
     *     #if   __STARTUP_<tcm>_LENGTH == <size_0>  -> cache = MAX - <size_0>
     *     #elif __STARTUP_<tcm>_LENGTH == <size_1>  -> cache = MAX - <size_1>
     *     ...
     *     #elif __STARTUP_<tcm>_LENGTH == <size_N>  -> (all SRAM used as TCM;
     *                                                   no cache configured,
     *                                                   CEN remains cleared)
     *     #else                                     -> __STARTUP_<tcm>_LENGTH
     *                                                   is 0, undefined, or
     *                                                   not one of the listed
     *                                                   sizes -> default to
     *                                                   the maximum cache. */
#if (__STARTUP_TCM_LENGTH == 0x800)
    CMCC_REGS->CMCC_CFG = CMCC_CFG_CSIZESW(CMCC_CFG_CSIZESW_CONF_CSIZE_2KB_Val);
    CMCC_REGS->CMCC_CTRL |= CMCC_CTRL_CEN_Msk;
#elif (__STARTUP_TCM_LENGTH == 0xC00)
    CMCC_REGS->CMCC_CFG = CMCC_CFG_CSIZESW(CMCC_CFG_CSIZESW_CONF_CSIZE_1KB_Val);
    CMCC_REGS->CMCC_CTRL |= CMCC_CTRL_CEN_Msk;
#elif (__STARTUP_TCM_LENGTH == 0x1000)
    /* TCM consumes all of the shared SRAM -- no room for cache, leave CEN cleared. */
#else
    /* __STARTUP_TCM_LENGTH is 0, undefined, or not one of the listed
     * sizes -- default to the maximum cache size (no TCM reserved). */
    CMCC_REGS->CMCC_CFG = CMCC_CFG_CSIZESW(CMCC_CFG_CSIZESW_CONF_CSIZE_4KB_Val);
    CMCC_REGS->CMCC_CTRL |= CMCC_CTRL_CEN_Msk;
#endif
    } while (0);

#else /* CMCC_REGS */
    #warning "Failed to configure CMCC for this device"
#endif /* CMCC_REGS */

}

/*---------------------------------------------------------------------------
  Exception / Interrupt Handler
 *---------------------------------------------------------------------------*/

/* Cortex-M33 core handlers */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
void NonMaskableInt_Handler         (void) __attribute__ ((weak, alias("Default_Handler"))); /* Legacy handler name */
void NMI_Handler                    (void);
void MemoryManagement_Handler       (void) __attribute__ ((weak, alias("Default_Handler"))); /* Legacy handler name */
void MemManage_Handler              (void);
void SVCall_Handler                 (void) __attribute__ ((weak, alias("Default_Handler"))); /* Legacy handler name */
void SVC_Handler                    (void);
void DebugMonitor_Handler           (void) __attribute__ ((weak, alias("Default_Handler"))); /* Legacy handler name */
void DebugMon_Handler               (void);

/* Trampoline functions for backward compatibility */
void __attribute__ ((weak)) NMI_Handler(void) {
    NonMaskableInt_Handler();
}
void __attribute__ ((weak)) MemManage_Handler(void) {
    MemoryManagement_Handler();
}
void __attribute__ ((weak)) SVC_Handler(void) {
    SVCall_Handler();
}
void __attribute__ ((weak)) DebugMon_Handler(void) {
    DebugMonitor_Handler();
}
#pragma GCC diagnostic pop /* -Wdeprecated-declarations */

void HardFault_Handler              (void) __attribute__ ((weak, alias("Default_Handler")));
void BusFault_Handler               (void) __attribute__ ((weak, alias("Default_Handler")));
void UsageFault_Handler             (void) __attribute__ ((weak, alias("Default_Handler")));
void SecureFaultMonitor_Handler     (void) __attribute__ ((weak, alias("Default_Handler")));
void PendSV_Handler                 (void) __attribute__ ((weak, alias("Default_Handler")));
void SysTick_Handler                (void) __attribute__ ((weak, alias("Default_Handler")));

/* Peripherals handlers */
void FCR_ERR_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void FCR_FLT_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void FCW_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PM_Handler                     ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SUPC_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void OSCCTRL_XOSC_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void OSCCTRL_DFLL_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void OSCCTRL_PLL0_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void OSC32KCTRL_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void MCLK_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void FREQM_Handler                  ( void ) __attribute__ ((weak, alias("Default_Handler")));
void WDT_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void RTC_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_0_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_1_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_2_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_3_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_4_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_5_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_6_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_7_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_8_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_9_Handler           ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_10_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_11_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_12_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_13_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_14_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_EXTINT_15_Handler          ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EIC_NSCHK_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PAC_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TRAM_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PORT_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DMA0_PRI0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DMA0_PRI1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DMA0_PRI2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DMA1_PRI0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void DMA1_PRI1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void HMATRIX_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EVSYS_0_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EVSYS_1_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EVSYS_2_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EVSYS_3_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void EVSYS_OTHER_Handler            ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM0_56_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM0_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM0_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM0_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM0_3_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM0_4_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM1_56_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM1_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM1_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM1_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM1_3_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM1_4_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM2_56_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM2_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM2_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM2_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM2_3_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM2_4_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM3_56_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM3_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM3_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM3_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM3_3_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM3_4_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC2_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC3_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC4_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC0_MC5_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_MC2_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_MC3_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_MC4_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC1_MC5_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC2_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC2_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC2_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC2_MC2_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC2_MC3_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC2_MC4_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC2_MC5_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC3_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC3_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC3_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC3_MC2_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC3_MC3_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC3_MC4_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC3_MC5_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM4_56_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM4_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM4_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM4_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM4_3_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM4_4_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM5_56_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM5_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM5_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM5_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM5_3_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM5_4_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM6_56_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM6_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM6_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM6_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM6_3_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM6_4_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM7_56_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM7_0_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM7_1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM7_2_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM7_3_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SERCOM7_4_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC4_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC4_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC4_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC5_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC5_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC5_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC6_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC6_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC6_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC7_OTHER_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC7_MC0_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TCC7_MC1_Handler               ( void ) __attribute__ ((weak, alias("Default_Handler")));
void ADC_GLOBAL_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void ADC_CORE1_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void AC_Handler                     ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PTC_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SPI_IXS_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PCC_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void PDEC_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void CAN0_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void CAN1_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void ETH_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SQI_Handler                    ( void ) __attribute__ ((weak, alias("Default_Handler")));
void TRNG_Handler                   ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SDMMC0_Handler                 ( void ) __attribute__ ((weak, alias("Default_Handler")));
void SDMMC1_Handler                 ( void ) __attribute__ ((weak, alias("Default_Handler")));
void USB_OTHER_Handler              ( void ) __attribute__ ((weak, alias("Default_Handler")));
void USB_SOF_Handler                ( void ) __attribute__ ((weak, alias("Default_Handler")));
void USB_TRCPT0_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void USB_TRCPT1_Handler             ( void ) __attribute__ ((weak, alias("Default_Handler")));
void USBHS_Handler                  ( void ) __attribute__ ((weak, alias("Default_Handler")));

/*----------------------------------------------------------------------------
  Exception / Interrupt Vector table
 *----------------------------------------------------------------------------*/

#if defined ( __GNUC__ )
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#endif

extern const VECTOR_TABLE_Type __VECTOR_TABLE;
const VECTOR_TABLE_Type __VECTOR_TABLE __VECTOR_TABLE_ATTRIBUTE = {
        .pvStack = (void*) (&__INITIAL_SP),       /*     Initial Stack Pointer */



        .pfnReset_Handler              = (void*) Reset_Handler,
        .pfnNMI_Handler                = (void*) NMI_Handler,
        .pfnHardFault_Handler          = (void*) HardFault_Handler,
        .pfnMemManage_Handler          = (void*) MemManage_Handler,
        .pfnBusFault_Handler           = (void*) BusFault_Handler,
        .pfnUsageFault_Handler         = (void*) UsageFault_Handler,
        .pfnSecureFaultMonitor_Handler = (void*) SecureFaultMonitor_Handler,
        .pvReservedC8                  = (void*) (0UL), /* Reserved */
        .pvReservedC7                  = (void*) (0UL), /* Reserved */
        .pvReservedC6                  = (void*) (0UL), /* Reserved */
        .pfnSVC_Handler                = (void*) SVC_Handler,
        .pfnDebugMon_Handler           = (void*) DebugMon_Handler,
        .pvReservedC3                  = (void*) (0UL), /* Reserved */
        .pfnPendSV_Handler             = (void*) PendSV_Handler,
        .pfnSysTick_Handler            = (void*) SysTick_Handler,

        /* Configurable interrupts */
        .pfnFCR_ERR_Handler            = (void*) FCR_ERR_Handler, /* 0  Flash Read Controller (FCR) */
        .pfnFCR_FLT_Handler            = (void*) FCR_FLT_Handler, /* 1  Flash Read Controller (FCR) */
        .pfnFCW_Handler                = (void*) FCW_Handler,    /* 2  Flash Write Control (FCW) */
        .pfnPM_Handler                 = (void*) PM_Handler,     /* 3  Power Manager */
        .pfnSUPC_Handler               = (void*) SUPC_Handler,   /* 4  Supply Controller */
        .pfnOSCCTRL_XOSC_Handler       = (void*) OSCCTRL_XOSC_Handler, /* 5  Oscillators Control */
        .pfnOSCCTRL_DFLL_Handler       = (void*) OSCCTRL_DFLL_Handler, /* 6  Oscillators Control */
        .pfnOSCCTRL_PLL0_Handler       = (void*) OSCCTRL_PLL0_Handler, /* 7  Oscillators Control */
        .pfnOSC32KCTRL_Handler         = (void*) OSC32KCTRL_Handler, /* 8  32kHz Oscillators Control */
        .pfnMCLK_Handler               = (void*) MCLK_Handler,   /* 9  Main Clock */
        .pfnFREQM_Handler              = (void*) FREQM_Handler,  /* 10 Frequency Meter */
        .pfnWDT_Handler                = (void*) WDT_Handler,    /* 11 Watchdog Timer */
        .pfnRTC_Handler                = (void*) RTC_Handler,    /* 12 Real-Time Counter */
        .pfnEIC_EXTINT_0_Handler       = (void*) EIC_EXTINT_0_Handler, /* 13 External Interrupt Controller */
        .pfnEIC_EXTINT_1_Handler       = (void*) EIC_EXTINT_1_Handler, /* 14 External Interrupt Controller */
        .pfnEIC_EXTINT_2_Handler       = (void*) EIC_EXTINT_2_Handler, /* 15 External Interrupt Controller */
        .pfnEIC_EXTINT_3_Handler       = (void*) EIC_EXTINT_3_Handler, /* 16 External Interrupt Controller */
        .pfnEIC_EXTINT_4_Handler       = (void*) EIC_EXTINT_4_Handler, /* 17 External Interrupt Controller */
        .pfnEIC_EXTINT_5_Handler       = (void*) EIC_EXTINT_5_Handler, /* 18 External Interrupt Controller */
        .pfnEIC_EXTINT_6_Handler       = (void*) EIC_EXTINT_6_Handler, /* 19 External Interrupt Controller */
        .pfnEIC_EXTINT_7_Handler       = (void*) EIC_EXTINT_7_Handler, /* 20 External Interrupt Controller */
        .pfnEIC_EXTINT_8_Handler       = (void*) EIC_EXTINT_8_Handler, /* 21 External Interrupt Controller */
        .pfnEIC_EXTINT_9_Handler       = (void*) EIC_EXTINT_9_Handler, /* 22 External Interrupt Controller */
        .pfnEIC_EXTINT_10_Handler      = (void*) EIC_EXTINT_10_Handler, /* 23 External Interrupt Controller */
        .pfnEIC_EXTINT_11_Handler      = (void*) EIC_EXTINT_11_Handler, /* 24 External Interrupt Controller */
        .pfnEIC_EXTINT_12_Handler      = (void*) EIC_EXTINT_12_Handler, /* 25 External Interrupt Controller */
        .pfnEIC_EXTINT_13_Handler      = (void*) EIC_EXTINT_13_Handler, /* 26 External Interrupt Controller */
        .pfnEIC_EXTINT_14_Handler      = (void*) EIC_EXTINT_14_Handler, /* 27 External Interrupt Controller */
        .pfnEIC_EXTINT_15_Handler      = (void*) EIC_EXTINT_15_Handler, /* 28 External Interrupt Controller */
        .pfnEIC_NSCHK_Handler          = (void*) EIC_NSCHK_Handler, /* 29 External Interrupt Controller */
        .pfnPAC_Handler                = (void*) PAC_Handler,    /* 30 Peripheral Access Controller */
        .pfnTRAM_Handler               = (void*) TRAM_Handler,   /* 31 TrustRAM */
        .pfnPORT_Handler               = (void*) PORT_Handler,   /* 32 Port Module */
        .pfnDMA0_PRI0_Handler          = (void*) DMA0_PRI0_Handler, /* 33 Direct Memory Access Controller */
        .pfnDMA0_PRI1_Handler          = (void*) DMA0_PRI1_Handler, /* 34 Direct Memory Access Controller */
        .pfnDMA0_PRI2_Handler          = (void*) DMA0_PRI2_Handler, /* 35 Direct Memory Access Controller */
        .pfnDMA1_PRI0_Handler          = (void*) DMA1_PRI0_Handler, /* 36 Direct Memory Access Controller */
        .pfnDMA1_PRI1_Handler          = (void*) DMA1_PRI1_Handler, /* 37 Direct Memory Access Controller */
        .pfnHMATRIX_Handler            = (void*) HMATRIX_Handler, /* 38 HSB Matrix */
        .pfnEVSYS_0_Handler            = (void*) EVSYS_0_Handler, /* 39 Event System Interface */
        .pfnEVSYS_1_Handler            = (void*) EVSYS_1_Handler, /* 40 Event System Interface */
        .pfnEVSYS_2_Handler            = (void*) EVSYS_2_Handler, /* 41 Event System Interface */
        .pfnEVSYS_3_Handler            = (void*) EVSYS_3_Handler, /* 42 Event System Interface */
        .pfnEVSYS_OTHER_Handler        = (void*) EVSYS_OTHER_Handler, /* 43 Event System Interface */
        .pfnSERCOM0_56_Handler         = (void*) SERCOM0_56_Handler, /* 44 Serial Communication Interface */
        .pfnSERCOM0_0_Handler          = (void*) SERCOM0_0_Handler, /* 45 Serial Communication Interface */
        .pfnSERCOM0_1_Handler          = (void*) SERCOM0_1_Handler, /* 46 Serial Communication Interface */
        .pfnSERCOM0_2_Handler          = (void*) SERCOM0_2_Handler, /* 47 Serial Communication Interface */
        .pfnSERCOM0_3_Handler          = (void*) SERCOM0_3_Handler, /* 48 Serial Communication Interface */
        .pfnSERCOM0_4_Handler          = (void*) SERCOM0_4_Handler, /* 49 Serial Communication Interface */
        .pfnSERCOM1_56_Handler         = (void*) SERCOM1_56_Handler, /* 50 Serial Communication Interface */
        .pfnSERCOM1_0_Handler          = (void*) SERCOM1_0_Handler, /* 51 Serial Communication Interface */
        .pfnSERCOM1_1_Handler          = (void*) SERCOM1_1_Handler, /* 52 Serial Communication Interface */
        .pfnSERCOM1_2_Handler          = (void*) SERCOM1_2_Handler, /* 53 Serial Communication Interface */
        .pfnSERCOM1_3_Handler          = (void*) SERCOM1_3_Handler, /* 54 Serial Communication Interface */
        .pfnSERCOM1_4_Handler          = (void*) SERCOM1_4_Handler, /* 55 Serial Communication Interface */
        .pfnSERCOM2_56_Handler         = (void*) SERCOM2_56_Handler, /* 56 Serial Communication Interface */
        .pfnSERCOM2_0_Handler          = (void*) SERCOM2_0_Handler, /* 57 Serial Communication Interface */
        .pfnSERCOM2_1_Handler          = (void*) SERCOM2_1_Handler, /* 58 Serial Communication Interface */
        .pfnSERCOM2_2_Handler          = (void*) SERCOM2_2_Handler, /* 59 Serial Communication Interface */
        .pfnSERCOM2_3_Handler          = (void*) SERCOM2_3_Handler, /* 60 Serial Communication Interface */
        .pfnSERCOM2_4_Handler          = (void*) SERCOM2_4_Handler, /* 61 Serial Communication Interface */
        .pfnSERCOM3_56_Handler         = (void*) SERCOM3_56_Handler, /* 62 Serial Communication Interface */
        .pfnSERCOM3_0_Handler          = (void*) SERCOM3_0_Handler, /* 63 Serial Communication Interface */
        .pfnSERCOM3_1_Handler          = (void*) SERCOM3_1_Handler, /* 64 Serial Communication Interface */
        .pfnSERCOM3_2_Handler          = (void*) SERCOM3_2_Handler, /* 65 Serial Communication Interface */
        .pfnSERCOM3_3_Handler          = (void*) SERCOM3_3_Handler, /* 66 Serial Communication Interface */
        .pfnSERCOM3_4_Handler          = (void*) SERCOM3_4_Handler, /* 67 Serial Communication Interface */
        .pfnTCC0_OTHER_Handler         = (void*) TCC0_OTHER_Handler, /* 68 Timer Counter for Control Applications */
        .pfnTCC0_MC0_Handler           = (void*) TCC0_MC0_Handler, /* 69 Timer Counter for Control Applications */
        .pfnTCC0_MC1_Handler           = (void*) TCC0_MC1_Handler, /* 70 Timer Counter for Control Applications */
        .pfnTCC0_MC2_Handler           = (void*) TCC0_MC2_Handler, /* 71 Timer Counter for Control Applications */
        .pfnTCC0_MC3_Handler           = (void*) TCC0_MC3_Handler, /* 72 Timer Counter for Control Applications */
        .pfnTCC0_MC4_Handler           = (void*) TCC0_MC4_Handler, /* 73 Timer Counter for Control Applications */
        .pfnTCC0_MC5_Handler           = (void*) TCC0_MC5_Handler, /* 74 Timer Counter for Control Applications */
        .pfnTCC1_OTHER_Handler         = (void*) TCC1_OTHER_Handler, /* 75 Timer Counter for Control Applications */
        .pfnTCC1_MC0_Handler           = (void*) TCC1_MC0_Handler, /* 76 Timer Counter for Control Applications */
        .pfnTCC1_MC1_Handler           = (void*) TCC1_MC1_Handler, /* 77 Timer Counter for Control Applications */
        .pfnTCC1_MC2_Handler           = (void*) TCC1_MC2_Handler, /* 78 Timer Counter for Control Applications */
        .pfnTCC1_MC3_Handler           = (void*) TCC1_MC3_Handler, /* 79 Timer Counter for Control Applications */
        .pfnTCC1_MC4_Handler           = (void*) TCC1_MC4_Handler, /* 80 Timer Counter for Control Applications */
        .pfnTCC1_MC5_Handler           = (void*) TCC1_MC5_Handler, /* 81 Timer Counter for Control Applications */
        .pfnTCC2_OTHER_Handler         = (void*) TCC2_OTHER_Handler, /* 82 Timer Counter for Control Applications */
        .pfnTCC2_MC0_Handler           = (void*) TCC2_MC0_Handler, /* 83 Timer Counter for Control Applications */
        .pfnTCC2_MC1_Handler           = (void*) TCC2_MC1_Handler, /* 84 Timer Counter for Control Applications */
        .pfnTCC2_MC2_Handler           = (void*) TCC2_MC2_Handler, /* 85 Timer Counter for Control Applications */
        .pfnTCC2_MC3_Handler           = (void*) TCC2_MC3_Handler, /* 86 Timer Counter for Control Applications */
        .pfnTCC2_MC4_Handler           = (void*) TCC2_MC4_Handler, /* 87 Timer Counter for Control Applications */
        .pfnTCC2_MC5_Handler           = (void*) TCC2_MC5_Handler, /* 88 Timer Counter for Control Applications */
        .pfnTCC3_OTHER_Handler         = (void*) TCC3_OTHER_Handler, /* 89 Timer Counter for Control Applications */
        .pfnTCC3_MC0_Handler           = (void*) TCC3_MC0_Handler, /* 90 Timer Counter for Control Applications */
        .pfnTCC3_MC1_Handler           = (void*) TCC3_MC1_Handler, /* 91 Timer Counter for Control Applications */
        .pfnTCC3_MC2_Handler           = (void*) TCC3_MC2_Handler, /* 92 Timer Counter for Control Applications */
        .pfnTCC3_MC3_Handler           = (void*) TCC3_MC3_Handler, /* 93 Timer Counter for Control Applications */
        .pfnTCC3_MC4_Handler           = (void*) TCC3_MC4_Handler, /* 94 Timer Counter for Control Applications */
        .pfnTCC3_MC5_Handler           = (void*) TCC3_MC5_Handler, /* 95 Timer Counter for Control Applications */
        .pfnSERCOM4_56_Handler         = (void*) SERCOM4_56_Handler, /* 96 Serial Communication Interface */
        .pfnSERCOM4_0_Handler          = (void*) SERCOM4_0_Handler, /* 97 Serial Communication Interface */
        .pfnSERCOM4_1_Handler          = (void*) SERCOM4_1_Handler, /* 98 Serial Communication Interface */
        .pfnSERCOM4_2_Handler          = (void*) SERCOM4_2_Handler, /* 99 Serial Communication Interface */
        .pfnSERCOM4_3_Handler          = (void*) SERCOM4_3_Handler, /* 100 Serial Communication Interface */
        .pfnSERCOM4_4_Handler          = (void*) SERCOM4_4_Handler, /* 101 Serial Communication Interface */
        .pfnSERCOM5_56_Handler         = (void*) SERCOM5_56_Handler, /* 102 Serial Communication Interface */
        .pfnSERCOM5_0_Handler          = (void*) SERCOM5_0_Handler, /* 103 Serial Communication Interface */
        .pfnSERCOM5_1_Handler          = (void*) SERCOM5_1_Handler, /* 104 Serial Communication Interface */
        .pfnSERCOM5_2_Handler          = (void*) SERCOM5_2_Handler, /* 105 Serial Communication Interface */
        .pfnSERCOM5_3_Handler          = (void*) SERCOM5_3_Handler, /* 106 Serial Communication Interface */
        .pfnSERCOM5_4_Handler          = (void*) SERCOM5_4_Handler, /* 107 Serial Communication Interface */
        .pfnSERCOM6_56_Handler         = (void*) SERCOM6_56_Handler, /* 108 Serial Communication Interface */
        .pfnSERCOM6_0_Handler          = (void*) SERCOM6_0_Handler, /* 109 Serial Communication Interface */
        .pfnSERCOM6_1_Handler          = (void*) SERCOM6_1_Handler, /* 110 Serial Communication Interface */
        .pfnSERCOM6_2_Handler          = (void*) SERCOM6_2_Handler, /* 111 Serial Communication Interface */
        .pfnSERCOM6_3_Handler          = (void*) SERCOM6_3_Handler, /* 112 Serial Communication Interface */
        .pfnSERCOM6_4_Handler          = (void*) SERCOM6_4_Handler, /* 113 Serial Communication Interface */
        .pfnSERCOM7_56_Handler         = (void*) SERCOM7_56_Handler, /* 114 Serial Communication Interface */
        .pfnSERCOM7_0_Handler          = (void*) SERCOM7_0_Handler, /* 115 Serial Communication Interface */
        .pfnSERCOM7_1_Handler          = (void*) SERCOM7_1_Handler, /* 116 Serial Communication Interface */
        .pfnSERCOM7_2_Handler          = (void*) SERCOM7_2_Handler, /* 117 Serial Communication Interface */
        .pfnSERCOM7_3_Handler          = (void*) SERCOM7_3_Handler, /* 118 Serial Communication Interface */
        .pfnSERCOM7_4_Handler          = (void*) SERCOM7_4_Handler, /* 119 Serial Communication Interface */
        .pfnTCC4_OTHER_Handler         = (void*) TCC4_OTHER_Handler, /* 120 Timer Counter for Control Applications */
        .pfnTCC4_MC0_Handler           = (void*) TCC4_MC0_Handler, /* 121 Timer Counter for Control Applications */
        .pfnTCC4_MC1_Handler           = (void*) TCC4_MC1_Handler, /* 122 Timer Counter for Control Applications */
        .pfnTCC5_OTHER_Handler         = (void*) TCC5_OTHER_Handler, /* 123 Timer Counter for Control Applications */
        .pfnTCC5_MC0_Handler           = (void*) TCC5_MC0_Handler, /* 124 Timer Counter for Control Applications */
        .pfnTCC5_MC1_Handler           = (void*) TCC5_MC1_Handler, /* 125 Timer Counter for Control Applications */
        .pfnTCC6_OTHER_Handler         = (void*) TCC6_OTHER_Handler, /* 126 Timer Counter for Control Applications */
        .pfnTCC6_MC0_Handler           = (void*) TCC6_MC0_Handler, /* 127 Timer Counter for Control Applications */
        .pfnTCC6_MC1_Handler           = (void*) TCC6_MC1_Handler, /* 128 Timer Counter for Control Applications */
        .pfnTCC7_OTHER_Handler         = (void*) TCC7_OTHER_Handler, /* 129 Timer Counter for Control Applications */
        .pfnTCC7_MC0_Handler           = (void*) TCC7_MC0_Handler, /* 130 Timer Counter for Control Applications */
        .pfnTCC7_MC1_Handler           = (void*) TCC7_MC1_Handler, /* 131 Timer Counter for Control Applications */
        .pfnADC_GLOBAL_Handler         = (void*) ADC_GLOBAL_Handler, /* 132 ADC Controller */
        .pfnADC_CORE1_Handler          = (void*) ADC_CORE1_Handler, /* 133 ADC Controller */
        .pvReserved134                 = (void*) (0UL),          /* 134 Reserved */
        .pvReserved135                 = (void*) (0UL),          /* 135 Reserved */
        .pvReserved136                 = (void*) (0UL),          /* 136 Reserved */
        .pvReserved137                 = (void*) (0UL),          /* 137 Reserved */
        .pfnAC_Handler                 = (void*) AC_Handler,     /* 138 Analog Comparator Controller */
        .pfnPTC_Handler                = (void*) PTC_Handler,    /* 139 Peripheral Touch Controller */
        .pfnSPI_IXS_Handler            = (void*) SPI_IXS_Handler, /* 140 Audio SPI with 8 channels compatibilty */
        .pfnPCC_Handler                = (void*) PCC_Handler,    /* 141 Parallel Capture Controller */
        .pfnPDEC_Handler               = (void*) PDEC_Handler,   /* 142 Position Decoder */
        .pfnCAN0_Handler               = (void*) CAN0_Handler,   /* 143 Control Area Network */
        .pfnCAN1_Handler               = (void*) CAN1_Handler,   /* 144 Control Area Network */
        .pfnETH_Handler                = (void*) ETH_Handler,    /* 145 Ethernet Controller */
        .pfnSQI_Handler                = (void*) SQI_Handler,    /* 146 Polaris Serial Quad Interface SIB */
        .pfnTRNG_Handler               = (void*) TRNG_Handler,   /* 147 True Random Generator */
        .pfnSDMMC0_Handler             = (void*) SDMMC0_Handler, /* 148 SD/MMC Host Controller */
        .pfnSDMMC1_Handler             = (void*) SDMMC1_Handler, /* 149 SD/MMC Host Controller */
        .pfnUSB_OTHER_Handler          = (void*) USB_OTHER_Handler, /* 150 Full-Speed Universal Serial Bus */
        .pfnUSB_SOF_Handler            = (void*) USB_SOF_Handler, /* 151 Full-Speed Universal Serial Bus */
        .pfnUSB_TRCPT0_Handler         = (void*) USB_TRCPT0_Handler, /* 152 Full-Speed Universal Serial Bus */
        .pfnUSB_TRCPT1_Handler         = (void*) USB_TRCPT1_Handler, /* 153 Full-Speed Universal Serial Bus */
        .pfnUSBHS_Handler              = (void*) USBHS_Handler   /* 154 USB High Speed */
};

#if defined ( __GNUC__ )
#pragma GCC diagnostic pop
#endif

/*----------------------------------------------------------------------------
  Reset Handler called on controller reset
 *----------------------------------------------------------------------------*/
__NO_RETURN void Reset_Handler(void)
{
    __set_PSP((uint32_t)(&__INITIAL_SP));

    __set_MSPLIM((uint32_t)(&__STACK_LIMIT));
    __set_PSPLIM((uint32_t)(&__STACK_LIMIT));

    #if defined (__ARM_FEATURE_CMSE) && (__ARM_FEATURE_CMSE == 3U)
        __TZ_set_STACKSEAL_S((uint32_t *)(&__STACK_SEAL));
    #endif

#if (__ITCM_PRESENT == 1) || (__DTCM_PRESENT == 1)
#if !defined(__STARTUP_TCM_DISABLE)
    TCM_Enable();
#endif
#endif

#if defined(TCMECC_REGS) && defined(TCMECC_CR_ENABLE_Msk)
    TCMECC_Initialize();
    FLEXRAM_EccInitialize();
#elif defined(DRMTCM_REGS)
    DRMTCM_RamInitialize();
#elif defined(MCRAMC_REGS) || defined(MCRAMC)
    MCRAMC_EccInitialize();
#endif

    #if !defined(DISABLE_CMSIS_INIT)
        SystemInit();          /* CMSIS System Initialization */
    #endif /* DISABLE_CMSIS_INIT */

    /* Branch to main function */
    __PROGRAM_START();      /* Enter PreMain (C library entry point) */
}

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
  #pragma clang diagnostic push
  #pragma clang diagnostic ignored "-Wmissing-noreturn"
#endif

/*----------------------------------------------------------------------------
  Default Handler for Exceptions / Interrupts
 *----------------------------------------------------------------------------*/
__NO_RETURN void Default_Handler(void)
{
    while (true) {}
}

#if defined(__ARMCC_VERSION) && (__ARMCC_VERSION >= 6010050)
  #pragma clang diagnostic pop
#endif
