/*
 * MAX32650 SPI
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_MAX32650_SPI_H
#define HW_MAX32650_SPI_H

#include "hw/sysbus.h"
#include "hw/ssi/ssi.h"
#include "qemu/fifo8.h"
#include "qom/object.h"

#define SPI_FIFO        0x00
#define SPI_CTRL0       0x04
#define SPI_CTRL1       0x08
#define SPI_CTRL2       0x0c
#define SPI_SSTIME      0x10
#define SPI_CLKCTRL     0x14
#define SPI_DMA         0x1c
#define SPI_INTFL       0x20
#define SPI_INTEN       0x24
#define SPI_WKFL        0x28
#define SPI_WKEN        0x2c
#define SPI_STAT        0x30

/* CTRL0 */
#define SPI_CTRL0_EN            (1 << 0)
#define SPI_CTRL0_MST_MODE      (1 << 1)
#define SPI_CTRL0_SS_IO         (1 << 4)
#define SPI_CTRL0_START         (1 << 5)
#define SPI_CTRL0_SS_CTRL       (1 << 8)
#define SPI_CTRL0_SS_ACTIVE_POS 16
#define SPI_CTRL0_SS_ACTIVE     (0xf << SPI_CTRL0_SS_ACTIVE_POS)

/* CTRL1 */
#define SPI_CTRL1_TX_NUM_CHAR_POS 0
#define SPI_CTRL1_TX_NUM_CHAR     (0xffff << SPI_CTRL1_TX_NUM_CHAR_POS)
#define SPI_CTRL1_RX_NUM_CHAR_POS 16
#define SPI_CTRL1_RX_NUM_CHAR     (0xffffU << SPI_CTRL1_RX_NUM_CHAR_POS)

/* CTRL2 */
#define SPI_CTRL2_CLKPHA          (1 << 0)
#define SPI_CTRL2_CLKPOL          (1 << 1)
#define SPI_CTRL2_NUMBITS_POS     8
#define SPI_CTRL2_NUMBITS         (0xf << SPI_CTRL2_NUMBITS_POS)
#define SPI_CTRL2_DATA_WIDTH_POS  12
#define SPI_CTRL2_DATA_WIDTH      (0x7 << SPI_CTRL2_DATA_WIDTH_POS)
#define SPI_CTRL2_THREE_WIRE      (1 << 15)
#define SPI_CTRL2_SS_POL_POS      16
#define SPI_CTRL2_SS_POL          (0xff << SPI_CTRL2_SS_POL_POS)

/* DMA
 *
 * Only the TX_LVL/RX_LVL fields (the live FIFO occupancy counts) and the
 * TX_FLUSH/RX_FLUSH pulse bits have any effect here -- real DMA is not
 * emulated. TX_LVL/RX_LVL are synthesized from the actual Fifo8 occupancy
 * on every read rather than stored, since the real MAX32650 SPI driver
 * (MXC_SPI_GetRXFIFOAvailable()/MXC_SPI_GetTXFIFOAvailable()) polls these
 * fields to know how many bytes it may push/pop -- leaving them static
 * would make a real guest driver spin forever waiting for RX_LVL to
 * become nonzero.
 */
#define SPI_DMA_TX_THD_VAL_POS  0
#define SPI_DMA_TX_THD_VAL      (0x1f << SPI_DMA_TX_THD_VAL_POS)
#define SPI_DMA_TX_FIFO_EN      (1 << 6)
#define SPI_DMA_TX_FLUSH        (1 << 7)
#define SPI_DMA_TX_LVL_POS      8
#define SPI_DMA_TX_LVL          (0x3f << SPI_DMA_TX_LVL_POS)
#define SPI_DMA_DMA_TX_EN       (1 << 15)
#define SPI_DMA_RX_THD_VAL_POS  16
#define SPI_DMA_RX_THD_VAL      (0x1f << SPI_DMA_RX_THD_VAL_POS)
#define SPI_DMA_RX_FIFO_EN      (1 << 22)
#define SPI_DMA_RX_FLUSH        (1 << 23)
#define SPI_DMA_RX_LVL_POS      24
#define SPI_DMA_RX_LVL          (0x3fU << SPI_DMA_RX_LVL_POS)
#define SPI_DMA_DMA_RX_EN       (1U << 31)

/* INTFL / INTEN / WKFL / WKEN share the same bit layout */
#define SPI_INT_TX_THD   (1 << 0)
#define SPI_INT_TX_EM    (1 << 1)
#define SPI_INT_RX_THD   (1 << 2)
#define SPI_INT_RX_FULL  (1 << 3)
#define SPI_INT_SSA      (1 << 4)
#define SPI_INT_SSD      (1 << 5)
#define SPI_INT_FAULT    (1 << 8)
#define SPI_INT_ABORT    (1 << 9)
#define SPI_INT_MST_DONE (1 << 11)
#define SPI_INT_TX_OV    (1 << 12)
#define SPI_INT_TX_UN    (1 << 13)
#define SPI_INT_RX_OV    (1 << 14)
#define SPI_INT_RX_UN    (1 << 15)

/* STAT */
#define SPI_STAT_BUSY (1 << 0)

/* Matches the real MAX32650 SPI hardware FIFO depth (MXC_SPI_FIFO_DEPTH in
 * the MSDK), so the TX_LVL/RX_LVL bookkeeping above lines up with what the
 * real driver's byte-count math (compiled against that same constant)
 * expects.
 */
#define MAX32650_SPI_FIFO_DEPTH 32

#define TYPE_MAX32650_SPI "max32650-spi"
OBJECT_DECLARE_SIMPLE_TYPE(Max32650SpiState, MAX32650_SPI)

struct Max32650SpiState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;

    uint32_t ctrl0;
    uint32_t ctrl1;
    uint32_t ctrl2;
    uint32_t sstime;
    uint32_t clkctrl;
    uint32_t dma;
    uint32_t intfl;
    uint32_t inten;
    uint32_t wkfl;
    uint32_t wken;

    Fifo8 tx_fifo;
    Fifo8 rx_fifo;

    qemu_irq irq;
    SSIBus *bus;
};

/**
 * max32650_spi_get_bus: return the SSIBus owned by this controller
 * @s: the MAX32650 SPI device instance
 *
 * Used by board/SoC composition code to attach a SSI peripheral (e.g. the
 * ADIN1110 model) to this controller after it has been realized.
 */
SSIBus *max32650_spi_get_bus(Max32650SpiState *s);

#endif /* HW_MAX32650_SPI_H */
