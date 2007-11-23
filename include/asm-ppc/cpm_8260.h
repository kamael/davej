
/*
 * MPC8260 Communication Processor Module.
 * Copyright (c) 1999 Dan Malek (dmalek@jlc.net)
 *
 * This file contains structures and information for the communication
 * processor channels found in the dual port RAM or parameter RAM.
 * All CPM control and status is available through the MPC8260 internal
 * memory map.  See immap.h for details.
 */
#ifndef __CPM_82XX__
#define __CPM_82XX__

#include <asm/immap_8260.h>

/* CPM Command register.
*/
#define CPM_CR_RST	((uint)0x80000000)
#define CPM_CR_PAGE	((uint)0x7c000000)
#define CPM_CR_SBLOCK	((uint)0x03e00000)
#define CPM_CR_FLG	((uint)0x00010000)
#define CPM_CR_MCN	((uint)0x00003fc0)
#define CPM_CR_OPCODE	((uint)0x0000000f)

/* Device sub-block and page codes.
*/
#define CPM_CR_SCC1_SBLOCK	(0x04)
#define CPM_CR_SCC2_SBLOCK	(0x05)
#define CPM_CR_SCC3_SBLOCK	(0x06)
#define CPM_CR_SCC4_SBLOCK	(0x07)
#define CPM_CR_SMC1_SBLOCK	(0x08)
#define CPM_CR_SMC2_SBLOCK	(0x09)
#define CPM_CR_SPI_SBLOCK	(0x0a)
#define CPM_CR_I2C_SBLOCK	(0x0b)
#define CPM_CR_TIMER_SBLOCK	(0x0f)
#define CPM_CR_RAND_SBLOCK	(0x0e)
#define CPM_CR_FCC1_SBLOCK	(0x10)
#define CPM_CR_FCC2_SBLOCK	(0x11)
#define CPM_CR_FCC3_SBLOCK	(0x12)
#define CPM_CR_IDMA1_SBLOCK	(0x14)
#define CPM_CR_IDMA2_SBLOCK	(0x15)
#define CPM_CR_IDMA3_SBLOCK	(0x16)
#define CPM_CR_IDMA4_SBLOCK	(0x17)
#define CPM_CR_MCC1_SBLOCK	(0x1c)

#define CPM_CR_SCC1_PAGE	(0x00)
#define CPM_CR_SCC2_PAGE	(0x01)
#define CPM_CR_SCC3_PAGE	(0x02)
#define CPM_CR_SCC4_PAGE	(0x03)
#define CPM_CR_SMC1_PAGE	(0x07)
#define CPM_CR_SMC2_PAGE	(0x08)
#define CPM_CR_SPI_PAGE		(0x09)
#define CPM_CR_I2C_PAGE		(0x0a)
#define CPM_CR_TIMER_PAGE	(0x0a)
#define CPM_CR_RAND_PAGE	(0x0a)
#define CPM_CR_FCC1_PAGE	(0x04)
#define CPM_CR_FCC2_PAGE	(0x05)
#define CPM_CR_FCC3_PAGE	(0x06)
#define CPM_CR_IDMA1_PAGE	(0x07)
#define CPM_CR_IDMA2_PAGE	(0x08)
#define CPM_CR_IDMA3_PAGE	(0x09)
#define CPM_CR_IDMA4_PAGE	(0x0a)
#define CPM_CR_MCC1_PAGE	(0x07)
#define CPM_CR_MCC2_PAGE	(0x08)

/* Some opcodes (there are more...later)
*/
#define CPM_CR_INIT_TRX		((ushort)0x0000)
#define CPM_CR_INIT_RX		((ushort)0x0001)
#define CPM_CR_INIT_TX		((ushort)0x0002)
#define CPM_CR_HUNT_MODE	((ushort)0x0003)
#define CPM_CR_STOP_TX		((ushort)0x0004)
#define CPM_CR_RESTART_TX	((ushort)0x0006)
#define CPM_CR_SET_GADDR	((ushort)0x0008)

#define mk_cr_cmd(PG, SBC, MCN, OP) \
	((PG << 26) | (SBC << 21) | (MCN << 6) | OP)

/* Dual Port RAM addresses.  The first 16K is available for almost
 * any CPM use, so we put the BDs there.  The first 128 bytes are
 * used for SMC1 and SMC2 parameter RAM, so we start allocating
 * BDs above that.  All of this must change when we start
 * downloading RAM microcode.
 */
