/*
 * MAX32650 SPI
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "hw/ssi/max32650_spi.h"
#include "hw/irq.h"
#include "qemu/log.h"
#include "qemu/module.h"
#include "migration/vmstate.h"

static void max32650_spi_update_irq(Max32650SpiState *s)
{
    int interrupt_level;

    interrupt_level = s->intfl & s->inten;
    qemu_set_irq(s->irq, interrupt_level);
}

/*
 * Drain what's queued in the TX FIFO across the SSI bus, pushing each
 * returned byte into the RX FIFO, stopping if the RX FIFO fills up before
 * TX empties. Real hardware shifts bytes out continuously once CTRL0.START
 * is asserted, refilling the TX FIFO and draining the RX FIFO as software
 * services it, and stalls the shift engine (no more bytes go out) once RX
 * has no room left rather than ever discarding a received byte -- so this
 * immediate/synchronous model must stop short at a full RX FIFO too,
 * leaving the rest queued in TX for a later call to pick up, instead of
 * dropping bytes there's no room for. (An earlier version dropped them,
 * which is invisible for any transfer that fits in one FIFO's worth, but
 * permanently -- and silently -- loses bytes on any full-duplex transfer
 * bigger than the FIFO, such as ADIN1110 frame reads/writes: the master's
 * byte-accounting then never reaches its expected total and its transfer
 * polling loop spins forever.)
 *
 * Called both when START is first asserted (draining whatever was queued
 * before the transfer started) and again from the FIFO write/read paths
 * while the engine is still running, so a transfer larger than the FIFO
 * makes progress as software refills TX and drains RX.
 */
static void max32650_spi_flush_tx(Max32650SpiState *s)
{
    while (!fifo8_is_empty(&s->tx_fifo) && !fifo8_is_full(&s->rx_fifo)) {
        uint8_t tx = fifo8_pop(&s->tx_fifo);
        uint8_t rx = ssi_transfer(s->bus, tx);

        fifo8_push(&s->rx_fifo, rx);
    }

    if (fifo8_is_empty(&s->tx_fifo)) {
        s->intfl |= SPI_INT_MST_DONE;
    }
}

static void max32650_spi_reset_hold(Object *obj, ResetType type)
{
    Max32650SpiState *s = MAX32650_SPI(obj);

    s->ctrl0 = 0;
    s->ctrl1 = 0;
    s->ctrl2 = 0;
    s->sstime = 0;
    s->clkctrl = 0;
    s->dma = 0;
    s->intfl = 0;
    s->inten = 0;
    s->wkfl = 0;
    s->wken = 0;

    fifo8_reset(&s->tx_fifo);
    fifo8_reset(&s->rx_fifo);

    /* Deasserted (this device's SS lines are active-low, per real hardware
     * and the no-OS platform driver's SPI_SS_POL_LOW default). */
    qemu_set_irq(s->cs, 1);
}

