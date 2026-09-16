/*
 * MAX32650 Global Control Register
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Register offsets and RST0 bit positions below are taken directly from
 * this part's own CMSIS gcr_regs.h (mxc_gcr_regs_t / MXC_F_GCR_*).
 * Notably, MAX32650's GCR has no ECC/GPR
 * registers and its RST0 has no TRNG bit at all (TRNG is only clock-gated
 * via PCLK_DIS1 on this part, never reset through GCR) -- this model omits
 * both rather than carrying over unrelated bits from another part's GCR.
 */
#ifndef HW_MAX32650_GCR_H
#define HW_MAX32650_GCR_H

#include "hw/sysbus.h"
#include "qom/object.h"

#define TYPE_MAX32650_GCR "max32650-gcr"
OBJECT_DECLARE_SIMPLE_TYPE(Max32650GcrState, MAX32650_GCR)

/* Register offsets, relative to the GCR base address (0x40000000) */
#define GCR_SCON        0x00
#define GCR_RST0        0x04
#define GCR_CLK_CTRL    0x08
#define GCR_PMR         0x0c
#define GCR_PCLK_DIV    0x18
#define GCR_PCLK_DIS0   0x24
#define GCR_MEM_CLK     0x28
#define GCR_MEM_ZERO    0x2c
#define GCR_SYS_STAT    0x40
#define GCR_RST1        0x44
#define GCR_PCLK_DIS1   0x48
#define GCR_EVENT_EN    0x4c
#define GCR_REV         0x50
#define GCR_SYS_STAT_IE 0x54

/* RST0 (MXC_F_GCR_RST0_*) */
#define RST0_SYS     (1 << 31)
#define RST0_PERIPH  (1 << 30)
#define RST0_SOFT    (1 << 29)
#define RST0_UART2   (1 << 28)
#define RST0_UART0   (1 << 11)
#define RST0_UART1   (1 << 12)

/* CLK_CTRL (MXC_F_GCR_CLK_CTRL_*) */
#define CLK_CTRL_SYSOSC_RDY (1 << 13)

/* MEM_ZERO (MXC_F_GCR_MEM_ZERO_*): SRAM0Z..SRAM6Z occupy bits 0-6. This
 * model doesn't track MAX32650's real per-bank SRAM sizing (not needed by
 * any project targeted so far), so any of those bits zeroes the whole
 * mapped SRAM region rather than a specific bank.
 */
#define MEM_ZERO_SRAM_MASK 0x7f

struct Max32650GcrState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t scon;
    uint32_t rst0;
    uint32_t clk_ctrl;
    uint32_t pmr;
    uint32_t pclk_div;
    uint32_t pclk_dis0;
    uint32_t mem_clk;
    uint32_t mem_zero;
    uint32_t sys_stat;
    uint32_t rst1;
    uint32_t pclk_dis1;
    uint32_t event_en;
    uint32_t rev;
    uint32_t sys_stat_ie;

    MemoryRegion *sram;
    AddressSpace sram_as;

    DeviceState *uart0;
    DeviceState *uart1;
    DeviceState *uart2;
};

#endif
