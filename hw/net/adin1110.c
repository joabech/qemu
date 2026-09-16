/*
 * ADIN1110 Robust, Low Power 10BASE-T1L MAC-PHY -- SPI slave model
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * See include/hw/net/adin1110.h for the protocol this implements.
 */

#include "qemu/osdep.h"
#include "qemu/log.h"
#include "hw/irq.h"
#include "hw/qdev-properties.h"
#include "migration/vmstate.h"
#include "hw/net/adin1110.h"

static void adin1110_reset_regs(Adin1110State *s)
{
    s->pos = 0;
    s->wlen = 0;
    s->rw = false;
    s->addr = 0;

    s->config1 = 0;
    s->config2 = 0;
    s->status0 = 0;
    s->status1 = 0;
    s->imask1 = 0;
    s->reset = 0;
    s->soft_rst = 0;
    s->mac_addr_mask_upr = 0;
    s->mac_addr_mask_lwr = 0;

    memset(s->mdioacc, 0, sizeof(s->mdioacc));
    s->mdio_c45_reg = 0;
    /* Not powered down: adin1110_setup_phy()'s wait-for-clear loop then
     * has nothing to wait for. */
    s->crsm_sft_pd_ctrl = 0;

    s->tx_fsize = 0;

    memset(s->mac_addr_filt_upr, 0, sizeof(s->mac_addr_filt_upr));
    memset(s->mac_addr_filt_lwr, 0, sizeof(s->mac_addr_filt_lwr));

    s->rx_frm_cnt = 0;
    s->rx_bcast_cnt = 0;
    s->rx_mcast_cnt = 0;
    s->rx_ucast_cnt = 0;
    s->tx_frm_cnt = 0;
    s->tx_bcast_cnt = 0;
    s->tx_mcast_cnt = 0;
    s->tx_ucast_cnt = 0;

    s->rx_frame_len = 0;
    s->rx_frame_pending = false;
}

static void adin1110_mdioacc_write(Adin1110State *s, int idx, uint32_t val)
{
    uint32_t op = (val & ADIN1110_MDIO_OP_MASK) >> ADIN1110_MDIO_OP_POS;
    uint32_t data = val & ADIN1110_MDIO_DATA_MASK;
    uint32_t result = 0;

    /*
     * Only clause 45 ADDR/WR/RD of the CRSM soft-power-down register is
     * ever exercised by this project (adin1110_setup_phy()) -- everything
     * else just completes with a 0 result.
     */
    switch (op) {
    case ADIN1110_MDIO_OP_ADDR:
        s->mdio_c45_reg = data;
        break;
    case ADIN1110_MDIO_OP_WR:
        if (s->mdio_c45_reg == ADIN1110_CRSM_SFT_PD_CNTRL_REG) {
            s->crsm_sft_pd_ctrl = data & ADIN1110_CRSM_SFT_PD_MASK;
        }
        break;
    case ADIN1110_MDIO_OP_RD:
        if (s->mdio_c45_reg == ADIN1110_CRSM_SFT_PD_CNTRL_REG) {
            result = s->crsm_sft_pd_ctrl;
        }
        break;
    default:
        break;
    }

    /* The transaction always completes synchronously, so TRDONE is set
     * unconditionally on the next read -- see adin1110_reg_read_value(). */
    s->mdioacc[idx] = result;
}

