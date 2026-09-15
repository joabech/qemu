/*
 * MAX32650 UART
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/char/max32650_uart.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-properties-system.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"

static int max32650_uart_can_receive(void *opaque)
{
    Max32650UartState *s = opaque;

    if (!(s->ctrl & UART_CTRL_ENABLE)) {
        return 0;
    }
    return fifo8_num_free(&s->rx_fifo);
}

static void max32650_uart_update_irq(Max32650UartState *s)
{
    uint32_t status = 0;

    if (fifo8_num_used(&s->rx_fifo) >=
        (s->thresh_ctrl & 0x3f)) {
        status |= UART_INT_RX_FIFO_THRESH;
    }
    /* TX is always empty (synchronous model). */
    status |= UART_INT_TX_FIFO_ALMOST_EMPTY | UART_INT_TX_FIFO_THRESH;

    s->int_fl |= (status & s->int_en);

    qemu_set_irq(s->irq, (s->int_fl & s->int_en) != 0);
}

static void max32650_uart_receive(void *opaque, const uint8_t *buf, int size)
{
    Max32650UartState *s = opaque;

    assert(size <= fifo8_num_free(&s->rx_fifo));
    fifo8_push_all(&s->rx_fifo, buf, size);

    max32650_uart_update_irq(s);
}

static void max32650_uart_reset_hold(Object *obj, ResetType type)
{
    Max32650UartState *s = MAX32650_UART(obj);

    s->ctrl = 0;
    s->thresh_ctrl = 0;
    s->int_en = 0;
    s->int_fl = 0;
    s->baud0 = 0;
    s->baud1 = 0;
    s->dma = 0;
    fifo8_reset(&s->rx_fifo);
}

static uint64_t max32650_uart_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Max32650UartState *s = opaque;
    uint64_t retvalue = 0;

    switch (addr) {
    case UART_CTRL:
        retvalue = s->ctrl;
        break;
    case UART_THRESH_CTRL:
        retvalue = s->thresh_ctrl;
        break;
    case UART_STATUS:
        retvalue = (fifo8_is_empty(&s->rx_fifo) ? UART_STATUS_RX_EMPTY : 0) |
                   (fifo8_is_full(&s->rx_fifo) ? UART_STATUS_RX_FULL : 0) |
                   UART_STATUS_TX_EMPTY |
                   (fifo8_num_used(&s->rx_fifo) << UART_STATUS_RX_FIFO_CNT_POS);
        break;
    case UART_INT_EN:
        retvalue = s->int_en;
        break;
    case UART_INT_FL:
        retvalue = s->int_fl;
        break;
    case UART_BAUD0:
        retvalue = s->baud0;
        break;
    case UART_BAUD1:
        retvalue = s->baud1;
        break;
    case UART_FIFO:
        if (!fifo8_is_empty(&s->rx_fifo)) {
            retvalue = fifo8_pop(&s->rx_fifo);
        }
        break;
    case UART_DMA:
        /* DMA not implemented. */
        retvalue = s->dma;
        break;
    case UART_TX_FIFO:
        /* Not used by the real driver's byte-at-a-time path; harmless. */
        retvalue = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }

    return retvalue;
}

static void max32650_uart_write(void *opaque, hwaddr addr,
                                 uint64_t val64, unsigned int size)
{
    Max32650UartState *s = opaque;
    uint32_t value = val64;
    uint8_t data;

    switch (addr) {
    case UART_CTRL:
        if (value & UART_CTRL_RX_FLUSH) {
            fifo8_reset(&s->rx_fifo);
        }
        s->ctrl = value & ~(UART_CTRL_TX_FLUSH | UART_CTRL_RX_FLUSH);
        return;
    case UART_THRESH_CTRL:
        s->thresh_ctrl = value;
        return;
    case UART_STATUS:
        /* UART_STATUS is read only. */
        return;
    case UART_INT_EN:
        s->int_en = value;
        max32650_uart_update_irq(s);
        return;
    case UART_INT_FL:
        /* Write-1-to-clear. */
        s->int_fl &= ~value;
        max32650_uart_update_irq(s);
        return;
    case UART_BAUD0:
        s->baud0 = value;
        return;
    case UART_BAUD1:
        s->baud1 = value;
        return;
    case UART_FIFO:
        data = value & 0xff;
        /*
         * XXX this blocks the whole thread. Rewrite to use
         * qemu_chr_fe_write and background I/O callbacks if this ever
         * becomes a bottleneck.
         */
        qemu_chr_fe_write_all(&s->chr, &data, 1);
        max32650_uart_update_irq(s);
        return;
    case UART_DMA:
        /* DMA not implemented. */
        s->dma = value;
        return;
    case UART_TX_FIFO:
        /* Not used by the real driver's byte-at-a-time path; harmless. */
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"
            HWADDR_PRIx "\n", __func__, addr);
    }
}

static const MemoryRegionOps max32650_uart_ops = {
    .read = max32650_uart_read,
    .write = max32650_uart_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const Property max32650_uart_properties[] = {
    DEFINE_PROP_CHR("chardev", Max32650UartState, chr),
};

static const VMStateDescription max32650_uart_vmstate = {
    .name = TYPE_MAX32650_UART,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctrl, Max32650UartState),
        VMSTATE_UINT32(thresh_ctrl, Max32650UartState),
        VMSTATE_UINT32(int_en, Max32650UartState),
        VMSTATE_UINT32(int_fl, Max32650UartState),
        VMSTATE_UINT32(baud0, Max32650UartState),
        VMSTATE_UINT32(baud1, Max32650UartState),
        VMSTATE_UINT32(dma, Max32650UartState),
        VMSTATE_FIFO8(rx_fifo, Max32650UartState),
        VMSTATE_END_OF_LIST()
    }
};

static void max32650_uart_init(Object *obj)
{
    Max32650UartState *s = MAX32650_UART(obj);

    fifo8_create(&s->rx_fifo, MAX32650_UART_FIFO_DEPTH);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &max32650_uart_ops, s,
                          TYPE_MAX32650_UART, 0x400);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void max32650_uart_finalize(Object *obj)
{
    Max32650UartState *s = MAX32650_UART(obj);
    fifo8_destroy(&s->rx_fifo);
}

static void max32650_uart_realize(DeviceState *dev, Error **errp)
{
    Max32650UartState *s = MAX32650_UART(dev);

    qemu_chr_fe_set_handlers(&s->chr, max32650_uart_can_receive,
                             max32650_uart_receive, NULL, NULL,
                             s, NULL, true);
}

static void max32650_uart_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = max32650_uart_reset_hold;

    device_class_set_props(dc, max32650_uart_properties);
    dc->realize = max32650_uart_realize;

    dc->vmsd = &max32650_uart_vmstate;
}

static const TypeInfo max32650_uart_info = {
    .name          = TYPE_MAX32650_UART,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max32650UartState),
    .instance_init = max32650_uart_init,
    .instance_finalize = max32650_uart_finalize,
    .class_init    = max32650_uart_class_init,
};

static void max32650_uart_register_types(void)
{
    type_register_static(&max32650_uart_info);
}

type_init(max32650_uart_register_types)
