/*
 * MAX32650 True Random Number Generator
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "trace.h"
#include "hw/irq.h"
#include "migration/vmstate.h"
#include "hw/misc/max32650_trng.h"
#include "qemu/guest-random.h"

static uint64_t max32650_trng_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    Max32650TrngState *s = opaque;
    uint32_t data;

    switch (addr) {
    case TRNG_CTRL:
        /* A random value is always available */
        return s->ctrl | TRNG_CTRL_RNG_IS;

    case TRNG_DATA:
        /*
         * When interrupts are enabled, reading random data should cause a
         * new interrupt to be generated; since there's always a random
         * number available, we could qemu_set_irq(s->irq, s->ctrl &
         * TRNG_CTRL_RNG_IE). Because of how trng_write is set up, this is
         * always a noop, so don't.
         */
        qemu_guest_getrandom_nofail(&data, sizeof(data));
        return data;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"
            HWADDR_PRIx "\n", __func__, addr);
        break;
    }
    return 0;
}

static void max32650_trng_write(void *opaque, hwaddr addr,
                                 uint64_t val64, unsigned int size)
{
    Max32650TrngState *s = opaque;
    uint32_t val = val64;

    switch (addr) {
    case TRNG_CTRL:
        s->ctrl = val & ~TRNG_CTRL_RNG_ISC;

        /*
         * This device models random number generation as taking 0 time.
         * A new random number is always available, so the condition for
         * the RND interrupt is always fulfilled; we can just set irq to 1.
         */
        if (val & TRNG_CTRL_RNG_IE) {
            qemu_set_irq(s->irq, 1);
        } else {
            qemu_set_irq(s->irq, 0);
        }
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad offset 0x%"
            HWADDR_PRIx "\n", __func__, addr);
        break;
    }
}

static void max32650_trng_reset_hold(Object *obj, ResetType type)
{
    Max32650TrngState *s = MAX32650_TRNG(obj);
    s->ctrl = 0;
}

static const MemoryRegionOps max32650_trng_ops = {
    .read = max32650_trng_read,
    .write = max32650_trng_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription max32650_trng_vmstate = {
    .name = TYPE_MAX32650_TRNG,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ctrl, Max32650TrngState),
        VMSTATE_END_OF_LIST()
    }
};

static void max32650_trng_init(Object *obj)
{
    Max32650TrngState *s = MAX32650_TRNG(obj);
    sysbus_init_irq(SYS_BUS_DEVICE(obj), &s->irq);

    memory_region_init_io(&s->mmio, obj, &max32650_trng_ops, s,
                        TYPE_MAX32650_TRNG, 0x1000);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void max32650_trng_class_init(ObjectClass *klass, const void *data)
{
    ResettableClass *rc = RESETTABLE_CLASS(klass);
    DeviceClass *dc = DEVICE_CLASS(klass);

    rc->phases.hold = max32650_trng_reset_hold;
    dc->vmsd = &max32650_trng_vmstate;
}

static const TypeInfo max32650_trng_info = {
    .name          = TYPE_MAX32650_TRNG,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max32650TrngState),
    .instance_init = max32650_trng_init,
    .class_init    = max32650_trng_class_init,
};

static void max32650_trng_register_types(void)
{
    type_register_static(&max32650_trng_info);
}

type_init(max32650_trng_register_types)
