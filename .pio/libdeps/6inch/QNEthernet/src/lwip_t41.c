// SPDX-FileCopyrightText: (c) 2021-2022 Shawn Silverman <shawn@pobox.com>
// SPDX-License-Identifier: MIT

// lwip_t41.c contains the Teensy 4.1 Ethernet interface implementation.
// Based on code from Paul Stoffregen and others:
// https://github.com/PaulStoffregen/teensy41_ethernet
// This file is part of the QNEthernet library.

#if defined(ARDUINO_TEENSY41)

#include "lwip_t41.h"

// C includes
#include <string.h>

#include <core_pins.h>
#include <pgmspace.h>

#include "lwip/err.h"
#include "lwip/etharp.h"
#include "lwip/init.h"
#include "lwip/opt.h"
#include "lwip/pbuf.h"
#include "lwip/stats.h"
#include "lwip/timeouts.h"
#include "netif/ethernet.h"

// https://forum.pjrc.com/threads/60532-Teensy-4-1-Beta-Test?p=237096&viewfull=1#post237096
// https://github.com/PaulStoffregen/teensy41_ethernet/blob/master/teensy41_ethernet.ino

#define CLRSET(reg, clear, set) ((reg) = ((reg) & ~(clear)) | (set))
#define RMII_PAD_INPUT_PULLDOWN 0x30E9
#define RMII_PAD_INPUT_PULLUP   0xB0E9
#define RMII_PAD_CLOCK          0x0031

#define RX_SIZE 5
#define TX_SIZE 5
#define BUF_SIZE 1536
#define IRQ_PRIORITY 64

// Defines the control and status region of the receive buffer descriptor.
typedef enum _enet_rx_bd_control_status {
  kEnetRxBdEmpty           = 0x8000U,  // Empty bit
  kEnetRxBdRxSoftOwner1    = 0x4000U,  // Receive software ownership
  kEnetRxBdWrap            = 0x2000U,  // Update buffer descriptor
  kEnetRxBdRxSoftOwner2    = 0x1000U,  // Receive software ownership
  kEnetRxBdLast            = 0x0800U,  // Last BD in the frame (L bit)
  kEnetRxBdMiss            = 0x0100U,  // In promiscuous mode; needs L
  kEnetRxBdBroadcast       = 0x0080U,  // Broadcast
  kEnetRxBdMulticast       = 0x0040U,  // Multicast
  kEnetRxBdLengthViolation = 0x0020U,  // Receive length violation; needs L
  kEnetRxBdNonOctet        = 0x0010U,  // Receive non-octet aligned frame; needs L
  kEnetRxBdCrc             = 0x0004U,  // Receive CRC or frame error; needs L
  kEnetRxBdOverrun         = 0x0002U,  // Receive FIFO overrun; needs L
  kEnetRxBdTrunc           = 0x0001U   // Frame is truncated
} enet_rx_bd_control_status_t;

// Defines the control extended region1 of the receive buffer descriptor.
typedef enum _enet_rx_bd_control_extend0 {
  kEnetRxBdIpHeaderChecksumErr = 0x0020U,  // IP header checksum error; needs L
  kEnetRxBdProtocolChecksumErr = 0x0010U,  // Protocol checksum error; needs L
  kEnetRxBdVlan                = 0x0004U,  // VLAN; needs L
  kEnetRxBdIpv6                = 0x0002U,  // Ipv6 frame; needs L
  kEnetRxBdIpv4Fragment        = 0x0001U,  // Ipv4 fragment; needs L
} enet_rx_bd_control_extend0_t;

// Defines the control extended region2 of the receive buffer descriptor.
typedef enum _enet_rx_bd_control_extend1 {
  kEnetRxBdMacErr    = 0x8000U,  // MAC error; needs L
  kEnetRxBdPhyErr    = 0x0400U,  // PHY error; needs L
  kEnetRxBdCollision = 0x0200U,  // Collision; needs L
  kEnetRxBdUnicast   = 0x0100U,  // Unicast frame; valid even if L is not set
  kEnetRxBdInterrupt = 0x0080U,  // Generate RXB/RXF interrupt
} enet_rx_bd_control_extend1_t;