static uint64_t max32650_spi_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Max32650SpiState *s = opaque;
    uint64_t retvalue = 0;

    switch (addr) {
    case SPI_FIFO:
        if (!fifo8_is_empty(&s->rx_fifo)) {
            retvalue = fifo8_pop(&s->rx_fifo);
        }
        /*
         * Popping may have freed the room that stalled max32650_spi_flush_tx()
         * (see its comment) -- resume shifting out whatever's still queued
         * in TX now that there's space for the responses again.
         */
        if ((s->ctrl0 & SPI_CTRL0_EN) && (s->ctrl0 & SPI_CTRL0_START)) {
            max32650_spi_flush_tx(s);
        }
        break;
    case SPI_CTRL0:
        retvalue = s->ctrl0;
        break;
    case SPI_CTRL1:
        retvalue = s->ctrl1;
        break;
    case SPI_CTRL2:
        retvalue = s->ctrl2;
        break;
    case SPI_SSTIME:
        retvalue = s->sstime;
        break;
    case SPI_CLKCTRL:
        retvalue = s->clkctrl;
        break;
    case SPI_DMA:
        /*
         * TX_LVL/RX_LVL are synthesized from the live FIFO occupancy
         * rather than the stored value -- see the comment above these
         * fields in the header.
         */
        retvalue = (s->dma & ~(SPI_DMA_TX_LVL | SPI_DMA_RX_LVL)) |
                   (fifo8_num_used(&s->tx_fifo) << SPI_DMA_TX_LVL_POS) |
                   (fifo8_num_used(&s->rx_fifo) << SPI_DMA_RX_LVL_POS);
        break;
    case SPI_INTFL:
        retvalue = s->intfl;
        break;
    case SPI_INTEN:
        retvalue = s->inten;
        break;
    case SPI_WKFL:
        retvalue = s->wkfl;
        break;
    case SPI_WKEN:
        retvalue = s->wken;
        break;
    case SPI_STAT:
        /*
         * Transfers complete synchronously within the register write that
         * triggers them, so BUSY is never observably set.
         */
        retvalue = 0;
        break;
    default:
        qemu_log_mask(LOG_GUEST_ERROR,
            "%s: Bad offset 0x%"HWADDR_PRIx"\n", __func__, addr);
        break;
    }

    return retvalue;
}

static void max32650_spi_write(void *opaque, hwaddr addr,
                                 uint64_t val64, unsigned int size)
{
    Max32650SpiState *s = opaque;
    uint32_t value = val64;

    switch (addr) {
    case SPI_FIFO:
        if (fifo8_is_full(&s->tx_fifo)) {
            s->intfl |= SPI_INT_TX_OV;
        } else {
            fifo8_push(&s->tx_fifo, value & 0xff);
        }

        /*
         * If the engine is already enabled and started, new bytes are
         * shifted out as soon as they are queued (see
         * max32650_spi_flush_tx()).
         */
        if ((s->ctrl0 & SPI_CTRL0_EN) && (s->ctrl0 & SPI_CTRL0_START)) {
            max32650_spi_flush_tx(s);
        }
        max32650_spi_update_irq(s);
        return;
    case SPI_CTRL0: {
        uint32_t old = s->ctrl0;

        /*
         * The real MAX32650 SPI driver (no-OS max32650/maxim_spi.c) asserts
         * SS by setting START (with SS_CTRL cleared, i.e. cs_change mode --
         * the only mode this workspace's SPI users configure) and
         * deasserts it by clearing START once the transaction's declared
         * byte count has been shifted -- so mirroring START's edges onto
         * the cs line reproduces real per-transaction chip-select framing
         * without needing to separately track CTRL1's byte count here.
         */
        if (!(old & SPI_CTRL0_START) && (value & SPI_CTRL0_START)) {
            qemu_set_irq(s->cs, 0);
            s->intfl |= SPI_INT_SSA;
        }

        s->ctrl0 = value;
        if ((value & SPI_CTRL0_EN) && (value & SPI_CTRL0_START)) {
            max32650_spi_flush_tx(s);
        }

        if ((old & SPI_CTRL0_START) && !(value & SPI_CTRL0_START)) {
            qemu_set_irq(s->cs, 1);
            s->intfl |= SPI_INT_SSD;
        }

        max32650_spi_update_irq(s);
        return;
    }
    case SPI_CTRL1:
        s->ctrl1 = value;
        return;
    case SPI_CTRL2:
        s->ctrl2 = value;
        return;
    case SPI_SSTIME:
        s->sstime = value;
        return;
    case SPI_CLKCTRL:
        s->clkctrl = value;
        return;
    case SPI_DMA:
        if (value & SPI_DMA_TX_FLUSH) {
            fifo8_reset(&s->tx_fifo);
        }
        if (value & SPI_DMA_RX_FLUSH) {
            fifo8_reset(&s->rx_fifo);
        }
        /*
         * TX_FLUSH/RX_FLUSH are self-clearing pulses, and TX_LVL/RX_LVL
         * are computed on read -- don't store any of them.
         */
        s->dma = value & ~(SPI_DMA_TX_FLUSH | SPI_DMA_RX_FLUSH |
                            SPI_DMA_TX_LVL | SPI_DMA_RX_LVL);
        return;
    case SPI_INTFL:
        /* Write-1-to-clear. */
        s->intfl &= ~value;
        max32650_spi_update_irq(s);
        return;
    case SPI_INTEN:
        s->inten = value;
        max32650_spi_update_irq(s);
        return;
    case SPI_WKFL:
        /* Write-1-to-clear. */
        s->wkfl &= ~value;
        return;
    case SPI_WKEN:
        s->wken = value;
        return;
    case SPI_STAT:
        /* SPI_STAT is read only */
        return;
    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"
            HWADDR_PRIx "\n", __func__, addr);
    }
}

