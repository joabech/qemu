/*
 * MAX32650 Global Control Register
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/irq.h"
#include "system/runstate.h"
#include "migration/vmstate.h"
#include "hw/qdev-properties.h"
#include "hw/char/max32650_uart.h"
#include "hw/misc/max32650_gcr.h"

static void max32650_gcr_reset_hold(Object *obj, ResetType type)
{
    Max32650GcrState *s = MAX32650_GCR(obj);

    s->scon = 0x21002;
    s->rst0 = 0;
    /* All clocks are always ready */
    s->clk_ctrl = 0x3e140008;
    s->pmr = 0x3f000;
    s->pclk_div = 0;
    s->pclk_dis0 = 0xffffffff;
    s->mem_clk = 0x5;
    s->mem_zero = 0;
    s->sys_stat = 0;
    s->rst1 = 0;
    s->pclk_dis1 = 0xffffffff;
    s->event_en = 0;
    s->rev = 0xa1;
    s->sys_stat_ie = 0;
}

static uint64_t max32650_gcr_read(void *opaque, hwaddr addr,
                                   unsigned int size)
{
    Max32650GcrState *s = opaque;

    switch (addr) {
    case GCR_SCON:
        return s->scon;

    case GCR_RST0:
        return s->rst0;

    case GCR_CLK_CTRL:
        return s->clk_ctrl;

    case GCR_PMR:
        return s->pmr;

    case GCR_PCLK_DIV:
        return s->pclk_div;

    case GCR_PCLK_DIS0:
        return s->pclk_dis0;

    case GCR_MEM_CLK:
        return s->mem_clk;

    case GCR_MEM_ZERO:
        return s->mem_zero;

    case GCR_SYS_STAT:
        return s->sys_stat;

    case GCR_RST1:
        return s->rst1;

    case GCR_PCLK_DIS1:
        return s->pclk_dis1;

    case GCR_EVENT_EN:
        return s->event_en;

    case GCR_REV:
        return s->rev;

    case GCR_SYS_STAT_IE:
        return s->sys_stat_ie;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"
            HWADDR_PRIx "\n", __func__, addr);
        return 0;
    }
}

/* Chunk size for the MEM_ZERO SRAM-clear loop below; arbitrary, just keeps
 * the on-stack buffer small regardless of how large the linked SRAM region
 * is. */
#define MAX32650_GCR_ZERO_CHUNK 4096