// Defines the control status of the transmit buffer descriptor.
typedef enum _enet_tx_bd_control_status {
  kEnetTxBdReady        = 0x8000U,  // Ready bit
  kEnetTxBdTxSoftOwner1 = 0x4000U,  // Transmit software ownership
  kEnetTxBdWrap         = 0x2000U,  // Wrap buffer descriptor
  kEnetTxBdTxSoftOwner2 = 0x1000U,  // Transmit software ownership
  kEnetTxBdLast         = 0x0800U,  // Last BD in the frame (L bit)
  kEnetTxBdTransmitCrc  = 0x0400U,  // Transmit CRC; needs L
} enet_tx_bd_control_status_t;

// Defines the control extended region1 of the transmit buffer descriptor.
typedef enum _enet_tx_bd_control_extend0 {
  kEnetTxBdTxErr              = 0x8000U,  // Transmit error; needs L
  kEnetTxBdTxUnderflowErr     = 0x2000U,  // Underflow error; needs L
  kEnetTxBdExcessCollisionErr = 0x1000U,  // Excess collision error; needs L
  kEnetTxBdTxFrameErr         = 0x0800U,  // Frame with error; needs L
  kEnetTxBdLatecollisionErr   = 0x0400U,  // Late collision error; needs L
  kEnetTxBdOverflowErr        = 0x0200U,  // Overflow error; needs L
  kEnetTxTimestampErr         = 0x0100U,  // Timestamp error; needs L
} enet_tx_bd_control_extend0_t;

// Defines the control extended region2 of the transmit buffer descriptor.
typedef enum _enet_tx_bd_control_extend1 {
  kEnetTxBdTxInterrupt   = 0x4000U,  // Transmit interrupt; all BDs
  kEnetTxBdTimestamp     = 0x2000U,  // Transmit timestamp flag; all BDs
  kEnetTxBdProtChecksum  = 0x1000U,  // Insert protocol specific checksum; all BDs
  kEnetTxBdIpHdrChecksum = 0x0800U,  // Insert IP header checksum; all BDs
} enet_tx_bd_control_extend1_t;

typedef struct {
  uint16_t length;
  uint16_t status;
  void *buffer;
  uint16_t extend0;
  uint16_t extend1;
  uint16_t checksum;
  uint8_t prototype;
  uint8_t headerlen;
  uint16_t unused0;
  uint16_t extend2;
  uint32_t timestamp;
  uint16_t unused1;
  uint16_t unused2;
  uint16_t unused3;
  uint16_t unused4;
} enetbufferdesc_t;

static uint8_t mac[ETH_HWADDR_LEN];
static enetbufferdesc_t rx_ring[RX_SIZE] __attribute__((aligned(64)));
static enetbufferdesc_t tx_ring[TX_SIZE] __attribute__((aligned(64)));
static uint8_t rxbufs[RX_SIZE * BUF_SIZE] __attribute__((aligned(64)));
static uint8_t txbufs[TX_SIZE * BUF_SIZE] __attribute__((aligned(64)));
volatile static enetbufferdesc_t *p_rxbd = &rx_ring[0];
volatile static enetbufferdesc_t *p_txbd = &tx_ring[0];
static struct netif t41_netif;
static volatile uint32_t rx_ready;

// PHY status, polled
static bool linkSpeed10Not100 = false;
static bool linkIsFullDuplex = false;

// Is Ethernet initialized?
static bool isInitialized = false;

void enet_isr();

// --------------------------------------------------------------------------
//  PHY_MDIO
// --------------------------------------------------------------------------

// Read a PHY register (using MDIO & MDC signals).
uint16_t mdio_read(int phyaddr, int regaddr) {
  ENET_MMFR = ENET_MMFR_ST(1) | ENET_MMFR_OP(2) | ENET_MMFR_TA(2) |
              ENET_MMFR_PA(phyaddr) | ENET_MMFR_RA(regaddr);
  // int count=0;
  while ((ENET_EIR & ENET_EIR_MII) == 0) {
    // count++; // wait
  }
  // print("mdio read waited ", count);
  uint16_t data = ENET_MMFR;
  ENET_EIR = ENET_EIR_MII;
  // printhex("mdio read:", data);
  return data;
}

// Write a PHY register (using MDIO & MDC signals).
void mdio_write(int phyaddr, int regaddr, uint16_t data) {
  ENET_MMFR = ENET_MMFR_ST(1) | ENET_MMFR_OP(1) | ENET_MMFR_TA(2) |
              ENET_MMFR_PA(phyaddr) | ENET_MMFR_RA(regaddr) |
              ENET_MMFR_DATA(data);
  // int count = 0;
  while ((ENET_EIR & ENET_EIR_MII) == 0) {
    // count++;  // wait
  }
  ENET_EIR = ENET_EIR_MII;
  // print("mdio write waited ", count);
  // printhex("mdio write :", data);
}