#define CPM_DATAONLY_BASE	((uint)128)
#define CPM_DATAONLY_SIZE	((uint)(16 * 1024) - CPM_DATAONLY_BASE)
#define CPM_DP_NOSPACE		((uint)0x7fffffff)

/* The number of pages of host memory we allocate for CPM.  This is
 * done early in kernel initialization to get physically contiguous
 * pages.
 */
#define NUM_CPM_HOST_PAGES	2


/* Export the base address of the communication processor registers
 * and dual port ram.
 */
extern	cpm8260_t	*cpmp;		/* Pointer to comm processor */
uint		m8260_cpm_dpalloc(uint size);
uint		m8260_cpm_hostalloc(uint size);
void		m8260_cpm_setbrg(uint brg, uint rate);
void		m8260_cpm_fastbrg(uint brg, uint rate, int div16);

/* Buffer descriptors used by many of the CPM protocols.
*/
typedef struct cpm_buf_desc {
	ushort	cbd_sc;		/* Status and Control */
	ushort	cbd_datlen;	/* Data length in buffer */
	uint	cbd_bufaddr;	/* Buffer address in host memory */
} cbd_t;

#define BD_SC_EMPTY	((ushort)0x8000)	/* Recieve is empty */
#define BD_SC_READY	((ushort)0x8000)	/* Transmit is ready */
#define BD_SC_WRAP	((ushort)0x2000)	/* Last buffer descriptor */
#define BD_SC_INTRPT	((ushort)0x1000)	/* Interrupt on change */
#define BD_SC_LAST	((ushort)0x0800)	/* Last buffer in frame */
#define BD_SC_CM	((ushort)0x0200)	/* Continous mode */
#define BD_SC_ID	((ushort)0x0100)	/* Rec'd too many idles */
#define BD_SC_P		((ushort)0x0100)	/* xmt preamble */
#define BD_SC_BR	((ushort)0x0020)	/* Break received */
#define BD_SC_FR	((ushort)0x0010)	/* Framing error */
#define BD_SC_PR	((ushort)0x0008)	/* Parity error */
#define BD_SC_OV	((ushort)0x0002)	/* Overrun */
#define BD_SC_CD	((ushort)0x0001)	/* ?? */

/* Function code bits, usually generic to devices.
*/
#define CPMFCR_GBL	((u_char)0x20)	/* Set memory snooping */
#define CPMFCR_EB	((u_char)0x10)	/* Set big endian byte order */
#define CPMFCR_TC2	((u_char)0x04)	/* Transfer code 2 value */
#define CPMFCR_DTB	((u_char)0x02)	/* Use local bus for data when set */
#define CPMFCR_BDB	((u_char)0x01)	/* Use local bus for BD when set */

/* Parameter RAM offsets from the base.
*/
#define PROFF_SCC1		((uint)0x8000)
#define PROFF_SCC2		((uint)0x8100)
#define PROFF_SCC3		((uint)0x8200)
#define PROFF_SCC4		((uint)0x8300)
#define PROFF_FCC1		((uint)0x8400)
#define PROFF_FCC2		((uint)0x8500)
#define PROFF_FCC3		((uint)0x8600)
#define PROFF_MCC1		((uint)0x8700)
#define PROFF_SMC1_BASE		((uint)0x87fc)
#define PROFF_IDMA1_BASE	((uint)0x87fe)
#define PROFF_MCC2		((uint)0x8800)
#define PROFF_SMC2_BASE		((uint)0x88fc)
#define PROFF_IDMA2_BASE	((uint)0x88fe)
#define PROFF_SPI_BASE		((uint)0x89fc)
#define PROFF_IDMA3_BASE	((uint)0x89fe)
#define PROFF_TIMERS		((uint)0x8ae0)
#define PROFF_REVNUM		((uint)0x8af0)
#define PROFF_RAND		((uint)0x8af8)
#define PROFF_I2C_BASE		((uint)0x8afc)
#define PROFF_IDMA4_BASE	((uint)0x89fe)

/* The SMCs are relocated to any of the first eight DPRAM pages.
 * We will fix these at the first locations of DPRAM, until we
 * get some microcode patches :-).
 * The parameter ram space for the SMCs is fifty-some bytes, and
 * they are required to start on a 64 byte boundary.
 */
#define PROFF_SMC1	(0)
#define PROFF_SMC2	(64)


