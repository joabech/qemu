/*
 * MAX32650 SOC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * platform-sdk local addition. MAX32650 and MAX78000 are both MSDK "_RevA"
 * peripheral-generation parts, but sharing a peripheral *generation* does
 * NOT mean sharing an exact register *layout* -- do not assume it without
 * checking. Base addresses matching is necessary but not sufficient; each
 * peripheral's internal register offsets were checked independently by
 * diffing the two chips' per-part CMSIS headers under
 * msdk/Libraries/CMSIS/Device/Maxim/{MAX32650,MAX78000}/Include/:
 *
 *   - GCR: base address (0x40000000) AND every internal register offset
 *     match exactly (only field *names* differ, e.g. SCON vs SYSCTRL --
 *     MAX78000 additionally has ECC-related/GPR registers MAX32650 lacks, which
 *     this project's firmware never touches). Safe to reuse max78000-gcr
 *     as-is.
 *   - ICC0: base address (0x4002a000) and both register offsets (0x0000,
 *     0x0004, 0x0100) match exactly (again, names only differ). Safe to
 *     reuse max78000-icc as-is.
 *   - UART: base addresses match, but internal offsets do NOT past the
 *     first two registers (MAX78000 model's FIFO register lands at 0x20;
 *     MAX32650's real FIFO is at 0x1c) -- confirmed the hard way, by
 *     booting real firmware and finding it never reached the emulated FIFO
 *     register. hw/char/max32650_uart.c is a from-scratch model with the
 *     correct MAX32650 offsets; do not reuse max78000-uart here.
 *   - TRNG: base address differs (0x400b5000 vs MAX78000's 0x4004d000,
 *     corrected below) AND internal offsets differ (MAX78000 inserts an
 *     extra STATUS register at 0x04, shifting DATA to 0x08; MAX32650's DATA
 *     is at 0x04). max78000-trng is reused here ANYWAY as a known,
 *     tracked gap: this project's firmware only gates the TRNG's clock
 *     during SystemInit and never touches its registers, so the wrong
 *     offset is dormant. Fix with a real max32650_trng.c before any project
 *     that actually calls MXC_TRNG_* (e.g. anything touching crypto/DRBG)
 *     is run under this machine.
 *   - GPIO2 (0x4000a000, vs MAX78000's 0x40080400) and SPI0/SPI1
 *     (0x40046000/0x40047000, vs MAX78000's split 0x400be000/0x40046000)
 *     have different base addresses; both are from-scratch MAX32650 models
 *     (hw/gpio/max32650_gpio.c, hw/ssi/max32650_spi.c) since MAX78000's own
 *     SoC leaves these peripherals as unimplemented stubs anyway.
 *   - MAX32650 has no AES peripheral at all (MAX78000's crypto accelerator
 *     is absent on this part; MAX32650 only has AESKEYS key storage at a
 *     different, unrelated address that nothing in this project touches),
 *     so unlike max78000_soc.c this SoC does not instantiate an AES device.
 *
 * Memory map is also corrected: MAX32650 has 3MB flash / 1MB SRAM, not
 * MAX78000's 512KB / 128KB.
 */

#ifndef HW_ARM_MAX32650_SOC_H
#define HW_ARM_MAX32650_SOC_H

#include "hw/or-irq.h"
#include "hw/arm/armv7m.h"
#include "hw/misc/max78000_gcr.h"
#include "hw/misc/max78000_icc.h"
#include "hw/char/max32650_uart.h"
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

/*
 * MXC_INFO_MEM_BASE/_SIZE from max32650.h -- a separate 16KB flash "info"
 * block (factory trim/calibration/user data), distinct from main flash.
 * MSDK's flash-controller init/self-test code touches this directly, so it
 * must be mapped even though this project has no real trim data to serve --
 * backed by plain RAM (read-back-what-you-write) rather than left
 * unimplemented, which faulted with a Data Abort during boot.
 */
#define MAX32650_INFO_MEM_BASE_ADDRESS 0x10800000
#define MAX32650_INFO_MEM_SIZE (16 * 1024)

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
    MemoryRegion info_mem;

    Max78000GcrState gcr;
    Max78000IccState icc[MAX32650_NUM_ICC];
    Max32650UartState uart[MAX32650_NUM_UART];
    Max78000TrngState trng;
    Max32650GpioState gpio[MAX32650_NUM_GPIO];
    Max32650SpiState spi[MAX32650_NUM_SPI];

    Clock *sysclk;
};

#endif