// --------------------------------------------------------------------------
//  Low-level
// --------------------------------------------------------------------------

static void t41_low_level_init() {
  CCM_CCGR1 |= CCM_CCGR1_ENET(CCM_CCGR_ON);
  // Configure PLL6 for 50 MHz, pg 1118 (Rev. 2, 1173 Rev. 1)
  CCM_ANALOG_PLL_ENET_SET = CCM_ANALOG_PLL_ENET_BYPASS;
  CCM_ANALOG_PLL_ENET_CLR = CCM_ANALOG_PLL_ENET_BYPASS_CLK_SRC(3) |
                            CCM_ANALOG_PLL_ENET_ENET2_DIV_SELECT(3) |
                            CCM_ANALOG_PLL_ENET_DIV_SELECT(3);
  CCM_ANALOG_PLL_ENET_SET = CCM_ANALOG_PLL_ENET_ENET_25M_REF_EN |
                            // CCM_ANALOG_PLL_ENET_ENET2_REF_EN |
                            CCM_ANALOG_PLL_ENET_ENABLE |
                            // CCM_ANALOG_PLL_ENET_ENET2_DIV_SELECT(1) |
                            CCM_ANALOG_PLL_ENET_DIV_SELECT(1);
  CCM_ANALOG_PLL_ENET_CLR = CCM_ANALOG_PLL_ENET_POWERDOWN;
  while ((CCM_ANALOG_PLL_ENET & CCM_ANALOG_PLL_ENET_LOCK) == 0) {
    // Wait for PLL lock
  }
  CCM_ANALOG_PLL_ENET_CLR = CCM_ANALOG_PLL_ENET_BYPASS;
  // printf("PLL6 = %08" PRIX32 "h (should be 80202001h)\n", CCM_ANALOG_PLL_ENET);

  // Configure REFCLK to be driven as output by PLL6, pg 329 (Rev. 2, 326 Rev. 1)
  CLRSET(IOMUXC_GPR_GPR1,
         IOMUXC_GPR_GPR1_ENET1_CLK_SEL | IOMUXC_GPR_GPR1_ENET_IPG_CLK_S_EN,
         IOMUXC_GPR_GPR1_ENET1_TX_CLK_DIR);

  // Configure pins
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_14 = 5;  // Reset    B0_14 Alt5 GPIO7.15
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B0_15 = 5;  // Power    B0_15 Alt5 GPIO7.14
  GPIO7_GDIR |= (1<<14) | (1<<15);
  GPIO7_DR_SET = (1<<15);    // Power on
  GPIO7_DR_CLEAR = (1<<14);  // Reset PHY chip
  IOMUXC_SW_PAD_CTL_PAD_GPIO_B1_04 = RMII_PAD_INPUT_PULLDOWN;  // PhyAdd[0] = 0
  IOMUXC_SW_PAD_CTL_PAD_GPIO_B1_06 = RMII_PAD_INPUT_PULLDOWN;  // PhyAdd[1] = 1
  IOMUXC_SW_PAD_CTL_PAD_GPIO_B1_05 = RMII_PAD_INPUT_PULLUP;    // Master/Slave = slave mode
  IOMUXC_SW_PAD_CTL_PAD_GPIO_B1_11 = RMII_PAD_INPUT_PULLDOWN;  // Auto MDIX Enable
  IOMUXC_SW_PAD_CTL_PAD_GPIO_B1_07 = RMII_PAD_INPUT_PULLUP;
  IOMUXC_SW_PAD_CTL_PAD_GPIO_B1_08 = RMII_PAD_INPUT_PULLUP;
  IOMUXC_SW_PAD_CTL_PAD_GPIO_B1_09 = RMII_PAD_INPUT_PULLUP;
  IOMUXC_SW_PAD_CTL_PAD_GPIO_B1_10 = RMII_PAD_CLOCK;
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_05 = 3;  // RXD1    B1_05 Alt3, pg 529 (Rev. 2, 525 Rev. 1)
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_04 = 3;  // RXD0    B1_04 Alt3, pg 528 (Rev. 2, 524 Rev. 1)
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_10 = 6 | 0x10;  // REFCLK    B1_10 Alt6, pg 534 (Rev. 2, 530 Rev. 1)
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_11 = 3;  // RXER    B1_11 Alt3, pg 535 (Rev. 2, 531 Rev. 1)
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_06 = 3;  // RXEN    B1_06 Alt3, pg 530 (Rev. 2, 526 Rev. 1)
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_09 = 3;  // TXEN    B1_09 Alt3, pg 533 (Rev. 2, 529 Rev. 1)
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_07 = 3;  // TXD0    B1_07 Alt3, pg 531 (Rev. 2, 527 Rev. 1)
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_08 = 3;  // TXD1    B1_08 Alt3, pg 532 (Rev. 2, 528 Rev. 1)
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_15 = 0;  // MDIO    B1_15 Alt0, pg 539 (Rev. 2, 535 Rev. 1)
  IOMUXC_SW_MUX_CTL_PAD_GPIO_B1_14 = 0;  // MDC     B1_14 Alt0, pg 538 (Rev. 2, 534 Rev. 1)
  IOMUXC_ENET_MDIO_SELECT_INPUT = 2;     // GPIO_B1_15_ALT0, pg 796 (Rev. 2, 792 Rev. 1)
  IOMUXC_ENET0_RXDATA_SELECT_INPUT = 1;  // GPIO_B1_04_ALT3, pg 796 (Rev. 2, 792 Rev. 1)
  IOMUXC_ENET1_RXDATA_SELECT_INPUT = 1;  // GPIO_B1_05_ALT3, pg 797 (Rev. 2, 793 Rev. 1)
  IOMUXC_ENET_RXEN_SELECT_INPUT = 1;     // GPIO_B1_06_ALT3, pg 798 (Rev. 2, 794 Rev. 1)
  IOMUXC_ENET_RXERR_SELECT_INPUT = 1;    // GPIO_B1_11_ALT3, pg 799 (Rev. 2, 795 Rev. 1)
  IOMUXC_ENET_IPG_CLK_RMII_SELECT_INPUT = 1;  // GPIO_B1_10_ALT6, pg 795 (Rev. 2, 791 Rev. 1)
  delayMicroseconds(2);
  GPIO7_DR_SET = (1<<14);  // Start PHY chip
  ENET_MSCR = ENET_MSCR_MII_SPEED(9);
  delayMicroseconds(5);

  // LEDCR offset 0x18, set LED_Link_Polarity, pg 62
  mdio_write(0, 0x18, 0x0280);  // LED shows link status, active high
  // RCSR offset 0x17, set RMII_Clock_Select, pg 61
  mdio_write(0, 0x17, 0x0081);  // Config for 50 MHz clock input

  memset(rx_ring, 0, sizeof(rx_ring));
  memset(tx_ring, 0, sizeof(tx_ring));

  for (int i = 0; i < RX_SIZE; i++) {
    rx_ring[i].buffer = &rxbufs[i * BUF_SIZE];
    rx_ring[i].status = kEnetRxBdEmpty;
    rx_ring[i].extend1 = kEnetRxBdInterrupt;
  }
  // The last buffer descriptor should be set with the wrap flag
  rx_ring[RX_SIZE - 1].status |= kEnetRxBdWrap;

  for (int i=0; i < TX_SIZE; i++) {
    tx_ring[i].buffer = &txbufs[i * BUF_SIZE];
    tx_ring[i].status = kEnetTxBdTransmitCrc;
    tx_ring[i].extend1 = kEnetTxBdTxInterrupt |
                         kEnetTxBdProtChecksum |
                         kEnetTxBdIpHdrChecksum;
  }
  tx_ring[TX_SIZE - 1].status |= kEnetTxBdWrap;

  ENET_EIMR = 0;  // This also deasserts all interrupts

  ENET_RCR = ENET_RCR_NLC |     // Payload length is checked
             ENET_RCR_MAX_FL(MAX_FRAME_LEN) |
             ENET_RCR_CFEN |    // Discard non-pause MAC control frames
             ENET_RCR_CRCFWD |  // CRC is stripped (ignored if PADEN)
             ENET_RCR_PADEN |   // Padding is removed
             ENET_RCR_RMII_MODE |
             ENET_RCR_FCE |     // Flow control enable
             // ENET_RCR_PROM |    // Promiscuous mode
             ENET_RCR_MII_MODE;
  ENET_TCR = ENET_TCR_ADDINS |  // Overwrite with programmed MAC address
             ENET_TCR_ADDSEL(0) |
             // ENET_TCR_RFC_PAUSE |
             // ENET_TCR_TFC_PAUSE |
             ENET_TCR_FDEN;     // Enable full-duplex

  ENET_TACC = 0
#if CHECKSUM_GEN_UDP == 0 || CHECKSUM_GEN_TCP == 0 || CHECKSUM_GEN_ICMP == 0
      | ENET_TACC_PROCHK  // Insert protocol checksum
#endif
#if CHECKSUM_GEN_IP == 0
      | ENET_TACC_IPCHK  // Insert IP header checksum
#endif
#if ETH_PAD_SIZE == 2
      | ENET_TACC_SHIFT16
#endif
      ;

  ENET_RACC = 0
#if ETH_PAD_SIZE == 2
      | ENET_RACC_SHIFT16
#endif
      | ENET_RACC_LINEDIS  // Discard bad frames
#if CHECKSUM_CHECK_UDP == 0 && \
    CHECKSUM_CHECK_TCP == 0 && \
    CHECKSUM_CHECK_ICMP == 0
      | ENET_RACC_PRODIS  // Discard frames with incorrect protocol checksum
                          // Requires RSFL == 0
#endif
#if CHECKSUM_CHECK_IP == 0
      | ENET_RACC_IPDIS  // Discard frames with incorrect IPv4 header checksum
                         // Requires RSFL == 0
#endif
      | ENET_RACC_PADREM
      ;

  ENET_TFWR = ENET_TFWR_STRFWD;
  ENET_RSFL = 0;

  ENET_RDSR = (uint32_t)rx_ring;
  ENET_TDSR = (uint32_t)tx_ring;
  ENET_MRBR = BUF_SIZE;

  ENET_RXIC = 0;
  ENET_TXIC = 0;
  ENET_PALR = mac[0] << 24 | mac[1] << 16 | mac[2] << 8 | mac[3];
  ENET_PAUR = mac[4] << 24 | mac[5] << 16 | 0x8808;

  ENET_OPD = 0x10014;
  ENET_RSEM = 0;
  ENET_MIBC = 0;

  ENET_IAUR = 0;
  ENET_IALR = 0;
  ENET_GAUR = 0;
  ENET_GALR = 0;

  ENET_EIMR = ENET_EIMR_RXF;
  attachInterruptVector(IRQ_ENET, enet_isr);
  NVIC_ENABLE_IRQ(IRQ_ENET);

  // Last, enable the Ethernet MAC
  ENET_ECR = 0x70000000 | ENET_ECR_DBSWP | ENET_ECR_EN1588 | ENET_ECR_ETHEREN;

  // Indicate there are empty RX buffers and available ready TX buffers
  ENET_RDAR = ENET_RDAR_RDAR;
  ENET_TDAR = ENET_TDAR_TDAR;

  // phy soft reset
  // phy_mdio_write(0, 0x00, 1 << 15);

  isInitialized = true;
}

