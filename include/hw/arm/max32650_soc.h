/*
 * MAX32650 SOC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * platform-sdk local addition. Register offsets and base addresses below
 * are taken directly from this part's own CMSIS headers and the "_RevA"
 * PeriphDriver register headers for the MAX32650 -- every device model
 * here (GCR, ICC, TRNG, UART, GPIO, SPI) is a from-scratch MAX32650 model
 * checked against those headers and against real firmware boot behavior,
 * not adapted from another part.
 *
 * MAX32650 has no AES peripheral at all (it only has AESKEYS key storage at
 * an unrelated address nothing in this project touches), so this SoC does
 * not instantiate an AES device.
 *
 * Memory map: 3MB flash @0x10000000, 1MB SRAM @0x20000000, per the
 * MAX32650's linker script.
 */

#ifndef HW_ARM_MAX32650_SOC_H
#define HW_ARM_MAX32650_SOC_H

#include "hw/or-irq.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/max32650_gcr.h"
#include "hw/misc/max32650_icc.h"
#include "hw/char/max32650_uart.h"
#include "hw/misc/max32650_trng.h"
#include "hw/gpio/max32650_gpio.h"
#include "hw/ssi/max32650_spi.h"
#include "qom/object.h"

#define TYPE_MAX32650_SOC "max32650-soc"
OBJECT_DECLARE_SIMPLE_TYPE(MAX32650State, MAX32650_SOC)

#define MAX32650_FLASH_BASE_ADDRESS 0x10000000
#define MAX32650_FLASH_SIZE (3 * 1024 * 1024)
#define MAX32650_SRAM_BASE_ADDRESS 0x20000000
#define MAX32650_SRAM_SIZE (1024 * 1024)

/*
 * MXC_INFO_MEM_BASE/_SIZE from max32650.h -- a separate 16KB flash "info"
 * block (factory trim/calibration/user data), distinct from main flash.
 * The part's flash-controller init/self-test code touches this directly, so
 * it must be mapped even though this project has no real trim data to serve
 * -- backed by plain RAM (read-back-what-you-write) rather than left
 * unimplemented, which faulted with a Data Abort during boot.
 */
#define MAX32650_INFO_MEM_BASE_ADDRESS 0x10800000
#define MAX32650_INFO_MEM_SIZE (16 * 1024)

/* Only icc0 exists on MAX32650 (single Cortex-M4F core, no second cache) */
#define MAX32650_NUM_ICC 1
#define MAX32650_NUM_UART 3
#define MAX32650_NUM_GPIO 4
#define MAX32650_NUM_SPI 2

struct MAX32650State {
    SysBusDevice parent_obj;

    ARMv7MState armv7m;

    MemoryRegion sram;
    MemoryRegion flash;
    MemoryRegion info_mem;

    Max32650GcrState gcr;
    Max32650IccState icc[MAX32650_NUM_ICC];
    Max32650UartState uart[MAX32650_NUM_UART];
    Max32650TrngState trng;
    Max32650GpioState gpio[MAX32650_NUM_GPIO];
    Max32650SpiState spi[MAX32650_NUM_SPI];

    Clock *sysclk;

    /*
     * SPI0 has no fixed peripheral on the real max32650fthr board (unlike
     * SPI1, permanently wired to the ADIN1110 below) -- this stands in for
     * physically shorting its MISO/MOSI pins together, for firmware that
     * assumes exactly that (e.g. msdk's own SPI example). Off by default so
     * it never intercepts traffic a future real SPI0 peripheral would get.
     */
    bool spi0_loopback;
};

#endif
