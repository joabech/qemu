/*
 * MAX32650 SOC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * platform-sdk local addition: MAX32650 shares its GCR/UART/ICC/TRNG
 * register *layout* with MAX78000 (same MSDK _RevA peripheral generation --
 * verified against msdk/Libraries/CMSIS/Device/Maxim/{MAX32650,MAX78000}/
 * Include/*.h), so this SoC reuses those MAX78000_* device types as-is.
 * Base addresses are NOT all identical between the two parts, though --
 * confirmed by direct comparison of the two chips' CMSIS headers:
 *   - GCR, ICC0, UART0/1/2, GPIO0/1 match MAX78000 exactly.
 *   - GPIO2 (0x4000a000, vs MAX78000's 0x40080400), SPI0/SPI1 (0x40046000/
 *     0x40047000, vs MAX78000's split 0x400be000/0x40046000), and TRNG
 *     (0x400b5000, vs MAX78000's 0x4004d000) all differ -- this file uses
 *     MAX32650's real addresses, not MAX78000's.
 *   - MAX32650 has no AES peripheral at all (MAX78000's crypto accelerator
 *     is absent on this part; MAX32650 only has AESKEYS key storage at a
 *     different, unrelated address that nothing in this project touches),
 *     so unlike max78000_soc.c this SoC does not instantiate an AES device.
 * Memory map is also corrected: MAX32650 has 3MB flash / 1MB SRAM, not
 * MAX78000's 512KB / 128KB. GPIO and SPI are real MAX32650-specific models
 * (hw/gpio/max32650_gpio.c, hw/ssi/max32650_spi.c) since MAX78000's own SoC
 * leaves those peripherals as unimplemented stubs.
 */

#ifndef HW_ARM_MAX32650_SOC_H
#define HW_ARM_MAX32650_SOC_H

#include "hw/or-irq.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/max78000_gcr.h"
#include "hw/misc/max78000_icc.h"
#include "hw/char/max78000_uart.h"
#include "hw/misc/max78000_trng.h"
#include "hw/gpio/max32650_gpio.h"
#include "hw/ssi/max32650_spi.h"
#include "qom/object.h"

#define TYPE_MAX32650_SOC "max32650-soc"
OBJECT_DECLARE_SIMPLE_TYPE(MAX32650State, MAX32650_SOC)

#define MAX32650_FLASH_BASE_ADDRESS 0x10000000
#define MAX32650_FLASH_SIZE (3 * 1024 * 1024)
#define MAX32650_SRAM_BASE_ADDRESS 0x20000000
#define MAX32650_SRAM_SIZE (1024 * 1024)

/* Only icc0 is wired on MAX32650; icc1 is MAX78000's RISC-V side-core cache */
#define MAX32650_NUM_ICC 1
#define MAX32650_NUM_UART 3
#define MAX32650_NUM_GPIO 3
#define MAX32650_NUM_SPI 2

struct MAX32650State {
    SysBusDevice parent_obj;

    ARMv7MState armv7m;

    MemoryRegion sram;
    MemoryRegion flash;

    Max78000GcrState gcr;
    Max78000IccState icc[MAX32650_NUM_ICC];
    Max78000UartState uart[MAX32650_NUM_UART];
    Max78000TrngState trng;
    Max32650GpioState gpio[MAX32650_NUM_GPIO];
    Max32650SpiState spi[MAX32650_NUM_SPI];

    Clock *sysclk;
};

#endif