/* Define enough so I can at least use the serial port as a UART.
 */
typedef struct smc_uart {
	ushort	smc_rbase;	/* Rx Buffer descriptor base address */
	ushort	smc_tbase;	/* Tx Buffer descriptor base address */
	u_char	smc_rfcr;	/* Rx function code */
	u_char	smc_tfcr;	/* Tx function code */
	ushort	smc_mrblr;	/* Max receive buffer length */
	uint	smc_rstate;	/* Internal */
	uint	smc_idp;	/* Internal */
	ushort	smc_rbptr;	/* Internal */
	ushort	smc_ibc;	/* Internal */
	uint	smc_rxtmp;	/* Internal */
	uint	smc_tstate;	/* Internal */
	uint	smc_tdp;	/* Internal */
	ushort	smc_tbptr;	/* Internal */
	ushort	smc_tbc;	/* Internal */
	uint	smc_txtmp;	/* Internal */
	ushort	smc_maxidl;	/* Maximum idle characters */
	ushort	smc_tmpidl;	/* Temporary idle counter */
	ushort	smc_brklen;	/* Last received break length */
	ushort	smc_brkec;	/* rcv'd break condition counter */
	ushort	smc_brkcr;	/* xmt break count register */
	ushort	smc_rmask;	/* Temporary bit mask */
	uint	smc_stmp;	/* SDMA Temp */
} smc_uart_t;

/* SMC uart mode register (Internal memory map).
*/
#define	SMCMR_REN	((ushort)0x0001)
#define SMCMR_TEN	((ushort)0x0002)
#define SMCMR_DM	((ushort)0x000c)
#define SMCMR_SM_GCI	((ushort)0x0000)
#define SMCMR_SM_UART	((ushort)0x0020)
#define SMCMR_SM_TRANS	((ushort)0x0030)
#define SMCMR_SM_MASK	((ushort)0x0030)
#define SMCMR_PM_EVEN	((ushort)0x0100)	/* Even parity, else odd */
#define SMCMR_REVD	SMCMR_PM_EVEN
#define SMCMR_PEN	((ushort)0x0200)	/* Parity enable */
#define SMCMR_BS	SMCMR_PEN
#define SMCMR_SL	((ushort)0x0400)	/* Two stops, else one */
#define SMCR_CLEN_MASK	((ushort)0x7800)	/* Character length */
#define smcr_mk_clen(C)	(((C) << 11) & SMCR_CLEN_MASK)

/* SMC Event and Mask register.
*/
#define	SMCM_TXE	((unsigned char)0x10)
#define	SMCM_BSY	((unsigned char)0x04)
#define	SMCM_TX		((unsigned char)0x02)
#define	SMCM_RX		((unsigned char)0x01)

/* Baud rate generators.
*/
#define CPM_BRG_RST		((uint)0x00020000)
#define CPM_BRG_EN		((uint)0x00010000)
#define CPM_BRG_EXTC_INT	((uint)0x00000000)
#define CPM_BRG_EXTC_CLK3_9	((uint)0x00004000)
#define CPM_BRG_EXTC_CLK5_15	((uint)0x00008000)
#define CPM_BRG_ATB		((uint)0x00002000)
#define CPM_BRG_CD_MASK		((uint)0x00001ffe)
#define CPM_BRG_DIV16		((uint)0x00000001)

/* SCCs.
*/
#define SCC_GSMRH_IRP		((uint)0x00040000)
#define SCC_GSMRH_GDE		((uint)0x00010000)
#define SCC_GSMRH_TCRC_CCITT	((uint)0x00008000)
#define SCC_GSMRH_TCRC_BISYNC	((uint)0x00004000)
#define SCC_GSMRH_TCRC_HDLC	((uint)0x00000000)
#define SCC_GSMRH_REVD		((uint)0x00002000)
#define SCC_GSMRH_TRX		((uint)0x00001000)
#define SCC_GSMRH_TTX		((uint)0x00000800)
#define SCC_GSMRH_CDP		((uint)0x00000400)
#define SCC_GSMRH_CTSP		((uint)0x00000200)
#define SCC_GSMRH_CDS		((uint)0x00000100)
#define SCC_GSMRH_CTSS		((uint)0x00000080)
#define SCC_GSMRH_TFL		((uint)0x00000040)
#define SCC_GSMRH_RFW		((uint)0x00000020)
#define SCC_GSMRH_TXSY		((uint)0x00000010)
#define SCC_GSMRH_SYNL16	((uint)0x0000000c)
#define SCC_GSMRH_SYNL8		((uint)0x00000008)
#define SCC_GSMRH_SYNL4		((uint)0x00000004)
#define SCC_GSMRH_RTSM		((uint)0x00000002)
#define SCC_GSMRH_RSYN		((uint)0x00000001)

