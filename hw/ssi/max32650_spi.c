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
 * How many bytes CTRL1 declares this transaction will move, accounting for
 * NUMBITS 9-16 packing two FIFO bytes per character. 0 means neither
 * TX_NUM_CHAR nor RX_NUM_CHAR was ever set (some drivers don't bother) --
 * callers fall back to draining "whatever's queued" for that case.
 */
static uint32_t max32650_spi_total_bytes(Max32650SpiState *s)
{
    unsigned bits = (s->ctrl2 & SPI_CTRL2_NUMBITS) >> SPI_CTRL2_NUMBITS_POS;
    unsigned effective_bits = bits ? bits : 16;
    uint32_t tx_chars = (s->ctrl1 & SPI_CTRL1_TX_NUM_CHAR) >>
                        SPI_CTRL1_TX_NUM_CHAR_POS;
    uint32_t rx_chars = (s->ctrl1 & SPI_CTRL1_RX_NUM_CHAR) >>
                        SPI_CTRL1_RX_NUM_CHAR_POS;
    uint32_t chars = MAX(tx_chars, rx_chars);

    return chars * (effective_bits > 8 ? 2 : 1);
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
 * Only does anything while s->running -- see the field's comment in the
 * header for why a transaction's "still in progress" state can't just be
 * read off CTRL0.START's current level. Called from all three FIFO/CTRL0
 * write and FIFO-read sites below unconditionally; it's a no-op otherwise.
 */
static void max32650_spi_flush_tx(Max32650SpiState *s)
{
    /*
     * NUMBITS (CTRL2, 0 meaning 16 per real hardware's own convention) is
     * the character width actually shifted onto the wire. Real hardware
     * only ever latches that many real bits per character into the
     * receive shift register -- the rest reads back as 0, not whatever
     * the peripheral drove on those unused clock edges. Masking rx here
     * (not e.g. at the FIFO-read side) matches where real hardware would
     * do it: at the point data actually moves off the wire into the
     * receive path.
     *
     * NUMBITS 9-16 packs one character across two FIFO bytes, low byte
     * first (msdk's own driver uses the FIFO16/32 aliases for those --
     * see spi_reva1.c's WriteTXFIFO/ReadRXFIFO): the low byte is always
     * fully used, only the high byte's low (bits-8) bits are real.
     * s->char_byte_parity tracks which half of the pair the next byte
     * this loop moves is, since nothing else in this byte-at-a-time model
     * knows where character boundaries fall.
     */
    unsigned bits = (s->ctrl2 & SPI_CTRL2_NUMBITS) >> SPI_CTRL2_NUMBITS_POS;
    uint8_t low_mask = 0xff;
    uint8_t high_mask = 0xff;
    uint32_t total;

    if (!s->running) {
        return;
    }

    if (bits > 0 && bits < 8) {
        low_mask = high_mask = (uint8_t)((1u << bits) - 1);
    } else if (bits > 8 && bits < 16) {
        high_mask = (uint8_t)((1u << (bits - 8)) - 1);
    }

    total = max32650_spi_total_bytes(s);

    while (!fifo8_is_empty(&s->tx_fifo) && !fifo8_is_full(&s->rx_fifo) &&
           (total == 0 || s->bytes_done < total)) {
        uint8_t tx = fifo8_pop(&s->tx_fifo);
        uint8_t rx_mask = s->char_byte_parity ? high_mask : low_mask;
        uint8_t rx = ssi_transfer(s->bus, tx) & rx_mask;

        fifo8_push(&s->rx_fifo, rx);
        s->bytes_done++;

        if (bits > 8) {
            s->char_byte_parity ^= 1;
        }
    }

    if (total != 0 ? (s->bytes_done >= total) : fifo8_is_empty(&s->tx_fifo)) {
        s->running = false;
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
    s->char_byte_parity = 0;
    s->running = false;
    s->bytes_done = 0;

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
    case SPI_FIFO: {
        /*
         * Real hardware aliases fifo8[4]/fifo16[2]/fifo32 over the same
         * bytes (msdk's own driver picks the access width matching NUMBITS
         * -- see spi_reva1.c's WriteTXFIFO/ReadRXFIFO), so a 2- or 4-byte
         * access here pops that many consecutive bytes, least-significant
         * first, not just the low byte of one.
         */
        unsigned int i;

        for (i = 0; i < size; i++) {
            uint8_t byte = 0;

            if (!fifo8_is_empty(&s->rx_fifo)) {
                byte = fifo8_pop(&s->rx_fifo);
            }
            retvalue |= ((uint64_t)byte) << (8 * i);
        }
        /*
         * Popping may have freed the room that stalled max32650_spi_flush_tx()
         * (see its comment) -- resume shifting out whatever's still queued
         * in TX now that there's space for the responses again.
         * max32650_spi_flush_tx() is a no-op unless s->running, so this is
         * safe to call unconditionally.
         */
        max32650_spi_flush_tx(s);
        break;
    }
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
    case SPI_FIFO: {
        /* See the matching comment in max32650_spi_read(). */
        unsigned int i;

        for (i = 0; i < size; i++) {
            uint8_t byte = (value >> (8 * i)) & 0xff;

            if (fifo8_is_full(&s->tx_fifo)) {
                s->intfl |= SPI_INT_TX_OV;
                break;
            }
            fifo8_push(&s->tx_fifo, byte);
        }

        /*
         * New bytes are shifted out as soon as they're queued, for as long
         * as a transaction is still running -- max32650_spi_flush_tx() is
         * a no-op otherwise, so this is safe to call unconditionally.
         */
        max32650_spi_flush_tx(s);
        max32650_spi_update_irq(s);
        return;
    }
    case SPI_CTRL0: {
        uint32_t old = s->ctrl0;

        /*
         * no-OS's own SPI driver (max32650/maxim_spi.c) asserts SS by
         * setting START and deasserts it by clearing START once the
         * transaction's declared byte count has been shifted, holding it
         * high for the whole transfer.
         *
         * msdk's own official PeriphDriver (spi_reva1.c
         * MXC_SPI_RevA1_MasterTransHandler, in hw_ss_control mode, the
         * driver's default) does NOT hold START high, though: real
         * hardware treats it as an edge-triggered "go" pulse that lets the
         * engine autonomously keep running to completion, so that driver
         * deliberately clears START again right after every refill call
         * (see msdk issue analogdevicesinc/msdk#713) and relies on the
         * hardware, not the bit's current level, to know a transfer is
         * still in progress.
         *
         * s->running is this model's stand-in for that hardware-tracked
         * "still in progress" state: latched true on START's rising edge,
         * only cleared once CTRL1's declared character count has actually
         * been shifted (see max32650_spi_flush_tx()) -- not by software
         * clearing START early, which would otherwise stall a multi-burst
         * transfer the moment a driver following msdk's real pattern
         * clears it after the first refill. CS assert/deassert still
         * follows START's edges directly (matching no-OS's driver, and
         * harmless for msdk's since nothing on SPI0 during this project's
         * testing cares about CS timing beyond the model's own SSI_CS_NONE
         * loopback peripheral).
         */
        if (!(old & SPI_CTRL0_START) && (value & SPI_CTRL0_START)) {
            qemu_set_irq(s->cs, 0);
            s->intfl |= SPI_INT_SSA;
            s->running = true;
            s->bytes_done = 0;
            /* Fresh transaction: the next byte is a character's low byte. */
            s->char_byte_parity = 0;
        }

        s->ctrl0 = value;
        max32650_spi_flush_tx(s);

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
            s->char_byte_parity = 0;
        }
        if (value & SPI_DMA_RX_FLUSH) {
            fifo8_reset(&s->rx_fifo);
            s->char_byte_parity = 0;
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
        VMSTATE_UINT8(char_byte_parity, Max32650SpiState),
        VMSTATE_BOOL(running, Max32650SpiState),
        VMSTATE_UINT32(bytes_done, Max32650SpiState),
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