static struct pbuf *t41_low_level_input(volatile enetbufferdesc_t *bdPtr) {
  const u16_t err_mask = kEnetRxBdTrunc |
                         kEnetRxBdOverrun |
                         kEnetRxBdCrc |
                         kEnetRxBdNonOctet |
                         kEnetRxBdLengthViolation;

  struct pbuf *p = NULL;

  // Determine if a frame has been received
  if (bdPtr->status & err_mask) {
#if LINK_STATS
    if (bdPtr->status & kEnetRxBdTrunc) {
      LINK_STATS_INC(link.lenerr);
    } else {  // Either truncated or others
      if (bdPtr->status & kEnetRxBdOverrun) {
        LINK_STATS_INC(link.err);
      } else {  // Either overrun and others zero, or others
        if (bdPtr->status & kEnetRxBdNonOctet) {
          LINK_STATS_INC(link.err);
        } else if (bdPtr->status & kEnetRxBdCrc) {  // Non-octet or CRC
          LINK_STATS_INC(link.chkerr);
        }
        if (bdPtr->status & kEnetRxBdLengthViolation) {
          LINK_STATS_INC(link.lenerr);
        }
      }
    }
    LINK_STATS_INC(link.drop);
#endif
  } else {
    p = pbuf_alloc(PBUF_RAW, bdPtr->length, PBUF_POOL);
    if (p) {
      pbuf_take(p, bdPtr->buffer, p->tot_len);
      LINK_STATS_INC(link.recv);
    } else {
      LINK_STATS_INC(link.drop);
    }
  }