#define SCC_GSMRL_SIR		((uint)0x80000000)	/* SCC2 only */
#define SCC_GSMRL_EDGE_NONE	((uint)0x60000000)
#define SCC_GSMRL_EDGE_NEG	((uint)0x40000000)
#define SCC_GSMRL_EDGE_POS	((uint)0x20000000)
#define SCC_GSMRL_EDGE_BOTH	((uint)0x00000000)
#define SCC_GSMRL_TCI		((uint)0x10000000)
#define SCC_GSMRL_TSNC_3	((uint)0x0c000000)
#define SCC_GSMRL_TSNC_4	((uint)0x08000000)
#define SCC_GSMRL_TSNC_14	((uint)0x04000000)
#define SCC_GSMRL_TSNC_INF	((uint)0x00000000)
#define SCC_GSMRL_RINV		((uint)0x02000000)
#define SCC_GSMRL_TINV		((uint)0x01000000)
#define SCC_GSMRL_TPL_128	((uint)0x00c00000)
#define SCC_GSMRL_TPL_64	((uint)0x00a00000)
#define SCC_GSMRL_TPL_48	((uint)0x00800000)
#define SCC_GSMRL_TPL_32	((uint)0x00600000)
#define SCC_GSMRL_TPL_16	((uint)0x00400000)
#define SCC_GSMRL_TPL_8		((uint)0x00200000)
#define SCC_GSMRL_TPL_NONE	((uint)0x00000000)
#define SCC_GSMRL_TPP_ALL1	((uint)0x00180000)
#define SCC_GSMRL_TPP_01	((uint)0x00100000)
#define SCC_GSMRL_TPP_10	((uint)0x00080000)
#define SCC_GSMRL_TPP_ZEROS	((uint)0x00000000)
#define SCC_GSMRL_TEND		((uint)0x00040000)
#define SCC_GSMRL_TDCR_32	((uint)0x00030000)
#define SCC_GSMRL_TDCR_16	((uint)0x00020000)
#define SCC_GSMRL_TDCR_8	((uint)0x00010000)
#define SCC_GSMRL_TDCR_1	((uint)0x00000000)
#define SCC_GSMRL_RDCR_32	((uint)0x0000c000)
#define SCC_GSMRL_RDCR_16	((uint)0x00008000)
#define SCC_GSMRL_RDCR_8	((uint)0x00004000)
#define SCC_GSMRL_RDCR_1	((uint)0x00000000)
#define SCC_GSMRL_RENC_DFMAN	((uint)0x00003000)
#define SCC_GSMRL_RENC_MANCH	((uint)0x00002000)
#define SCC_GSMRL_RENC_FM0	((uint)0x00001000)
#define SCC_GSMRL_RENC_NRZI	((uint)0x00000800)
#define SCC_GSMRL_RENC_NRZ	((uint)0x00000000)
#define SCC_GSMRL_TENC_DFMAN	((uint)0x00000600)
#define SCC_GSMRL_TENC_MANCH	((uint)0x00000400)
#define SCC_GSMRL_TENC_FM0	((uint)0x00000200)
#define SCC_GSMRL_TENC_NRZI	((uint)0x00000100)
#define SCC_GSMRL_TENC_NRZ	((uint)0x00000000)
#define SCC_GSMRL_DIAG_LE	((uint)0x000000c0)	/* Loop and echo */
#define SCC_GSMRL_DIAG_ECHO	((uint)0x00000080)
#define SCC_GSMRL_DIAG_LOOP	((uint)0x00000040)
#define SCC_GSMRL_DIAG_NORM	((uint)0x00000000)
#define SCC_GSMRL_ENR		((uint)0x00000020)
#define SCC_GSMRL_ENT		((uint)0x00000010)
#define SCC_GSMRL_MODE_ENET	((uint)0x0000000c)
#define SCC_GSMRL_MODE_DDCMP	((uint)0x00000009)
#define SCC_GSMRL_MODE_BISYNC	((uint)0x00000008)
#define SCC_GSMRL_MODE_V14	((uint)0x00000007)
#define SCC_GSMRL_MODE_AHDLC	((uint)0x00000006)
#define SCC_GSMRL_MODE_PROFIBUS	((uint)0x00000005)
#define SCC_GSMRL_MODE_UART	((uint)0x00000004)
#define SCC_GSMRL_MODE_SS7	((uint)0x00000003)
#define SCC_GSMRL_MODE_ATALK	((uint)0x00000002)
#define SCC_GSMRL_MODE_HDLC	((uint)0x00000000)

