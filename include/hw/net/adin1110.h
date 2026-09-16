/*
 * ADIN1110 Robust, Low Power 10BASE-T1L MAC-PHY -- SPI slave model
 *
 * platform-sdk local addition
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Models the "Generic SPI" register protocol from the ADIN1110 datasheet
 * (Rev. C, "MAC SPI" section, Table 22-25): a 16-bit header
 * (CD=1, fixed-0, R/W, ADDR[12:0]) big-endian, followed for writes by N
 * 32-bit big-endian data words, or for reads by a 1-byte turnaround then N
 * 32-bit big-endian data words -- no CRC (this project's no-OS driver runs
 * with append_crc=false and oa_tc6_spi=false, i.e. neither the optional
 * per-word CRC nor the separate Open Alliance TC6 chunked-datagram
 * protocol is in use here; see no-OS/drivers/net/adin1110/adin1110.c's
 * adin1110_standard_spi_reg_read/write()).
 *
 * TX_REG/RX_REG (the frame FIFOs) are addressed exactly like any other
 * register in this protocol -- a write/read of arbitrary length instead of
 * a fixed 4 bytes. There is no host networking backend yet: a written
 * frame is simply queued as the next frame available to read back (see
 * adin1110_commit_tx() in adin1110.c), which is enough to validate the SPI
 * framing end-to-end and is documented as Phase 3's first milestone in
 * ~/.claude/plans/cheeky-watching-haven.md. Bridging to a real QEMU netdev
 * is a tracked follow-up, not implemented here.
 */
#ifndef HW_NET_ADIN1110_H
#define HW_NET_ADIN1110_H

#include "hw/ssi/ssi.h"
#include "qom/object.h"

#define TYPE_ADIN1110 "adin1110"
OBJECT_DECLARE_SIMPLE_TYPE(Adin1110State, ADIN1110)

/* Register addresses (13-bit ADDR field), matching
 * no-OS/drivers/net/adin1110/adin1110.h's ADIN1110_*_REG constants.
 */
#define ADIN1110_REG_RESET             0x03
#define ADIN1110_REG_PHY_ID            0x01
#define ADIN1110_REG_CONFIG1           0x04
#define ADIN1110_REG_CONFIG2           0x06
#define ADIN1110_REG_STATUS0           0x08
#define ADIN1110_REG_STATUS1           0x09
#define ADIN1110_REG_IMASK1            0x0D
#define ADIN1110_REG_MDIOACC0          0x20
#define ADIN1110_REG_MDIOACC1          0x21
#define ADIN1110_REG_TX_FSIZE          0x30
#define ADIN1110_REG_TX                0x31
#define ADIN1110_REG_TX_SPACE          0x32
#define ADIN1110_REG_FIFO_CLR          0x36
#define ADIN1110_REG_MAC_RST_STATUS    0x3B
#define ADIN1110_REG_SOFT_RST          0x3C
#define ADIN1110_REG_MAC_ADDR_FILT_UPR(i)  (0x50 + 2 * (i))
#define ADIN1110_REG_MAC_ADDR_FILT_LWR(i)  (0x51 + 2 * (i))
#define ADIN1110_REG_MAC_ADDR_MASK_UPR 0x70
#define ADIN1110_REG_MAC_ADDR_MASK_LWR 0x71
#define ADIN1110_REG_RX_FSIZE          0x90
#define ADIN1110_REG_RX                0x91
#define ADIN1110_REG_RX_FRM_CNT        0xA0
#define ADIN1110_REG_RX_BCAST_CNT      0xA1
#define ADIN1110_REG_RX_MCAST_CNT      0xA2
#define ADIN1110_REG_RX_UCAST_CNT      0xA3
#define ADIN1110_REG_RX_CRC_ERR_CNT    0xA4
#define ADIN1110_REG_RX_ALGN_ERR_CNT   0xA5
#define ADIN1110_REG_RX_LS_ERR_CNT     0xA6
#define ADIN1110_REG_RX_PHY_ERR_CNT    0xA7
#define ADIN1110_REG_TX_FRM_CNT        0xA8
#define ADIN1110_REG_TX_BCAST_CNT      0xA9
#define ADIN1110_REG_TX_MCAST_CNT      0xAA
#define ADIN1110_REG_TX_UCAST_CNT      0xAB
#define ADIN1110_REG_RX_DROP_FULL_CNT  0xAC
#define ADIN1110_REG_RX_DROP_FILT_CNT  0xAD
#define ADIN1110_NUM_MAC_ADDR_FILT     16