  // Set rx bd empty
  bdPtr->status = (bdPtr->status & kEnetRxBdWrap) | kEnetRxBdEmpty;

  ENET_RDAR = ENET_RDAR_RDAR;

  return p;
}

// Acquire a buffer descriptor. Meant to be used with update_bufdesc().
static inline volatile enetbufferdesc_t *get_bufdesc() {
  volatile enetbufferdesc_t *bdPtr = p_txbd;

  while (bdPtr->status & kEnetTxBdReady) {
    // Wait until BD is free
  }

  return bdPtr;
}

// Update a buffer descriptor. Meant to be used with get_bufdesc().
static inline void update_bufdesc(volatile enetbufferdesc_t *bdPtr,
                                  uint16_t len) {
  bdPtr->length = len;
  bdPtr->status = (bdPtr->status & kEnetTxBdWrap) |
                  kEnetTxBdTransmitCrc |
                  kEnetTxBdLast |
                  kEnetTxBdReady;

  ENET_TDAR = ENET_TDAR_TDAR;

  if (bdPtr->status & kEnetTxBdWrap) {
    p_txbd = &tx_ring[0];
  } else {
    p_txbd++;
  }

  LINK_STATS_INC(link.xmit);
}

static err_t t41_low_level_output(struct netif *netif, struct pbuf *p) {
  volatile enetbufferdesc_t *bdPtr = get_bufdesc();
  update_bufdesc(bdPtr, pbuf_copy_partial(p, bdPtr->buffer, p->tot_len, 0));
  return ERR_OK;
}

