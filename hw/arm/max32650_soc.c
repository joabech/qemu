/*
 * MAX32650 SOC
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * platform-sdk local addition -- see include/hw/arm/max32650_soc.h for the
 * rationale (real, from-scratch MAX32650 GCR/ICC/TRNG/UART/GPIO/SPI device
 * models, MAX32650's own memory map, base addresses, and IRQ numbers).
 *
 * Base addresses and IRQ numbers below are taken from this part's own
 * CMSIS header (MXC_BASE_* and the IRQn_Type enum).
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "system/address-spaces.h"
#include "system/system.h"
#include "hw/arm/max32650_soc.h"
#include "hw/qdev-clock.h"
#include "hw/misc/unimp.h"
#include "hw/net/adin1110.h"

#define MAX32650_ICC0_ADDR 0x4002a000

static const uint32_t max32650_uart_addr[] = {0x40042000, 0x40043000,
                                              0x40044000};
static const int max32650_uart_irq[] = {14, 15, 34};

static const uint32_t max32650_gpio_addr[] = {0x40008000, 0x40009000,
                                              0x4000a000};
static const int max32650_gpio_irq[] = {24, 25, 26};

static const uint32_t max32650_spi_addr[] = {0x40046000, 0x40047000};
static const int max32650_spi_irq[] = {16, 17};

#define MAX32650_TRNG_ADDR 0x400b5000
#define MAX32650_TRNG_IRQ 4

static void max32650_soc_initfn(Object *obj)
{
    MAX32650State *s = MAX32650_SOC(obj);
    int i;

    object_initialize_child(obj, "armv7m", &s->armv7m, TYPE_ARMV7M);

    object_initialize_child(obj, "gcr", &s->gcr, TYPE_MAX32650_GCR);

    for (i = 0; i < MAX32650_NUM_ICC; i++) {
        g_autofree char *name = g_strdup_printf("icc%d", i);
        object_initialize_child(obj, name, &s->icc[i], TYPE_MAX32650_ICC);
    }

    for (i = 0; i < MAX32650_NUM_UART; i++) {
        g_autofree char *name = g_strdup_printf("uart%d", i);
        object_initialize_child(obj, name, &s->uart[i], TYPE_MAX32650_UART);
    }

    object_initialize_child(obj, "trng", &s->trng, TYPE_MAX32650_TRNG);

    for (i = 0; i < MAX32650_NUM_GPIO; i++) {
        g_autofree char *name = g_strdup_printf("gpio%d", i);
        object_initialize_child(obj, name, &s->gpio[i], TYPE_MAX32650_GPIO);
    }

    for (i = 0; i < MAX32650_NUM_SPI; i++) {
        g_autofree char *name = g_strdup_printf("spi%d", i);
        object_initialize_child(obj, name, &s->spi[i], TYPE_MAX32650_SPI);
    }

    s->sysclk = qdev_init_clock_in(DEVICE(s), "sysclk", NULL, NULL, 0);
}

static void max32650_soc_realize(DeviceState *dev_soc, Error **errp)
{
    MAX32650State *s = MAX32650_SOC(dev_soc);
    MemoryRegion *system_memory = get_system_memory();
    DeviceState *dev, *gcrdev, *armv7m;
    SysBusDevice *busdev;
    Error *err = NULL;
    int i;

    if (!clock_has_source(s->sysclk)) {
        error_setg(errp, "sysclk clock must be wired up by the board code");
        return;
    }

    memory_region_init_rom(&s->flash, OBJECT(dev_soc), "MAX32650.flash",
                           MAX32650_FLASH_SIZE, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(system_memory, MAX32650_FLASH_BASE_ADDRESS,
                                &s->flash);

    memory_region_init_ram(&s->sram, NULL, "MAX32650.sram",
                           MAX32650_SRAM_SIZE, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }

    memory_region_init_ram(&s->info_mem, NULL, "MAX32650.info_mem",
                           MAX32650_INFO_MEM_SIZE, &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(system_memory, MAX32650_INFO_MEM_BASE_ADDRESS,
                                &s->info_mem);

    gcrdev = DEVICE(&s->gcr);
    object_property_set_link(OBJECT(gcrdev), "sram", OBJECT(&s->sram), &err);
    if (err != NULL) {
        error_propagate(errp, err);
        return;
    }
    memory_region_add_subregion(system_memory, MAX32650_SRAM_BASE_ADDRESS,
                                &s->sram);

    armv7m = DEVICE(&s->armv7m);

    /*
     * MXC_IRQ_EXT_COUNT in max32650.h is 97; use a slightly generous value
     * since this has not been tested against real hardware interrupt
     * timing, only the register-level boot/SPI path this project needs.
     */
    qdev_prop_set_uint32(armv7m, "num-irq", 100);
    qdev_prop_set_uint8(armv7m, "num-prio-bits", 3);
    qdev_prop_set_string(armv7m, "cpu-type", ARM_CPU_TYPE_NAME("cortex-m4"));
    qdev_prop_set_bit(armv7m, "enable-bitband", true);
    qdev_connect_clock_in(armv7m, "cpuclk", s->sysclk);
    object_property_set_link(OBJECT(&s->armv7m), "memory",
                             OBJECT(system_memory), &error_abort);
    if (!sysbus_realize(SYS_BUS_DEVICE(&s->armv7m), errp)) {
        return;
    }

    for (i = 0; i < MAX32650_NUM_ICC; i++) {
        dev = DEVICE(&(s->icc[i]));
        sysbus_realize(SYS_BUS_DEVICE(dev), errp);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, MAX32650_ICC0_ADDR);
    }

    for (i = 0; i < MAX32650_NUM_UART; i++) {
        g_autofree char *link = g_strdup_printf("uart%d", i);
        dev = DEVICE(&(s->uart[i]));
        qdev_prop_set_chr(dev, "chardev", serial_hd(i));
        if (!sysbus_realize(SYS_BUS_DEVICE(&s->uart[i]), errp)) {
            return;
        }

        object_property_set_link(OBJECT(gcrdev), link, OBJECT(dev), &err);

        busdev = SYS_BUS_DEVICE(dev);
        sysbus_mmio_map(busdev, 0, max32650_uart_addr[i]);
        sysbus_connect_irq(busdev, 0,
                           qdev_get_gpio_in(armv7m, max32650_uart_irq[i]));
    }

    dev = DEVICE(&s->trng);
    sysbus_realize(SYS_BUS_DEVICE(dev), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, MAX32650_TRNG_ADDR);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       qdev_get_gpio_in(armv7m, MAX32650_TRNG_IRQ));
    /*
     * No link to the GCR here: MAX32650's real RST0 register has no TRNG
     * reset bit (it's only clock-gated via PCLK_DIS1), so unlike this SoC's
     * UARTs, the TRNG is never reset through the GCR.
     */

    dev = DEVICE(&s->gcr);
    sysbus_realize(SYS_BUS_DEVICE(dev), errp);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, 0x40000000);

    for (i = 0; i < MAX32650_NUM_GPIO; i++) {
        dev = DEVICE(&(s->gpio[i]));
        sysbus_realize(SYS_BUS_DEVICE(dev), errp);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, max32650_gpio_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                           qdev_get_gpio_in(armv7m, max32650_gpio_irq[i]));
    }

    for (i = 0; i < MAX32650_NUM_SPI; i++) {
        dev = DEVICE(&(s->spi[i]));
        sysbus_realize(SYS_BUS_DEVICE(dev), errp);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, max32650_spi_addr[i]);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                           qdev_get_gpio_in(armv7m, max32650_spi_irq[i]));
    }

    /*
     * The adin1110 project (NO_OS_BOARD=max32650fthr) wires its ADIN1110
     * MAC-PHY to SPI1 chip-select 0 (SPI_DEVICE_ID=1, SPI_CS=0 in
     * no-OS/projects/adin1110/src/platform/maxim/parameters.h) and its
     * reset pin to GPIO0.19 (RST_GPIO_PORT/NUM in the same file).
     */
    {
        DeviceState *adin1110dev;

        adin1110dev = DEVICE(ssi_create_peripheral(max32650_spi_get_bus(&s->spi[1]),
                                                    TYPE_ADIN1110));
        qdev_connect_gpio_out_named(DEVICE(&s->spi[1]), "cs", 0,
                                    qdev_get_gpio_in_named(adin1110dev,
                                                           SSI_GPIO_CS, 0));
        qdev_connect_gpio_out(DEVICE(&s->gpio[0]), 19,
                              qdev_get_gpio_in_named(adin1110dev, "reset", 0));
    }

    /*
     * Everything else this SoC doesn't yet model as a real device. Timers,
     * I2C, DMA, ADC, watchdog, flash controller etc. are all unimplemented
     * for now -- the adin1110 boot + SPI path this project targets does not
     * require them. Extend this list (and drop entries here) as real models
     * are added.
     */
    create_unimplemented_device("systemInterface",      0x40000400, 0x400);
    create_unimplemented_device("functionControl",      0x40000800, 0x400);
    create_unimplemented_device("watchdogTimer0",       0x40003000, 0x400);
    create_unimplemented_device("dynamicVoltScale",     0x40003c00, 0x40);
    create_unimplemented_device("aeskeys",              0x40005000, 0x400);
    create_unimplemented_device("trimSystemInit",       0x40005400, 0x400);
    create_unimplemented_device("generalCtrlFunc",      0x40005800, 0x400);
    create_unimplemented_device("wakeupTimer",          0x40006400, 0x400);
    create_unimplemented_device("powerSequencer",       0x40006800, 0x400);
    create_unimplemented_device("miscControl",          0x40006c00, 0x400);

    create_unimplemented_device("rtc",                  0x40006000, 0x400);

    create_unimplemented_device("timer0",               0x40010000, 0x1000);
    create_unimplemented_device("timer1",               0x40011000, 0x1000);
    create_unimplemented_device("timer2",               0x40012000, 0x1000);
    create_unimplemented_device("timer3",               0x40013000, 0x1000);
    create_unimplemented_device("timer4",               0x40014000, 0x1000);
    create_unimplemented_device("timer5",               0x40015000, 0x1000);

    create_unimplemented_device("i2c0",                 0x4001d000, 0x1000);

    create_unimplemented_device("standardDMA",          0x40028000, 0x1000);
    create_unimplemented_device("flashController0",     0x40029000, 0x400);

    create_unimplemented_device("spi2",                 0x40048000, 0x2000);
    create_unimplemented_device("adc",                  0x40034000, 0x1000);
    create_unimplemented_device("oneWireMaster",        0x4003d000, 0x1000);

    create_unimplemented_device("i2c1",                 0x4001e000, 0x1000);
}

static void max32650_soc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->realize = max32650_soc_realize;
}

static const TypeInfo max32650_soc_info = {
    .name          = TYPE_MAX32650_SOC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(MAX32650State),
    .instance_init = max32650_soc_initfn,
    .class_init    = max32650_soc_class_init,
};

static void max32650_soc_types(void)
{
    type_register_static(&max32650_soc_info);
}

type_init(max32650_soc_types)