#define SCC_TODR_TOD		((ushort)0x8000)

/* SCC Event and Mask register.
*/
#define	SCCM_TXE	((unsigned char)0x10)
#define	SCCM_BSY	((unsigned char)0x04)
#define	SCCM_TX		((unsigned char)0x02)
#define	SCCM_RX		((unsigned char)0x01)

typedef struct scc_param {
	ushort	scc_rbase;	/* Rx Buffer descriptor base address */
	ushort	scc_tbase;	/* Tx Buffer descriptor base address */
	u_char	scc_rfcr;	/* Rx function code */
	u_char	scc_tfcr;	/* Tx function code */
	ushort	scc_mrblr;	/* Max receive buffer length */
	uint	scc_rstate;	/* Internal */
	uint	scc_idp;	/* Internal */
	ushort	scc_rbptr;	/* Internal */
	ushort	scc_ibc;	/* Internal */
	uint	scc_rxtmp;	/* Internal */
	uint	scc_tstate;	/* Internal */
	uint	scc_tdp;	/* Internal */
	ushort	scc_tbptr;	/* Internal */
	ushort	scc_tbc;	/* Internal */
	uint	scc_txtmp;	/* Internal */
	uint	scc_rcrc;	/* Internal */
	uint	scc_tcrc;	/* Internal */
} sccp_t;

/* CPM Ethernet through SCC1.
 */
typedef struct scc_enet {
	sccp_t	sen_genscc;
	uint	sen_cpres;	/* Preset CRC */
	uint	sen_cmask;	/* Constant mask for CRC */
	uint	sen_crcec;	/* CRC Error counter */
	uint	sen_alec;	/* alignment error counter */
	uint	sen_disfc;	/* discard frame counter */
	ushort	sen_pads;	/* Tx short frame pad character */
	ushort	sen_retlim;	/* Retry limit threshold */
	ushort	sen_retcnt;	/* Retry limit counter */
	ushort	sen_maxflr;	/* maximum frame length register */
	ushort	sen_minflr;	/* minimum frame length register */
	ushort	sen_maxd1;	/* maximum DMA1 length */
	ushort	sen_maxd2;	/* maximum DMA2 length */
	ushort	sen_maxd;	/* Rx max DMA */
	ushort	sen_dmacnt;	/* Rx DMA counter */
	ushort	sen_maxb;	/* Max BD byte count */
	ushort	sen_gaddr1;	/* Group address filter */
	ushort	sen_gaddr2;
	ushort	sen_gaddr3;
	ushort	sen_gaddr4;
	uint	sen_tbuf0data0;	/* Save area 0 - current frame */
	uint	sen_tbuf0data1;	/* Save area 1 - current frame */
	uint	sen_tbuf0rba;	/* Internal */
	uint	sen_tbuf0crc;	/* Internal */
	ushort	sen_tbuf0bcnt;	/* Internal */
	ushort	sen_paddrh;	/* physical address (MSB) */
	ushort	sen_paddrm;
	ushort	sen_paddrl;	/* physical address (LSB) */
	ushort	sen_pper;	/* persistence */
	ushort	sen_rfbdptr;	/* Rx first BD pointer */
	ushort	sen_tfbdptr;	/* Tx first BD pointer */
	ushort	sen_tlbdptr;	/* Tx last BD pointer */
	uint	sen_tbuf1data0;	/* Save area 0 - current frame */
	uint	sen_tbuf1data1;	/* Save area 1 - current frame */
	uint	sen_tbuf1rba;	/* Internal */
	uint	sen_tbuf1crc;	/* Internal */
	ushort	sen_tbuf1bcnt;	/* Internal */
	ushort	sen_txlen;	/* Tx Frame length counter */
	ushort	sen_iaddr1;	/* Individual address filter */
	ushort	sen_iaddr2;
	ushort	sen_iaddr3;
	ushort	sen_iaddr4;
	ushort	sen_boffcnt;	/* Backoff counter */

	/* NOTE: Some versions of the manual have the following items
	 * incorrectly documented.  Below is the proper order.
	 */
	ushort	sen_taddrh;	/* temp address (MSB) */
	ushort	sen_taddrm;
	ushort	sen_taddrl;	/* temp address (LSB) */
} scc_enet_t;