static err_t t41_netif_init(struct netif *netif) {
  netif->linkoutput = t41_low_level_output;
  netif->output = etharp_output;
  netif->mtu = MTU;
  netif->flags = NETIF_FLAG_BROADCAST |
                 NETIF_FLAG_ETHARP |
                 NETIF_FLAG_ETHERNET |
                 NETIF_FLAG_IGMP;

  SMEMCPY(netif->hwaddr, mac, ETH_HWADDR_LEN);
  netif->hwaddr_len = ETH_HWADDR_LEN;
#if LWIP_NETIF_HOSTNAME
  netif_set_hostname(netif, NULL);
#endif
  netif->name[0] = 'e';
  netif->name[1] = '0';

  t41_low_level_init();

  return ERR_OK;
}

// Find the next non-empty BD.
static inline volatile enetbufferdesc_t *rxbd_next() {
  volatile enetbufferdesc_t *p_bd = p_rxbd;

  while (p_bd->status & kEnetRxBdEmpty) {
    if (p_bd->status & kEnetRxBdWrap) {
      p_bd = &rx_ring[0];
    } else {
      p_bd++;
    }
    if (p_bd == p_rxbd) {
      return NULL;
    }
  }

  if (p_rxbd->status & kEnetRxBdWrap) {
    p_rxbd = &rx_ring[0];
  } else {
    p_rxbd++;
  }
  return p_bd;
}

void enet_isr() {
  if (ENET_EIR & ENET_EIR_RXF) {
    ENET_EIR = ENET_EIR_RXF;
    rx_ready = 1;
  }
}

static inline void check_link_status() {
  if (!isInitialized) {
    return;
  }
  uint16_t status = mdio_read(0, 0x01);
  uint8_t is_link_up = !!(status & (1 << 2));
  if (netif_is_link_up(&t41_netif) != is_link_up) {
    if (is_link_up) {
      netif_set_link_up(&t41_netif);

      // TODO: Should we read the speed only at link UP or every time?
      status = mdio_read(0, 0x10);
      linkSpeed10Not100 = ((status & (1 << 1)) != 0);
      linkIsFullDuplex = ((status & (1 << 2)) != 0);
    } else {
      netif_set_link_down(&t41_netif);
    }
  }
}

// CRC-32 routines for computing the FCS for multicast lookup

static DMAMEM uint32_t kCRC32Lookup[256];