/* STATUS0 */
#define ADIN1110_STATUS0_RESETC (1 << 6)

/* STATUS1 */
#define ADIN1110_STATUS1_LINK_STATE (1 << 0)

/* This driver only ever probes 0x0283BCA1 (ADIN2111_PHY_ID): the no-OS
 * project's frame_rx_tx example sets .chip_type = ADIN2111 even though
 * this board has a single ADIN1110 -- see
 * no-OS/projects/adin1110/src/examples/frame_rx_tx/frame_rx_tx_example.c.
 */
#define ADIN1110_PHY_ID_VALUE 0x0283BCA1

/* MDIOACC (clause 22/45 MDIO-over-SPI bridge) */
#define ADIN1110_MDIO_TRDONE (1U << 31)
#define ADIN1110_MDIO_ST_POS 28
#define ADIN1110_MDIO_OP_POS 26
#define ADIN1110_MDIO_OP_MASK (0x3 << ADIN1110_MDIO_OP_POS)
#define ADIN1110_MDIO_OP_ADDR 0x0
#define ADIN1110_MDIO_OP_WR   0x1
#define ADIN1110_MDIO_OP_RD   0x3
#define ADIN1110_MDIO_DATA_MASK 0xFFFF

/* The only MMD register this driver's PHY setup ever touches (clause 45,
 * MMD 0x1E "CRSM", reg 0x8812) -- see adin1110_setup_phy() in
 * no-OS/drivers/net/adin1110/adin1110.c. Nothing else needs a real MMD
 * register file behind this model.
 */
#define ADIN1110_CRSM_SFT_PD_CNTRL_REG 0x8812
#define ADIN1110_CRSM_SFT_PD_MASK (1 << 0)

#define ADIN1110_MAX_FRAME_LEN 1530

struct Adin1110State {
    SSIPeripheral parent_obj;

    /* Header/turnaround byte-position state machine for the transaction
     * currently framed by the cs line (reset by adin1110_set_cs()). Wide
     * enough for a full ADIN1110_MAX_FRAME_LEN-sized burst -- a uint8_t
     * wraps mid-frame for anything over 255 bytes, which a real Ethernet
     * frame write routinely exceeds. */
    uint32_t pos;
    uint8_t header[2];
    bool rw;
    uint16_t addr;

    /* Accumulated write data for the transaction currently in progress,
     * committed in adin1110_set_cs() once cs deasserts. */
    uint8_t wbuf[ADIN1110_MAX_FRAME_LEN];
    uint32_t wlen;

    /*
     * Last level seen on the "reset" gpio-in (see adin1110_reset_pin()).
     * qemu_irq lines carry a level, not an edge -- QEMU re-invokes a GPIO
     * input handler with the *same* level on every write to the driving
     * GPIO bank's registers (max32650_gpio_update() recomputes and
     * re-asserts every output line on nearly any register write, not just
     * ones that actually change that pin), so this device must track the
     * previous level itself to tell a real reset-release edge apart from
     * routine noise on an already-released reset line.
     */
    bool reset_level;

    uint32_t config1;
    uint32_t config2;
    uint32_t status0;
    uint32_t status1;
    uint32_t imask1;
    uint32_t reset;
    uint32_t soft_rst;
    uint32_t mac_addr_mask_upr;
    uint32_t mac_addr_mask_lwr;

    uint32_t mdioacc[2];
    uint16_t mdio_c45_reg;
    uint32_t crsm_sft_pd_ctrl;

    uint32_t tx_fsize;

    uint32_t mac_addr_filt_upr[ADIN1110_NUM_MAC_ADDR_FILT];
    uint32_t mac_addr_filt_lwr[ADIN1110_NUM_MAC_ADDR_FILT];

    uint32_t rx_frm_cnt;
    uint32_t rx_bcast_cnt;
    uint32_t rx_mcast_cnt;
    uint32_t rx_ucast_cnt;
    uint32_t tx_frm_cnt;
    uint32_t tx_bcast_cnt;
    uint32_t tx_mcast_cnt;
    uint32_t tx_ucast_cnt;

    /* The single pending "received" frame -- populated by a TX_REG write
     * (loopback: what the host sends becomes what it next receives) and
     * drained by a RX_REG read. No real host networking backend yet. */
    uint8_t rx_frame[ADIN1110_MAX_FRAME_LEN];
    uint32_t rx_frame_len;
    bool rx_frame_pending;
};

#endif
