/*
 * MAX32650 UART
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Register offsets below are UART_REVA's real layout, taken from msdk
 * Libraries/PeriphDrivers/Source/UART/uart_reva_regs.h -- notably the real
 * FIFO data register is at 0x1c.
 */

#ifndef HW_CHAR_MAX32650_UART_H
#define HW_CHAR_MAX32650_UART_H

#include "hw/sysbus.h"
#include "chardev/char-fe.h"
#include "qemu/fifo8.h"
#include "qom/object.h"

/* Register offsets, relative to the per-instance UART base address */
#define UART_CTRL         0x00
#define UART_THRESH_CTRL  0x04
#define UART_STATUS       0x08
#define UART_INT_EN       0x0c
#define UART_INT_FL       0x10
#define UART_BAUD0        0x14
#define UART_BAUD1        0x18
#define UART_FIFO         0x1c
#define UART_DMA          0x20
#define UART_TX_FIFO      0x24

#define UART_CTRL_ENABLE     (1 << 0)
#define UART_CTRL_TX_FLUSH   (1 << 5)
#define UART_CTRL_RX_FLUSH   (1 << 6)

#define UART_STATUS_RX_EMPTY (1 << 4)
#define UART_STATUS_RX_FULL  (1 << 5)
#define UART_STATUS_TX_EMPTY (1 << 6)
#define UART_STATUS_TX_FULL  (1 << 7)
#define UART_STATUS_RX_FIFO_CNT_POS 8
#define UART_STATUS_TX_FIFO_CNT_POS 16

#define UART_INT_RX_FIFO_THRESH      (1 << 4)
#define UART_INT_TX_FIFO_ALMOST_EMPTY (1 << 5)
#define UART_INT_TX_FIFO_THRESH      (1 << 6)

#define MAX32650_UART_FIFO_DEPTH 32

#define TYPE_MAX32650_UART "max32650-uart"
OBJECT_DECLARE_SIMPLE_TYPE(Max32650UartState, MAX32650_UART)

struct Max32650UartState {
    SysBusDevice parent_obj;

    MemoryRegion mmio;
    CharFrontend chr;
    qemu_irq irq;

    uint32_t ctrl;
    uint32_t thresh_ctrl;
    uint32_t int_en;
    uint32_t int_fl;
    uint32_t baud0;
    uint32_t baud1;
    uint32_t dma;

    Fifo8 rx_fifo;
};

#endif /* HW_CHAR_MAX32650_UART_H */