// See: https://create.stephan-brumme.com/crc32/#sarwate
static void generate_crc32_lookup() {
  for (int i = 0; i < 256; i++) {
    uint32_t crc = i;
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ (-(int)(crc & 1) & 0xEDB88320);
    }
    kCRC32Lookup[i] = crc;
  }
}

static uint32_t crc32(uint32_t crc, const unsigned char *data, size_t len) {
  crc = ~crc;
  while (len--) {
    crc = (crc >> 8) ^ kCRC32Lookup[(crc & 0xff) ^ *(data++)];
  }
  return crc;  // Why does this work without inversion?
}

// Multicast filter for letting the hardware know which packets to let in.
static err_t multicast_filter(struct netif *netif, const ip4_addr_t *group,
                              enum netif_mac_filter_action action) {
  switch (action) {
    case NETIF_ADD_MAC_FILTER:
      enet_join_group(group);
      break;
    case NETIF_DEL_MAC_FILTER:
      enet_leave_group(group);
      break;
    default:
      break;
  }
  return ERR_OK;
}

// --------------------------------------------------------------------------
//  Public interface
// --------------------------------------------------------------------------

void enet_getmac(uint8_t *mac) {
  uint32_t m1 = HW_OCOTP_MAC1;
  uint32_t m2 = HW_OCOTP_MAC0;
  mac[0] = m1 >> 8;
  mac[1] = m1 >> 0;
  mac[2] = m2 >> 24;
  mac[3] = m2 >> 16;
  mac[4] = m2 >> 8;
  mac[5] = m2 >> 0;
}

static bool isFirstInit = true;
static bool isNetifAdded = false;

NETIF_DECLARE_EXT_CALLBACK(netif_callback);

void enet_init(const uint8_t macaddr[ETH_HWADDR_LEN],
               const ip4_addr_t *ipaddr,
               const ip4_addr_t *netmask,
               const ip4_addr_t *gw,
               netif_ext_callback_fn callback) {
  // Only execute the following code once
  if (isFirstInit) {
    generate_crc32_lookup();
    lwip_init();

    isFirstInit = false;
  }

  if (ipaddr == NULL) ipaddr = IP4_ADDR_ANY4;
  if (netmask == NULL) netmask = IP4_ADDR_ANY4;
  if (gw == NULL) gw = IP4_ADDR_ANY4;

  // First test if the MAC address has changed
  // If it's changed then remove the interface and start again
  uint8_t m[ETH_HWADDR_LEN];
  if (macaddr == NULL) {
    enet_getmac(m);
  } else {
    SMEMCPY(m, macaddr, ETH_HWADDR_LEN);
  }
  if (memcmp(mac, m, ETH_HWADDR_LEN)) {
    // MAC address has changed

    if (isNetifAdded) {
      // Remove any previous configuration
      netif_remove(&t41_netif);
      netif_remove_ext_callback(&netif_callback);
      isNetifAdded = false;
    }
    SMEMCPY(mac, m, ETH_HWADDR_LEN);
  }

  if (isNetifAdded) {
    netif_set_addr(&t41_netif, ipaddr, netmask, gw);
  } else {
    netif_add_ext_callback(&netif_callback, callback);
    netif_add(&t41_netif, ipaddr, netmask, gw,
              NULL, t41_netif_init, ethernet_input);
    netif_set_default(&t41_netif);
    isNetifAdded = true;
  }

  // Multicast filtering, to allow desired multicast packets in
  netif_set_igmp_mac_filter(&t41_netif, &multicast_filter);
}

void enet_deinit() {
  isInitialized = false;

  if (isNetifAdded) {
    netif_remove(&t41_netif);
    netif_remove_ext_callback(&netif_callback);
    isNetifAdded = false;
  }

  // Gracefully stop any transmission before disabling the Ethernet MAC
  ENET_TCR |= ENET_TCR_GTS;
  while ((ENET_EIR & ENET_EIR_GRA) == 0) {
    // Wait until it's gracefully stopped
  }
  ENET_EIR = ENET_EIR_GRA;

  // Disable the Ethernet MAC
  // Note: All interrupts are cleared when Ethernet is reinitialized,
  //       so nothing will be pending
  ENET_ECR &= ~ENET_ECR_ETHEREN;

  // Power down the PHY
  GPIO7_GDIR |= (1<<15);
  GPIO7_DR_CLEAR = (1<<15);

  // Stop the PLL
  CCM_ANALOG_PLL_ENET = CCM_ANALOG_PLL_ENET_POWERDOWN;

  // Disable the clock for ENET
  CCM_CCGR1 &= ~CCM_CCGR1_ENET(CCM_CCGR_ON);
}