static uint32_t adin1110_reg_read_value(Adin1110State *s, uint16_t addr)
{
    int i;

    switch (addr) {
    case ADIN1110_REG_PHY_ID:
        return ADIN1110_PHY_ID_VALUE;
    case ADIN1110_REG_CONFIG1:
        return s->config1;
    case ADIN1110_REG_CONFIG2:
        return s->config2;
    case ADIN1110_REG_STATUS0:
        /* Reset is always already complete by the time the guest can poll
         * this (this device has no real power-up delay). */
        return s->status0 | ADIN1110_STATUS0_RESETC;
    case ADIN1110_REG_STATUS1:
        /* Link is always reported up -- there is no real PHY link to
         * negotiate here. */
        return s->status1 | ADIN1110_STATUS1_LINK_STATE;
    case ADIN1110_REG_IMASK1:
        return s->imask1;
    case ADIN1110_REG_MDIOACC0:
        return ADIN1110_MDIO_TRDONE | (s->mdioacc[0] & ADIN1110_MDIO_DATA_MASK);
    case ADIN1110_REG_MDIOACC1:
        return ADIN1110_MDIO_TRDONE | (s->mdioacc[1] & ADIN1110_MDIO_DATA_MASK);
    case ADIN1110_REG_TX_FSIZE:
        return s->tx_fsize;
    case ADIN1110_REG_TX_SPACE:
        /* Constant: transfers complete synchronously within the SPI write
         * that triggers them, so the TX FIFO is always empty/at its
         * power-on-reset value (0xFFF, per the datasheet) by the time the
         * guest can observe it. */
        return 0xFFF;
    case ADIN1110_REG_MAC_RST_STATUS:
        return 1;
    case ADIN1110_REG_RX_FSIZE:
        return s->rx_frame_pending ? s->rx_frame_len + 2 : 0;
    case ADIN1110_REG_RX_FRM_CNT:
        return s->rx_frm_cnt;
    case ADIN1110_REG_RX_BCAST_CNT:
        return s->rx_bcast_cnt;
    case ADIN1110_REG_RX_MCAST_CNT:
        return s->rx_mcast_cnt;
    case ADIN1110_REG_RX_UCAST_CNT:
        return s->rx_ucast_cnt;
    case ADIN1110_REG_TX_FRM_CNT:
        return s->tx_frm_cnt;
    case ADIN1110_REG_TX_BCAST_CNT:
        return s->tx_bcast_cnt;
    case ADIN1110_REG_TX_MCAST_CNT:
        return s->tx_mcast_cnt;
    case ADIN1110_REG_TX_UCAST_CNT:
        return s->tx_ucast_cnt;
    /* Real, valid registers this model doesn't track separately -- always
     * read back as 0 rather than falling into the guest-error default. */
    case ADIN1110_REG_RX_CRC_ERR_CNT:
    case ADIN1110_REG_RX_ALGN_ERR_CNT:
    case ADIN1110_REG_RX_LS_ERR_CNT:
    case ADIN1110_REG_RX_PHY_ERR_CNT:
    case ADIN1110_REG_RX_DROP_FULL_CNT:
    case ADIN1110_REG_RX_DROP_FILT_CNT:
    case ADIN1110_REG_MAC_ADDR_MASK_UPR:
        return 0;
    case ADIN1110_REG_RESET:
        return s->reset;
    default:
        break;
    }

    for (i = 0; i < ADIN1110_NUM_MAC_ADDR_FILT; i++) {
        if (addr == ADIN1110_REG_MAC_ADDR_FILT_UPR(i)) {
            return s->mac_addr_filt_upr[i];
        }
        if (addr == ADIN1110_REG_MAC_ADDR_FILT_LWR(i)) {
            return s->mac_addr_filt_lwr[i];
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad register address 0x%x\n",
                  __func__, addr);
    return 0;
}

static void adin1110_reg_write_value(Adin1110State *s, uint16_t addr,
                                      uint32_t val)
{
    int i;

    switch (addr) {
    case ADIN1110_REG_CONFIG1:
        s->config1 = val;
        return;
    case ADIN1110_REG_CONFIG2:
        s->config2 = val;
        return;
    case ADIN1110_REG_STATUS0:
        /* R/W1C */
        s->status0 &= ~val;
        return;
    case ADIN1110_REG_STATUS1:
        s->status1 &= ~val;
        return;
    case ADIN1110_REG_IMASK1:
        s->imask1 = val;
        return;
    case ADIN1110_REG_RESET:
        s->reset = val;
        return;
    case ADIN1110_REG_SOFT_RST:
        s->soft_rst = val;
        return;
    case ADIN1110_REG_MDIOACC0:
        adin1110_mdioacc_write(s, 0, val);
        return;
    case ADIN1110_REG_MDIOACC1:
        adin1110_mdioacc_write(s, 1, val);
        return;
    case ADIN1110_REG_TX_FSIZE:
        s->tx_fsize = val;
        return;
    case ADIN1110_REG_FIFO_CLR:
        if (val & 1) {
            s->rx_frame_pending = false;
            s->rx_frame_len = 0;
        }
        return;
    case ADIN1110_REG_MAC_ADDR_MASK_UPR:
        s->mac_addr_mask_upr = val;
        return;
    case ADIN1110_REG_MAC_ADDR_MASK_LWR:
        s->mac_addr_mask_lwr = val;
        return;
    default:
        break;
    }

    for (i = 0; i < ADIN1110_NUM_MAC_ADDR_FILT; i++) {
        if (addr == ADIN1110_REG_MAC_ADDR_FILT_UPR(i)) {
            s->mac_addr_filt_upr[i] = val;
            return;
        }
        if (addr == ADIN1110_REG_MAC_ADDR_FILT_LWR(i)) {
            s->mac_addr_filt_lwr[i] = val;
            return;
        }
    }

    qemu_log_mask(LOG_GUEST_ERROR, "%s: Bad register address 0x%x\n",
                  __func__, addr);
}

/*
 * Commit a completed TX_REG write: no-OS's adin1110_write_fifo() precedes
 * it with a TX_FSIZE_REG write giving the true frame length (payload + the
 * 2-byte frame header), so wbuf[0:1] is that header (port number for this
 * single-port board -- ignored) and wbuf[2:tx_fsize-1] is the real,
 * unpadded Ethernet frame (see the datasheet's Table 22-25 and
 * adin1110_write_fifo() in no-OS/drivers/net/adin1110/adin1110.c).
 *
 * No host networking backend exists yet, so the "MAC" here just loops the
 * frame back: whatever the guest transmits becomes the next frame it
 * receives, which is enough to validate the SPI framing end-to-end.
 */
static void adin1110_commit_tx(Adin1110State *s)
{
    uint32_t frame_len;

    if (s->tx_fsize < 2 || s->wlen < 2) {
        return;
    }

    frame_len = s->tx_fsize - 2;
    frame_len = MIN(frame_len, s->wlen - 2);
    frame_len = MIN(frame_len, ADIN1110_MAX_FRAME_LEN);

    if (frame_len < 6) {
        return;
    }

    memcpy(s->rx_frame, &s->wbuf[2], frame_len);
    s->rx_frame_len = frame_len;
    s->rx_frame_pending = true;

    s->tx_frm_cnt++;
    if (memcmp(&s->wbuf[2], "\xff\xff\xff\xff\xff\xff", 6) == 0) {
        s->tx_bcast_cnt++;
    } else if (s->wbuf[2] & 0x1) {
        s->tx_mcast_cnt++;
    } else {
        s->tx_ucast_cnt++;
    }

    s->rx_frm_cnt++;
    if (memcmp(&s->wbuf[2], "\xff\xff\xff\xff\xff\xff", 6) == 0) {
        s->rx_bcast_cnt++;
    } else if (s->wbuf[2] & 0x1) {
        s->rx_mcast_cnt++;
    } else {
        s->rx_ucast_cnt++;
    }
}

static uint8_t adin1110_read_data_byte(Adin1110State *s, uint32_t data_pos)
{
    if (s->addr == ADIN1110_REG_RX) {
        /*
         * data_pos 0:1 is the 2-byte frame header preceding the Ethernet
         * frame in the RX_REG data phase (the read-side counterpart of
         * TX_REG's port header, see adin1110_commit_tx()) -- the no-OS
         * driver never inspects it, so its value doesn't matter.
         */
        if (data_pos < 2) {
            return 0;
        }
        data_pos -= 2;
        if (data_pos < s->rx_frame_len) {
            return s->rx_frame[data_pos];
        }
        return 0;
    }

    if (data_pos < 4) {
        uint32_t val = adin1110_reg_read_value(s, s->addr);
        return (val >> (24 - 8 * data_pos)) & 0xff;
    }

    return 0;
}

static uint32_t adin1110_transfer(SSIPeripheral *ssidev, uint32_t tx)
{
    Adin1110State *s = ADIN1110(ssidev);
    uint8_t byte = tx & 0xff;
    uint32_t data_pos;

    if (s->pos < 2) {
        s->header[s->pos] = byte;
        if (s->pos == 1) {
            uint16_t hdr = ((uint16_t)s->header[0] << 8) | s->header[1];

            s->rw = (hdr & (1 << 13)) != 0;
            s->addr = hdr & 0x1fff;
            s->wlen = 0;
        }
        s->pos++;
        return 0;
    }

    if (!s->rw && s->pos == 2) {
        /* Turnaround byte before read data starts. */
        s->pos++;
        return 0;
    }

    data_pos = s->rw ? (s->pos - 2) : (s->pos - 3);
    s->pos++;

    if (s->rw) {
        if (data_pos < sizeof(s->wbuf)) {
            s->wbuf[data_pos] = byte;
            s->wlen = data_pos + 1;
        }
        return 0;
    }

    return adin1110_read_data_byte(s, data_pos);
}

static int adin1110_set_cs(SSIPeripheral *ssidev, bool select)
{
    Adin1110State *s = ADIN1110(ssidev);
    /*
     * ssi_cs_default()/ssi_transfer_raw_default() (hw/ssi/ssi.c) pass this
     * callback the raw cs line level, not "is selected" -- for this
     * device's SSI_CS_LOW polarity, selected means the line is driven low,
     * i.e. select == false.
     */
    bool selected = !select;

    if (selected) {
        s->pos = 0;
        s->wlen = 0;
    } else if (s->rw && s->addr == ADIN1110_REG_TX && s->wlen > 0) {
        adin1110_commit_tx(s);
    } else if (s->rw && s->addr != ADIN1110_REG_TX && s->wlen >= 4) {
        uint32_t val = ((uint32_t)s->wbuf[0] << 24) | (s->wbuf[1] << 16) |
                       (s->wbuf[2] << 8) | s->wbuf[3];
        adin1110_reg_write_value(s, s->addr, val);
    }

    return 0;
}

static void adin1110_reset_pin(void *opaque, int line, int level)
{
    Adin1110State *s = ADIN1110(opaque);
    bool new_level = !!level;

    /*
     * Only act on an actual 0->1 transition -- see the "reset_level"
     * comment in the header for why this can't just check "if (level)".
     */
    if (new_level && !s->reset_level) {
        /*
         * Rising edge: the reset pin (wired from a MAX32650 GPIO output,
         * see max32650_soc.c) has been released. adin1110_phy_reset()
         * waits 90ms after this and then expects to read back a valid
         * PHY_ID immediately, so reinitialize register state to power-on
         * defaults right here rather than modeling that delay.
         */
        adin1110_reset_regs(s);
    }

    s->reset_level = new_level;
}

static void adin1110_reset_hold(Object *obj, ResetType type)
{
    adin1110_reset_regs(ADIN1110(obj));
}

static void adin1110_realize(SSIPeripheral *dev, Error **errp)
{
    qdev_init_gpio_in_named(DEVICE(dev), adin1110_reset_pin, "reset", 1);
}

static const VMStateDescription vmstate_adin1110 = {
    .name = TYPE_ADIN1110,
    .version_id = 1,
    .minimum_version_id = 1,
    .fields = (const VMStateField[]) {
        VMSTATE_SSI_PERIPHERAL(parent_obj, Adin1110State),
        VMSTATE_UINT32(pos, Adin1110State),
        VMSTATE_UINT8_ARRAY(header, Adin1110State, 2),
        VMSTATE_BOOL(rw, Adin1110State),
        VMSTATE_UINT16(addr, Adin1110State),
        VMSTATE_UINT8_ARRAY(wbuf, Adin1110State, ADIN1110_MAX_FRAME_LEN),
        VMSTATE_UINT32(wlen, Adin1110State),
        VMSTATE_UINT32(config1, Adin1110State),
        VMSTATE_UINT32(config2, Adin1110State),
        VMSTATE_UINT32(status0, Adin1110State),
        VMSTATE_UINT32(status1, Adin1110State),
        VMSTATE_UINT32(imask1, Adin1110State),
        VMSTATE_UINT32(reset, Adin1110State),
        VMSTATE_UINT32(soft_rst, Adin1110State),
        VMSTATE_UINT32(mac_addr_mask_upr, Adin1110State),
        VMSTATE_UINT32(mac_addr_mask_lwr, Adin1110State),
        VMSTATE_UINT32_ARRAY(mdioacc, Adin1110State, 2),
        VMSTATE_UINT16(mdio_c45_reg, Adin1110State),
        VMSTATE_UINT32(crsm_sft_pd_ctrl, Adin1110State),
        VMSTATE_UINT32(tx_fsize, Adin1110State),
        VMSTATE_UINT32_ARRAY(mac_addr_filt_upr, Adin1110State,
                             ADIN1110_NUM_MAC_ADDR_FILT),
        VMSTATE_UINT32_ARRAY(mac_addr_filt_lwr, Adin1110State,
                             ADIN1110_NUM_MAC_ADDR_FILT),
        VMSTATE_UINT32(rx_frm_cnt, Adin1110State),
        VMSTATE_UINT32(rx_bcast_cnt, Adin1110State),
        VMSTATE_UINT32(rx_mcast_cnt, Adin1110State),
        VMSTATE_UINT32(rx_ucast_cnt, Adin1110State),
        VMSTATE_UINT32(tx_frm_cnt, Adin1110State),
        VMSTATE_UINT32(tx_bcast_cnt, Adin1110State),
        VMSTATE_UINT32(tx_mcast_cnt, Adin1110State),
        VMSTATE_UINT32(tx_ucast_cnt, Adin1110State),
        VMSTATE_UINT8_ARRAY(rx_frame, Adin1110State, ADIN1110_MAX_FRAME_LEN),
        VMSTATE_UINT32(rx_frame_len, Adin1110State),
        VMSTATE_BOOL(rx_frame_pending, Adin1110State),
        VMSTATE_END_OF_LIST()
    }
};

static void adin1110_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    SSIPeripheralClass *k = SSI_PERIPHERAL_CLASS(klass);
    ResettableClass *rc = RESETTABLE_CLASS(klass);

    k->realize = adin1110_realize;
    k->transfer = adin1110_transfer;
    k->set_cs = adin1110_set_cs;
    k->cs_polarity = SSI_CS_LOW;
    rc->phases.hold = adin1110_reset_hold;
    dc->vmsd = &vmstate_adin1110;
}

static const TypeInfo adin1110_info = {
    .name          = TYPE_ADIN1110,
    .parent        = TYPE_SSI_PERIPHERAL,
    .instance_size = sizeof(Adin1110State),
    .class_init    = adin1110_class_init,
};

static void adin1110_register_types(void)
{
    type_register_static(&adin1110_info);
}

type_init(adin1110_register_types)
