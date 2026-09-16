/*
 * MAX32650 Instruction Cache
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
#include "hw/misc/max32650_icc.h"

static uint64_t max32650_icc_read(void *opaque, hwaddr addr,
                                   unsigned int size)
{
    Max32650IccState *s = opaque;

    switch (addr) {
    case ICC_INFO:
        return s->info;

    case ICC_SZ:
        return s->sz;

    case ICC_CTRL:
        return s->ctrl;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        return 0;
    }
}

static void max32650_icc_write(void *opaque, hwaddr addr,
                                uint64_t val64, unsigned int size)
{
    Max32650IccState *s = opaque;

    switch (addr) {
    case ICC_CTRL:
        s->ctrl = 0x10000 | (val64 & 1);
        break;

    case ICC_INVALIDATE:
        break;

    default:
        qemu_log_mask(LOG_GUEST_ERROR,
                      "%s: Bad offset 0x%" HWADDR_PRIx "\n",
                      __func__, addr);
        break;
    }
}

static const MemoryRegionOps max32650_icc_ops = {
    .read = max32650_icc_read,
    .write = max32650_icc_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
};

static const VMStateDescription max32650_icc_vmstate = {
    .name = TYPE_MAX32650_ICC,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(info, Max32650IccState),
        VMSTATE_UINT32(sz, Max32650IccState),
        VMSTATE_UINT32(ctrl, Max32650IccState),
        VMSTATE_END_OF_LIST()
    }
};

static void max32650_icc_reset_hold(Object *obj, ResetType type)
{
    Max32650IccState *s = MAX32650_ICC(obj);
    s->info = 0;
    s->sz = 0x10000010;
    s->ctrl = 0x10000;
}

static void max32650_icc_init(Object *obj)
{
    Max32650IccState *s = MAX32650_ICC(obj);

    memory_region_init_io(&s->mmio, obj, &max32650_icc_ops, s,
                        TYPE_MAX32650_ICC, 0x800);
    sysbus_init_mmio(SYS_BUS_DEVICE(obj), &s->mmio);
}

static void max32650_icc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    rc->phases.hold = max32650_icc_reset_hold;
    dc->vmsd = &max32650_icc_vmstate;
}

static const TypeInfo max32650_icc_info = {
    .name          = TYPE_MAX32650_ICC,
    .parent        = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(Max32650IccState),
    .instance_init = max32650_icc_init,
    .class_init    = max32650_icc_class_init,
};

static void max32650_icc_register_types(void)
{
    type_register_static(&max32650_icc_info);
}

type_init(max32650_icc_register_types)