struct netif *enet_netif() {
  return &t41_netif;
}

// Get the next chunk of input data.
static struct pbuf *enet_rx_next() {
  volatile enetbufferdesc_t *p_bd = rxbd_next();
  return (p_bd ? t41_low_level_input(p_bd) : NULL);
}

// Process one chunk of input data.
static void enet_input(struct pbuf *p_frame) {
  if (t41_netif.input(p_frame, &t41_netif) != ERR_OK) {
    pbuf_free(p_frame);
  }
}

void enet_proc_input(void) {
  struct pbuf *p;

  if (!rx_ready) {
    return;
  }
  rx_ready = 0;
  while ((p = enet_rx_next()) != NULL) {
    enet_input(p);
  }
}

void enet_poll() {
  sys_check_timeouts();
  check_link_status();
}

int enet_link_speed() {
  return linkSpeed10Not100 ? 10 : 100;
}

bool enet_link_is_full_duplex() {
  return linkIsFullDuplex;
}

bool enet_output_frame(const uint8_t *frame, size_t len) {
  if (!isInitialized) {
    return false;
  }
  if (frame == NULL || len < 60 || MAX_FRAME_LEN - 4 < len) {
    return false;
  }
  volatile enetbufferdesc_t *bdPtr = get_bufdesc();
#if ETH_PAD_SIZE
  memcpy(bdPtr->buffer + ETH_PAD_SIZE, frame, len);
  update_bufdesc(bdPtr, len + ETH_PAD_SIZE);
#else
  memcpy(bdPtr->buffer, frame, len);
  update_bufdesc(bdPtr, len);
#endif  // ETH_PAD_SIZE
  return true;
}

// Multicast MAC address.
static uint8_t multicastMAC[6] = {
    LL_IP4_MULTICAST_ADDR_0,
    LL_IP4_MULTICAST_ADDR_1,
    LL_IP4_MULTICAST_ADDR_2,
    0,
    0,
    0,
};

// Don't release bits that have had a collision. Track these here.
static uint32_t collisionGALR = 0;
static uint32_t collisionGAUR = 0;
static uint32_t collisionIALR = 0;
static uint32_t collisionIAUR = 0;

// Join or leave a multicast group. The flag should be true to join and false
// to leave.
static void enet_join_notleave_group(const ip4_addr_t *group, bool flag) {
  multicastMAC[3] = ip4_addr2(group) & 0x7f;
  multicastMAC[4] = ip4_addr3(group);
  multicastMAC[5] = ip4_addr4(group);

  enet_set_mac_address_allowed(multicastMAC, flag);
}

void enet_set_mac_address_allowed(const uint8_t *mac, bool allow) {
  volatile uint32_t *lower;
  volatile uint32_t *upper;
  uint32_t *collisionLower;
  uint32_t *collisionUpper;
  if ((mac[0] & 0x01) != 0) {  // Group
    lower = &ENET_GALR;
    upper = &ENET_GAUR;
    collisionLower = &collisionGALR;
    collisionUpper = &collisionGAUR;
  } else {  // Individual
    lower = &ENET_IALR;
    upper = &ENET_IAUR;
    collisionLower = &collisionIALR;
    collisionUpper = &collisionIAUR;
  }

  uint32_t crc = (crc32(0, mac, 6) >> 26) & 0x3f;
  uint32_t value = 1 << (crc & 0x1f);
  if (crc < 0x20) {
    if (allow) {
      if ((*lower & value) != 0) {
        *collisionLower |= value;
      } else {
        *lower |= value;
      }
    } else {
      // Keep collided bits set
      *lower &= ~value | *collisionLower;
    }
  } else {
    if (allow) {
      if ((*upper & value) != 0) {
        *collisionUpper |= value;
      } else {
        *upper |= value;
      }
    } else {
      // Keep collided bits set
      *upper &= ~value | *collisionUpper;
    }
  }
}

void enet_join_group(const ip4_addr_t *group) {
  enet_join_notleave_group(group, true);
}

void enet_leave_group(const ip4_addr_t *group) {
  enet_join_notleave_group(group, false);
}

#endif  // ARDUINO_TEENSY41