static const MemoryRegionOps max32650_spi_ops = {
    .read = max32650_spi_read,
    .write = max32650_spi_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    /*
     * The real FIFO register is a union (fifo32/fifo16[2]/fifo8[4]) and the
     * driver does byte-at-a-time stores to it via the fifo8[] member -- a
     * 4-byte-only restriction (this device's first cut only allowed full
     * word accesses) faults on that access, confirmed by booting real
     * firmware and observing a Data Abort at this device's base address.
     */
    .valid.min_access_size = 1,
    .valid.max_access_size = 4,
};

static const VMStateDescription max32650_spi_vmstate = {
    .name = TYPE_MAX32650_SPI,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (VMStateField[]) {
        VMSTATE_UINT32(ctrl0, Max32650SpiState),
        VMSTATE_UINT32(ctrl1, Max32650SpiState),
        VMSTATE_UINT32(ctrl2, Max32650SpiState),
        VMSTATE_UINT32(sstime, Max32650SpiState),
        VMSTATE_UINT32(clkctrl, Max32650SpiState),
        VMSTATE_UINT32(dma, Max32650SpiState),
        VMSTATE_UINT32(intfl, Max32650SpiState),
        VMSTATE_UINT32(inten, Max32650SpiState),
        VMSTATE_UINT32(wkfl, Max32650SpiState),
        VMSTATE_UINT32(wken, Max32650SpiState),
        VMSTATE_FIFO8(tx_fifo, Max32650SpiState),
        VMSTATE_FIFO8(rx_fifo, Max32650SpiState),
        VMSTATE_END_OF_LIST()
    }
};

static void max32650_spi_init(Object *obj)
{
    Max32650SpiState *s = MAX32650_SPI(obj);

    fifo8_create(&s->tx_fifo, MAX32650_SPI_FIFO_DEPTH);
    fifo8_create(&s->rx_fifo, MAX32650_SPI_FIFO_DEPTH);

    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);
    qdev_init_gpio_out_named(DEVICE(obj), &s->cs, "cs", 1);

    memory_region_init_io(&s->mmio, obj, &max32650_spi_ops, s,
                          TYPE_MAX32650_SPI, 0x2000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);

    s->bus = ssi_create_bus(DEVICE(obj), "ssi");
}

static void max32650_spi_finalize(Object *obj)
{
    Max32650SpiState *s = MAX32650_SPI(obj);

    fifo8_destroy(&s->tx_fifo);
    fifo8_destroy(&s->rx_fifo);
}

SSIBus *max32650_spi_get_bus(Max32650SpiState *s)
{
    return s->bus;
}

static void max32650_spi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = max32650_spi_reset_hold;

    dc->vmsd = &max32650_spi_vmstate;
}

static const TypeInfo max32650_spi_info = {
    .name          = TYPE_MAX32650_SPI,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max32650SpiState),
    .instance_init = max32650_spi_init,
    .instance_finalize = max32650_spi_finalize,
    .class_init    = max32650_spi_class_init,
};

static void max32650_spi_register_types(void)
{
    type_register_static(&max32650_spi_info);
}

type_init(max32650_spi_register_types)
