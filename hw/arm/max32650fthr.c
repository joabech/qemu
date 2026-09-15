/*
 * MAX32650FTHR Evaluation Board
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * platform-sdk local addition, modeled on hw/arm/max78000fthr.c. Targets
 * no-OS's `adin1110` project (NO_OS_BOARD=max32650fthr).
 */

#include "qemu/osdep.h"
#include "qapi/error.h"
#include "hw/boards.h"
#include "hw/qdev-properties.h"
#include "hw/qdev-clock.h"
#include "qemu/error-report.h"
#include "hw/arm/machines-qom.h"
#include "hw/arm/max32650_soc.h"
#include "hw/arm/boot.h"

/* 96MHz is MAX32650's default IPO clock; matches msdk's SystemInit(). */
#define SYSCLK_FRQ 96000000ULL

static void max32650_init(MachineState *machine)
{
    DeviceState *dev;
    Clock *sysclk;

    sysclk = clock_new(OBJECT(machine), "SYSCLK");
    clock_set_hz(sysclk, SYSCLK_FRQ);

    dev = qdev_new(TYPE_MAX32650_SOC);
    object_property_add_child(OBJECT(machine), "soc", OBJECT(dev));
    qdev_connect_clock_in(dev, "sysclk", sysclk);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);

    armv7m_load_kernel(ARM_CPU(first_cpu),
                       machine->kernel_filename,
                       0x00000000, MAX32650_FLASH_SIZE);
}

static void max32650_machine_init(MachineClass *mc)
{
    static const char * const valid_cpu_types[] = {
        ARM_CPU_TYPE_NAME("cortex-m4"),
        NULL
    };

    mc->desc = "MAX32650FTHR Board (Cortex-M4), platform-sdk local addition";
    mc->init = max32650_init;
    mc->valid_cpu_types = valid_cpu_types;
}

DEFINE_MACHINE_ARM("max32650fthr", max32650_machine_init)