/* SCC Event register as used by Ethernet.
*/
#define SCCE_ENET_GRA	((ushort)0x0080)	/* Graceful stop complete */
#define SCCE_ENET_TXE	((ushort)0x0010)	/* Transmit Error */
#define SCCE_ENET_RXF	((ushort)0x0008)	/* Full frame received */
#define SCCE_ENET_BSY	((ushort)0x0004)	/* All incoming buffers full */
#define SCCE_ENET_TXB	((ushort)0x0002)	/* A buffer was transmitted */
#define SCCE_ENET_RXB	((ushort)0x0001)	/* A buffer was received */

/* SCC Mode Register (PMSR) as used by Ethernet.
*/
#define SCC_PMSR_HBC	((ushort)0x8000)	/* Enable heartbeat */
#define SCC_PMSR_FC	((ushort)0x4000)	/* Force collision */
#define SCC_PMSR_RSH	((ushort)0x2000)	/* Receive short frames */
#define SCC_PMSR_IAM	((ushort)0x1000)	/* Check individual hash */
#define SCC_PMSR_ENCRC	((ushort)0x0800)	/* Ethernet CRC mode */
#define SCC_PMSR_PRO	((ushort)0x0200)	/* Promiscuous mode */
#define SCC_PMSR_BRO	((ushort)0x0100)	/* Catch broadcast pkts */
#define SCC_PMSR_SBT	((ushort)0x0080)	/* Special backoff timer */
#define SCC_PMSR_LPB	((ushort)0x0040)	/* Set Loopback mode */
#define SCC_PMSR_SIP	((ushort)0x0020)	/* Sample Input Pins */
#define SCC_PMSR_LCW	((ushort)0x0010)	/* Late collision window */
#define SCC_PMSR_NIB22	((ushort)0x000a)	/* Start frame search */
#define SCC_PMSR_FDE	((ushort)0x0001)	/* Full duplex enable */

/* Buffer descriptor control/status used by Ethernet receive.
*/
#define BD_ENET_RX_EMPTY	((ushort)0x8000)
#define BD_ENET_RX_WRAP		((ushort)0x2000)
#define BD_ENET_RX_INTR		((ushort)0x1000)
#define BD_ENET_RX_LAST		((ushort)0x0800)
#define BD_ENET_RX_FIRST	((ushort)0x0400)
#define BD_ENET_RX_MISS		((ushort)0x0100)
#define BD_ENET_RX_LG		((ushort)0x0020)
#define BD_ENET_RX_NO		((ushort)0x0010)
#define BD_ENET_RX_SH		((ushort)0x0008)
#define BD_ENET_RX_CR		((ushort)0x0004)
#define BD_ENET_RX_OV		((ushort)0x0002)
#define BD_ENET_RX_CL		((ushort)0x0001)
#define BD_ENET_RX_STATS	((ushort)0x013f)	/* All status bits */

/* Buffer descriptor control/status used by Ethernet transmit.
*/
#define BD_ENET_TX_READY	((ushort)0x8000)
#define BD_ENET_TX_PAD		((ushort)0x4000)
#define BD_ENET_TX_WRAP		((ushort)0x2000)
#define BD_ENET_TX_INTR		((ushort)0x1000)
#define BD_ENET_TX_LAST		((ushort)0x0800)
#define BD_ENET_TX_TC		((ushort)0x0400)
#define BD_ENET_TX_DEF		((ushort)0x0200)
#define BD_ENET_TX_HB		((ushort)0x0100)
#define BD_ENET_TX_LC		((ushort)0x0080)
#define BD_ENET_TX_RL		((ushort)0x0040)
#define BD_ENET_TX_RCMASK	((ushort)0x003c)
#define BD_ENET_TX_UN		((ushort)0x0002)
#define BD_ENET_TX_CSL		((ushort)0x0001)
#define BD_ENET_TX_STATS	((ushort)0x03ff)	/* All status bits */

