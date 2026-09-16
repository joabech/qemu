/*
 * Generic SSI loopback peripheral
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * platform-sdk local addition. Stands in for a MISO/MOSI jumper wire: some
 * firmware self-tests (e.g. msdk's SPI example) assume the two lines of one
 * SPI instance are physically shorted together and never touch a real
 * peripheral at all. Nothing in QEMU's SSI framework models a bare wire, so
 * this device does the equivalent at the byte level -- echo back whatever
 * byte a master just sent, unconditionally, regardless of chip-select.
 *
 * Not wired into any machine unconditionally; a board attaches it to a
 * specific SPI instance's bus only when asked (see max32650-soc's
 * "spi0-loopback" property), so it never intercepts traffic meant for a
 * real peripheral on the same bus.
 */

#include "qemu/osdep.h"
#include "hw/ssi/ssi.h"
#include "hw/ssi/ssi-loopback.h"
#include "qom/object.h"

OBJECT_DECLARE_SIMPLE_TYPE(SSILoopbackState, SSI_LOOPBACK)

struct SSILoopbackState {
    SSIPeripheral parent_obj;
};

static void ssi_loopback_realize(SSIPeripheral *dev, Error **errp)
{
}

static uint32_t ssi_loopback_transfer(SSIPeripheral *dev, uint32_t val)
{
    return val;
}

static void ssi_loopback_class_init(ObjectClass *klass, const void *data)
{
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);

    k->realize = ssi_loopback_realize;
    k->transfer = ssi_loopback_transfer;
    k->cs_polarity = SSI_CS_NONE;
}

static const TypeInfo ssi_loopback_info = {
    .name          = TYPE_SSI_LOOPBACK,
    .parent        = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(SSILoopbackState),
    .class_init    = ssi_loopback_class_init,
};

static void ssi_loopback_register_types(void)
{
    type_register_static(&ssi_loopback_info);
}

type_init(ssi_loopback_register_types)