static void max32650_gcr_write(void *opaque, hwaddr addr,
                                uint64_t val64, unsigned int size)
{
    Max32650GcrState *s = opaque;
    uint32_t val = val64;
    uint8_t zero[MAX32650_GCR_ZERO_CHUNK] = {0};

    switch (addr) {
    case GCR_SCON:
        /* Checksum calculations always pass immediately */
        s->scon = (val & 0x30000) | 0x1002;
        break;

    case GCR_RST0:
        if (val & RST0_SYS) {
            qemu_system_reset_request(SHUTDOWN_CAUSE_GUEST_RESET);
        }
        if (val & (RST0_PERIPH | RST0_SOFT)) {
            device_cold_reset(s->uart0);
            device_cold_reset(s->uart1);
            device_cold_reset(s->uart2);
        }
        if (val & RST0_UART0) {
            device_cold_reset(s->uart0);
        }
        if (val & RST0_UART1) {
            device_cold_reset(s->uart1);
        }
        if (val & RST0_UART2) {
            device_cold_reset(s->uart2);
        }
        /* TODO: As other devices are implemented, add them here */
        break;

    case GCR_CLK_CTRL:
        s->clk_ctrl = val | CLK_CTRL_SYSOSC_RDY;
        break;

    case GCR_PMR:
        s->pmr = val;
        break;

    case GCR_PCLK_DIV:
        s->pclk_div = val;
        break;

    case GCR_PCLK_DIS0:
        s->pclk_dis0 = val;
        break;

    case GCR_MEM_CLK:
        s->mem_clk = val;
        break;

    case GCR_MEM_ZERO:
        if (val & MEM_ZERO_SRAM_MASK) {
            uint64_t sram_size = memory_region_size(s->sram);
            hwaddr off;
            for (off = 0; off < sram_size; off += MAX32650_GCR_ZERO_CHUNK) {
                uint64_t chunk = MIN((uint64_t)MAX32650_GCR_ZERO_CHUNK,
                                     sram_size - off);
                address_space_write(&s->sram_as, off,
                                    MEMTXATTRS_UNSPECIFIED, zero, chunk);
            }
        }
        break;

    case GCR_SYS_STAT:
        s->sys_stat = val;
        break;

    case GCR_RST1:
        /* TODO: As other devices are implemented, add them here */
        s->rst1 = val;
        break;

    case GCR_PCLK_DIS1:
        s->pclk_dis1 = val;
        break;

    case GCR_EVENT_EN:
        s->event_en = val;
        break;

    case GCR_REV:
        s->rev = val;
        break;

    case GCR_SYS_STAT_IE:
        s->sys_stat_ie = val;
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static const Property max32650_gcr_properties[] = {
    DEFINE_PROP_LINK("sram", Max32650GcrState, sram,
                     TYPE_MEMORY_REGION, MemoryRegion*),
    DEFINE_PROP_LINK("uart0", Max32650GcrState, uart0,
                     TYPE_MAX32650_UART, DeviceState*),
    DEFINE_PROP_LINK("uart1", Max32650GcrState, uart1,
                     TYPE_MAX32650_UART, DeviceState*),
    DEFINE_PROP_LINK("uart2", Max32650GcrState, uart2,
                     TYPE_MAX32650_UART, DeviceState*),
};

static const MemoryRegionOps max32650_gcr_ops = {
    .read = max32650_gcr_read,
    .write = max32650_gcr_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription vmstate_max32650_gcr = {
    .name = TYPE_MAX32650_GCR,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(scon, Max32650GcrState),
        VMSTATE_UINT32(rst0, Max32650GcrState),
        VMSTATE_UINT32(clk_ctrl, Max32650GcrState),
        VMSTATE_UINT32(pmr, Max32650GcrState),
        VMSTATE_UINT32(pclk_div, Max32650GcrState),
        VMSTATE_UINT32(pclk_dis0, Max32650GcrState),
        VMSTATE_UINT32(mem_clk, Max32650GcrState),
        VMSTATE_UINT32(mem_zero, Max32650GcrState),
        VMSTATE_UINT32(sys_stat, Max32650GcrState),
        VMSTATE_UINT32(rst1, Max32650GcrState),
        VMSTATE_UINT32(pclk_dis1, Max32650GcrState),
        VMSTATE_UINT32(event_en, Max32650GcrState),
        VMSTATE_UINT32(rev, Max32650GcrState),
        VMSTATE_UINT32(sys_stat_ie, Max32650GcrState),
        VMSTATE_END_OF_LIST()
    }
};

static void max32650_gcr_init(Object *obj)
{
    Max32650GcrState *s = MAX32650_GCR(obj);

    memory_region_init_io(&s->mmio, obj, &max32650_gcr_ops, s,
                          TYPE_MAX32650_GCR, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void max32650_gcr_realize(DeviceState *dev, Error **errp)
{
    Max32650GcrState *s = MAX32650_GCR(dev);

    address_space_init(&s->sram_as, s->sram, "sram");
}

static void max32650_gcr_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    device_class_set_props(dc, max32650_gcr_properties);

    dc->realize = max32650_gcr_realize;
    dc->vmsd = &vmstate_max32650_gcr;
    rc->phases.hold = max32650_gcr_reset_hold;
}

static const TypeInfo max32650_gcr_info = {
    .name          = TYPE_MAX32650_GCR,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max32650GcrState),
    .instance_init = max32650_gcr_init,
    .class_init     = max32650_gcr_class_init,
};

static void max32650_gcr_register_types(void)
{
    type_register_static(&max32650_gcr_info);
}

type_init(max32650_gcr_register_types)