/* SCC as UART
*/
typedef struct scc_uart {
	sccp_t	scc_genscc;
	uint	scc_res1;	/* Reserved */
	uint	scc_res2;	/* Reserved */
	ushort	scc_maxidl;	/* Maximum idle chars */
	ushort	scc_idlc;	/* temp idle counter */
	ushort	scc_brkcr;	/* Break count register */
	ushort	scc_parec;	/* receive parity error counter */
	ushort	scc_frmec;	/* receive framing error counter */
	ushort	scc_nosec;	/* receive noise counter */
	ushort	scc_brkec;	/* receive break condition counter */
	ushort	scc_brkln;	/* last received break length */
	ushort	scc_uaddr1;	/* UART address character 1 */
	ushort	scc_uaddr2;	/* UART address character 2 */
	ushort	scc_rtemp;	/* Temp storage */
	ushort	scc_toseq;	/* Transmit out of sequence char */
	ushort	scc_char1;	/* control character 1 */
	ushort	scc_char2;	/* control character 2 */
	ushort	scc_char3;	/* control character 3 */
	ushort	scc_char4;	/* control character 4 */
	ushort	scc_char5;	/* control character 5 */
	ushort	scc_char6;	/* control character 6 */
	ushort	scc_char7;	/* control character 7 */
	ushort	scc_char8;	/* control character 8 */
	ushort	scc_rccm;	/* receive control character mask */
	ushort	scc_rccr;	/* receive control character register */
	ushort	scc_rlbc;	/* receive last break character */
} scc_uart_t;

/* SCC Event and Mask registers when it is used as a UART.
*/
#define UART_SCCM_GLR		((ushort)0x1000)
#define UART_SCCM_GLT		((ushort)0x0800)
#define UART_SCCM_AB		((ushort)0x0200)
#define UART_SCCM_IDL		((ushort)0x0100)
#define UART_SCCM_GRA		((ushort)0x0080)
#define UART_SCCM_BRKE		((ushort)0x0040)
#define UART_SCCM_BRKS		((ushort)0x0020)
#define UART_SCCM_CCR		((ushort)0x0008)
#define UART_SCCM_BSY		((ushort)0x0004)
#define UART_SCCM_TX		((ushort)0x0002)
#define UART_SCCM_RX		((ushort)0x0001)

/* The SCC PMSR when used as a UART.
*/
#define SCU_PMSR_FLC		((ushort)0x8000)
#define SCU_PMSR_SL		((ushort)0x4000)
#define SCU_PMSR_CL		((ushort)0x3000)
#define SCU_PMSR_UM		((ushort)0x0c00)
#define SCU_PMSR_FRZ		((ushort)0x0200)
#define SCU_PMSR_RZS		((ushort)0x0100)
#define SCU_PMSR_SYN		((ushort)0x0080)
#define SCU_PMSR_DRT		((ushort)0x0040)
#define SCU_PMSR_PEN		((ushort)0x0010)
#define SCU_PMSR_RPM		((ushort)0x000c)
#define SCU_PMSR_REVP		((ushort)0x0008)
#define SCU_PMSR_TPM		((ushort)0x0003)
#define SCU_PMSR_TEVP		((ushort)0x0003)

/* CPM Transparent mode SCC.
 */
typedef struct scc_trans {
	sccp_t	st_genscc;
	uint	st_cpres;	/* Preset CRC */
	uint	st_cmask;	/* Constant mask for CRC */
} scc_trans_t;

#define BD_SCC_TX_LAST		((ushort)0x0800)

/* IIC parameter RAM.
*/
typedef struct iic {
	ushort	iic_rbase;	/* Rx Buffer descriptor base address */
	ushort	iic_tbase;	/* Tx Buffer descriptor base address */
	u_char	iic_rfcr;	/* Rx function code */
	u_char	iic_tfcr;	/* Tx function code */
	ushort	iic_mrblr;	/* Max receive buffer length */
	uint	iic_rstate;	/* Internal */
	uint	iic_rdp;	/* Internal */
	ushort	iic_rbptr;	/* Internal */
	ushort	iic_rbc;	/* Internal */
	uint	iic_rxtmp;	/* Internal */
	uint	iic_tstate;	/* Internal */
	uint	iic_tdp;	/* Internal */
	ushort	iic_tbptr;	/* Internal */
	ushort	iic_tbc;	/* Internal */
	uint	iic_txtmp;	/* Internal */
} iic_t;

#define BD_IIC_START		((ushort)0x0400)

#endif /* __CPM_82XX__ */