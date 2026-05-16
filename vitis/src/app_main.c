#include "xparameters.h"
#include "xil_types.h"
#include "xil_io.h"
#include "xstatus.h"
#include "sleep.h"

#include "xspi.h"

/* ============================================================
 * Address map
 * ============================================================ */
#define LED_GPIO_BASEADDR        XPAR_XGPIO_0_BASEADDR
#define W5500_RST_GPIO_BASEADDR  XPAR_XGPIO_1_BASEADDR
#define UART_BASEADDR            XPAR_XUARTLITE_0_BASEADDR
#define SPI_BASEADDR             XPAR_XSPI_0_BASEADDR
#define SHARED_BRAM_BASEADDR     XPAR_XBRAM_0_BASEADDR
#define CNN_ACCEL_BASEADDR       XPAR_XCNN_ACCEL_0_BASEADDR

/* ============================================================
 * AXI GPIO registers
 * ============================================================ */
#define GPIO_DATA_OFFSET   0x0
#define GPIO_TRI_OFFSET    0x4

/* ============================================================
 * AXI UART Lite registers
 * ============================================================ */
#define UART_RX_OFFSET     0x0
#define UART_TX_OFFSET     0x4
#define UART_STAT_OFFSET   0x8
#define UART_CTRL_OFFSET   0xC

#define UART_STAT_RXVALID  0x01
#define UART_STAT_TXFULL   0x08

#define UART_CTRL_RST_TX   0x01
#define UART_CTRL_RST_RX   0x02

/* ============================================================
 * HLS control registers
 * ============================================================ */
#define CNN_AP_CTRL                0x00
#define CNN_GIE                    0x04
#define CNN_IER                    0x08
#define CNN_ISR                    0x0C

#define CNN_PREDICTED_DATA         0x10

#define CNN_DBG_P0_DATA            0x20
#define CNN_DBG_P1_DATA            0x30
#define CNN_DBG_P2_DATA            0x40
#define CNN_DBG_P3_DATA            0x50
#define CNN_DBG_P4_DATA            0x60
#define CNN_DBG_P5_DATA            0x70
#define CNN_DBG_P6_DATA            0x80
#define CNN_DBG_P7_DATA            0x90
#define CNN_DBG_SUM8_DATA          0xA0

#define CNN_AP_START_MASK          0x01
#define CNN_AP_DONE_MASK           0x02

/* ============================================================
 * Packet format
 * ============================================================ */
#define IMG_PIXELS        196u
#define BRAM_WORD_COUNT   (IMG_PIXELS / 4u)
#define PKT_MAGIC0        'M'
#define PKT_MAGIC1        'N'
#define PKT_MAGIC2        'S'
#define PKT_MAGIC3        'T'
#define PKT_LEN           205u
#define UDP_RX_MAX        256u

/* ============================================================
 * UDP response packet format
 * FPGA -> PC:
 *   byte 0..3   = 'R' 'S' 'L' 'T'
 *   byte 4..7   = seq, big-endian
 *   byte 8      = predicted digit
 *   byte 9      = ok flag, 1 = valid prediction, 0 = timeout/error
 *   byte 10     = received label, useful for dataset tests; 0xFF for unknown
 *   byte 11     = reserved
 * ============================================================ */
#define RSP_MAGIC0        'R'
#define RSP_MAGIC1        'S'
#define RSP_MAGIC2        'L'
#define RSP_MAGIC3        'T'
#define RSP_LEN           12u

/* ============================================================
 * Debug config
 * ============================================================ */
#define DEBUG_DUMP_ALL_WORDS_ON_RX   1u
#define DEBUG_DUMP_BYTES_ON_RX       16u
#define DEBUG_DUMP_RAW_HLS_REGS      0u

/* ============================================================
 * Network config
 * ============================================================ */
static const u8 MAC[6]  = {0x02,0x00,0x00,0x00,0x00,0x01};
static const u8 IP[4]   = {192,168,8,50};
static const u8 GW[4]   = {192,168,8,1};
static const u8 MASK[4] = {255,255,255,0};

#define SOCK_UDP        0
#define UDP_PORT_LOCAL  5000

/* ============================================================
 * W5500 SPI framing
 * ============================================================ */
#define OM_VDM 0x00

static inline u8 w5500_ctrl(u8 bsb, int is_read)
{
    u8 rwb = is_read ? 0 : 1;
    return (u8)((bsb << 3) | (rwb << 2) | OM_VDM);
}

/* BSB mapping */
#define BSB_COMMON      0x00
#define BSB_SOCK_REG(n) (0x01 + 4*(n))
#define BSB_SOCK_TX(n)  (0x02 + 4*(n))
#define BSB_SOCK_RX(n)  (0x03 + 4*(n))

/* Common regs */
#define REG_MR       0x0000
#define REG_GAR0     0x0001
#define REG_SUBR0    0x0005
#define REG_SHAR0    0x0009
#define REG_SIPR0    0x000F

/* Socket regs */
#define Sn_MR          0x0000
#define Sn_CR          0x0001
#define Sn_IR          0x0002
#define Sn_PORT        0x0004
#define Sn_DIPR        0x000C
#define Sn_DPORT       0x0010
#define Sn_TX_FSR      0x0020
#define Sn_TX_WR       0x0024
#define Sn_RX_RSR      0x0026
#define Sn_RX_RD       0x0028
#define Sn_IMR         0x002C

#define REG_Sn_TXBUF_SIZE(sock) (0x1E + (sock))
#define REG_Sn_RXBUF_SIZE(sock) (0x26 + (sock))

/* Commands */
#define CMD_OPEN   0x01
#define CMD_CLOSE  0x10
#define CMD_SEND   0x20
#define CMD_RECV   0x40

#define MODE_UDP   0x02
#define IR_ALL     0x1F

/* ============================================================
 * SPI
 * ============================================================ */
#define SPI_SS0_MASK       0x01
#define SPI_FRAME_MAX      16
#define SPI_PAYLOAD_MAX    (SPI_FRAME_MAX - 3)

static XSpi Spi;
static u8 spi_tx[SPI_FRAME_MAX];
static u8 spi_rx[SPI_FRAME_MAX];
static u16 rx_buf_size_bytes[8] = {0};
static u16 tx_buf_size_bytes[8] = {0};

/* Global buffers */
static u8 rxbuf[UDP_RX_MAX];
static u8 udp_hdr[8];
static u8 last_udp_src_ip[4] = {0, 0, 0, 0};
static u16 last_udp_src_port = 0u;
static u8 rspbuf[RSP_LEN];

static inline void spi_cs_assert(void)   { XSpi_SetSlaveSelect(&Spi, SPI_SS0_MASK); }
static inline void spi_cs_deassert(void) { XSpi_SetSlaveSelect(&Spi, 0x00); }

static int spi_transfer_cs(unsigned len)
{
    int st;
    spi_cs_assert();
    st = XSpi_Transfer(&Spi, spi_tx, spi_rx, len);
    spi_cs_deassert();
    usleep(1);
    return st;
}

/* ============================================================
 * UART
 * ============================================================ */
static void uart_init(void)
{
    Xil_Out32(UART_BASEADDR + UART_CTRL_OFFSET, UART_CTRL_RST_TX | UART_CTRL_RST_RX);
}

static void uart_putc(char c)
{
    while (Xil_In32(UART_BASEADDR + UART_STAT_OFFSET) & UART_STAT_TXFULL) {}
    Xil_Out32(UART_BASEADDR + UART_TX_OFFSET, (u32)c);
}

static void uart_puts(const char *s)
{
    while (*s) {
        if (*s == '\n') uart_putc('\r');
        uart_putc(*s++);
    }
}

static void uart_put_u32(u32 v)
{
    char buf[11];
    int i = 0;
    int j;

    if (v == 0u) {
        uart_putc('0');
        return;
    }

    while (v > 0u) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }

    for (j = i - 1; j >= 0; j--) {
        uart_putc(buf[j]);
    }
}

static void uart_put_u32_2(u32 v)
{
    if (v < 10u) {
        uart_putc('0');
        uart_putc((char)('0' + v));
    } else {
        uart_put_u32(v);
    }
}

static void uart_put_hex8(u8 v)
{
    static const char hex[] = "0123456789ABCDEF";
    uart_putc(hex[(v >> 4) & 0xF]);
    uart_putc(hex[v & 0xF]);
}

static void uart_put_hex32(u32 v)
{
    uart_put_hex8((u8)((v >> 24) & 0xFF));
    uart_put_hex8((u8)((v >> 16) & 0xFF));
    uart_put_hex8((u8)((v >>  8) & 0xFF));
    uart_put_hex8((u8)(v & 0xFF));
}

static int uart_getc_nb(void)
{
    u32 st = Xil_In32(UART_BASEADDR + UART_STAT_OFFSET);
    if ((st & UART_STAT_RXVALID) != 0u) {
        return (int)(Xil_In32(UART_BASEADDR + UART_RX_OFFSET) & 0xFFu);
    }
    return -1;
}

/* ============================================================
 * GPIO
 * ============================================================ */
static void gpio_init_outputs(void)
{
    Xil_Out32(LED_GPIO_BASEADDR + GPIO_TRI_OFFSET, 0x0);
    Xil_Out32(W5500_RST_GPIO_BASEADDR + GPIO_TRI_OFFSET, 0x0);
}

static void led_set(int on)
{
    Xil_Out32(LED_GPIO_BASEADDR + GPIO_DATA_OFFSET, (on ? 1u : 0u));
}

static void w5500_reset_pin_set(int high)
{
    Xil_Out32(W5500_RST_GPIO_BASEADDR + GPIO_DATA_OFFSET, (high ? 1u : 0u));
}

/* ============================================================
 * CNN control
 * ============================================================ */
static void cnn_init(void)
{
    Xil_Out32(CNN_ACCEL_BASEADDR + CNN_GIE, 0x0);
    Xil_Out32(CNN_ACCEL_BASEADDR + CNN_IER, 0x0);
    Xil_Out32(CNN_ACCEL_BASEADDR + CNN_ISR, 0x0);
}

static void cnn_start(void)
{
    Xil_Out32(CNN_ACCEL_BASEADDR + CNN_AP_CTRL, CNN_AP_START_MASK);
}

static int cnn_is_done(void)
{
    u32 v = Xil_In32(CNN_ACCEL_BASEADDR + CNN_AP_CTRL);
    return ((v & CNN_AP_DONE_MASK) != 0u);
}

static u32 cnn_read_reg(u32 offset)
{
    return Xil_In32(CNN_ACCEL_BASEADDR + offset);
}

static u32 cnn_get_predicted_digit(void)
{
    return cnn_read_reg(CNN_PREDICTED_DATA);
}

static void dump_hls_debug_regs_hex(void)
{
    uart_puts("HLS_DBG\n");

    uart_puts("PRED=0x"); uart_put_hex32(cnn_read_reg(CNN_PREDICTED_DATA)); uart_puts("\n");

    uart_puts("P0=0x"); uart_put_hex32(cnn_read_reg(CNN_DBG_P0_DATA)); uart_puts("\n");
    uart_puts("P1=0x"); uart_put_hex32(cnn_read_reg(CNN_DBG_P1_DATA)); uart_puts("\n");
    uart_puts("P2=0x"); uart_put_hex32(cnn_read_reg(CNN_DBG_P2_DATA)); uart_puts("\n");
    uart_puts("P3=0x"); uart_put_hex32(cnn_read_reg(CNN_DBG_P3_DATA)); uart_puts("\n");
    uart_puts("P4=0x"); uart_put_hex32(cnn_read_reg(CNN_DBG_P4_DATA)); uart_puts("\n");
    uart_puts("P5=0x"); uart_put_hex32(cnn_read_reg(CNN_DBG_P5_DATA)); uart_puts("\n");
    uart_puts("P6=0x"); uart_put_hex32(cnn_read_reg(CNN_DBG_P6_DATA)); uart_puts("\n");
    uart_puts("P7=0x"); uart_put_hex32(cnn_read_reg(CNN_DBG_P7_DATA)); uart_puts("\n");
    uart_puts("SUM=0x"); uart_put_hex32(cnn_read_reg(CNN_DBG_SUM8_DATA)); uart_puts("\n");
}

static void dump_hls_regs_raw(void)
{
    u32 off;

    uart_puts("HLS_RAW\n");
    for (off = 0x00u; off <= 0xA0u; off += 4u) {
        uart_puts("OFF_0x");
        uart_put_hex32(off);
        uart_puts("=0x");
        uart_put_hex32(cnn_read_reg(off));
        uart_puts("\n");
    }
}

/* ============================================================
 * W5500 low-level
 * ============================================================ */
static void w5500_write_buf(u8 bsb, u16 addr, const u8 *data, u16 len)
{
    while (len > 0u) {
        u16 chunk = len;
        u16 i;

        if (chunk > SPI_PAYLOAD_MAX) chunk = SPI_PAYLOAD_MAX;

        spi_tx[0] = (u8)(addr >> 8);
        spi_tx[1] = (u8)(addr & 0xFF);
        spi_tx[2] = w5500_ctrl(bsb, 0);

        for (i = 0; i < chunk; i++) {
            spi_tx[3 + i] = data[i];
        }

        (void)spi_transfer_cs((unsigned)(3u + chunk));

        addr = (u16)(addr + chunk);
        data += chunk;
        len  = (u16)(len - chunk);
    }
}

static void w5500_read_buf(u8 bsb, u16 addr, u8 *data, u16 len)
{
    while (len > 0u) {
        u16 chunk = len;
        u16 i;

        if (chunk > SPI_PAYLOAD_MAX) chunk = SPI_PAYLOAD_MAX;

        spi_tx[0] = (u8)(addr >> 8);
        spi_tx[1] = (u8)(addr & 0xFF);
        spi_tx[2] = w5500_ctrl(bsb, 1);

        for (i = 0; i < chunk; i++) {
            spi_tx[3 + i] = 0x00;
        }

        (void)spi_transfer_cs((unsigned)(3u + chunk));

        for (i = 0; i < chunk; i++) {
            data[i] = spi_rx[3 + i];
        }

        addr = (u16)(addr + chunk);
        data += chunk;
        len  = (u16)(len - chunk);
    }
}

static void w5500_write8(u8 bsb, u16 addr, u8 v)
{
    w5500_write_buf(bsb, addr, &v, 1);
}

static u8 w5500_read8(u8 bsb, u16 addr)
{
    u8 v;
    w5500_read_buf(bsb, addr, &v, 1);
    return v;
}

static void w5500_write16(u8 bsb, u16 addr, u16 v)
{
    u8 b[2];
    b[0] = (u8)(v >> 8);
    b[1] = (u8)(v & 0xFF);
    w5500_write_buf(bsb, addr, b, 2);
}

static u16 w5500_read16(u8 bsb, u16 addr)
{
    u8 b[2];
    w5500_read_buf(bsb, addr, b, 2);
    return (u16)((b[0] << 8) | b[1]);
}

static u16 w5500_read16_stable(u8 bsb, u16 addr, int max_tries)
{
    u16 v0 = w5500_read16(bsb, addr);
    int i;

    for (i = 0; i < max_tries; i++) {
        u16 v1 = w5500_read16(bsb, addr);
        if (v1 == v0) return v1;
        v0 = v1;
    }
    return v0;
}

static int w5500_wait_cr_clear(u8 sock, int timeout_ms)
{
    while (timeout_ms-- > 0) {
        if (w5500_read8(BSB_SOCK_REG(sock), Sn_CR) == 0u) return 0;
        usleep(1000);
    }
    return -1;
}

/* ============================================================
 * W5500 helpers
 * ============================================================ */
static u16 size_code_to_bytes(u8 code_kb)
{
    return (u16)((u16)code_kb * 1024u);
}

static void w5500_set_sock_mem_sizes(void)
{
    u8 s;
    for (s = 0; s < 8; s++) {
        u8 tx = 0;
        u8 rx = 0;
        if (s == SOCK_UDP) {
            tx = 2;
            rx = 2;
        }
        w5500_write8(BSB_COMMON, (u16)REG_Sn_TXBUF_SIZE(s), tx);
        w5500_write8(BSB_COMMON, (u16)REG_Sn_RXBUF_SIZE(s), rx);
    }
    usleep(2000);
}

static void w5500_load_sock_mem_sizes(void)
{
    rx_buf_size_bytes[SOCK_UDP] =
        size_code_to_bytes(w5500_read8(BSB_COMMON, (u16)REG_Sn_RXBUF_SIZE(SOCK_UDP)));

    /*
     * Socket 0 TX buffer is configured/kept at 2 KB.
     * We use this size only for TX ring wrapping.
     */
    tx_buf_size_bytes[SOCK_UDP] = 2048u;
}

static void w5500_rx_read_ring(u8 sock, u16 rx_rd, u8 *data, u16 len)
{
    u16 size = rx_buf_size_bytes[sock];
    u16 mask;
    u16 off;
    u16 first;
    u16 second;

    if (size == 0u) return;

    mask  = (u16)(size - 1u);
    off   = (u16)(rx_rd & mask);
    first = len;

    if ((u16)(off + len) > size) {
        first = (u16)(size - off);
    }

    second = (u16)(len - first);

    w5500_read_buf(BSB_SOCK_RX(sock), off, data, first);
    if (second > 0u) {
        w5500_read_buf(BSB_SOCK_RX(sock), 0x0000, data + first, second);
    }
}

static void w5500_tx_write_ring(u8 sock, u16 tx_wr, const u8 *data, u16 len)
{
    u16 size = tx_buf_size_bytes[sock];
    u16 mask;
    u16 off;
    u16 first;
    u16 second;

    if (size == 0u) return;

    mask  = (u16)(size - 1u);
    off   = (u16)(tx_wr & mask);
    first = len;

    if ((u16)(off + len) > size) {
        first = (u16)(size - off);
    }

    second = (u16)(len - first);

    w5500_write_buf(BSB_SOCK_TX(sock), off, data, first);
    if (second > 0u) {
        w5500_write_buf(BSB_SOCK_TX(sock), 0x0000, data + first, second);
    }
}

/* ============================================================
 * W5500 high-level
 * ============================================================ */
static void w5500_soft_reset(void)
{
    w5500_write8(BSB_COMMON, REG_MR, 0x80);
    usleep(5000);
}

static void w5500_set_network(void)
{
    w5500_write_buf(BSB_COMMON, REG_GAR0,  GW,   4);
    w5500_write_buf(BSB_COMMON, REG_SUBR0, MASK, 4);
    w5500_write_buf(BSB_COMMON, REG_SHAR0, MAC,  6);
    w5500_write_buf(BSB_COMMON, REG_SIPR0, IP,   4);
}

static void sock_udp_open(u8 sock, u16 port)
{
    w5500_write8(BSB_SOCK_REG(sock), Sn_CR, CMD_CLOSE);
    (void)w5500_wait_cr_clear(sock, 100);

    w5500_write8(BSB_SOCK_REG(sock), Sn_MR, MODE_UDP);
    w5500_write16(BSB_SOCK_REG(sock), Sn_PORT, port);

    w5500_write8(BSB_SOCK_REG(sock), Sn_IMR, IR_ALL);
    w5500_write8(BSB_SOCK_REG(sock), Sn_IR,  IR_ALL);

    w5500_write8(BSB_SOCK_REG(sock), Sn_CR, CMD_OPEN);
    (void)w5500_wait_cr_clear(sock, 100);
}

static int sock_udp_recv(u8 sock, u8 *payload, u16 maxlen, u16 *outlen)
{
    u16 rxsz = w5500_read16_stable(BSB_SOCK_REG(sock), Sn_RX_RSR, 8);
    u16 rx_rd;
    u16 orig_len;
    u16 dlen;

    if (rxsz < 8u) return 0;

    rx_rd = w5500_read16_stable(BSB_SOCK_REG(sock), Sn_RX_RD, 8);

    w5500_rx_read_ring(sock, rx_rd, udp_hdr, 8);

    last_udp_src_ip[0] = udp_hdr[0];
    last_udp_src_ip[1] = udp_hdr[1];
    last_udp_src_ip[2] = udp_hdr[2];
    last_udp_src_ip[3] = udp_hdr[3];
    last_udp_src_port = (u16)(((u16)udp_hdr[4] << 8) | udp_hdr[5]);

    orig_len = (u16)((udp_hdr[6] << 8) | udp_hdr[7]);
    dlen = (orig_len > maxlen) ? maxlen : orig_len;

    w5500_rx_read_ring(sock, (u16)(rx_rd + 8u), payload, dlen);

    w5500_write16(BSB_SOCK_REG(sock), Sn_RX_RD, (u16)(rx_rd + 8u + orig_len));
    w5500_write8(BSB_SOCK_REG(sock), Sn_CR, CMD_RECV);
    (void)w5500_wait_cr_clear(sock, 50);

    *outlen = dlen;
    return 1;
}

static int sock_udp_wait_tx_free(u8 sock, u16 len, int timeout_ms)
{
    while (timeout_ms-- > 0) {
        u16 free_bytes = w5500_read16_stable(BSB_SOCK_REG(sock), Sn_TX_FSR, 8);
        if (free_bytes >= len) {
            return 0;
        }
        usleep(1000);
    }

    return -1;
}

static int sock_udp_sendto(u8 sock, const u8 dst_ip[4], u16 dst_port, const u8 *payload, u16 len)
{
    u16 tx_wr;

    if (len == 0u) {
        return 0;
    }

    if (sock_udp_wait_tx_free(sock, len, 100) != 0) {
        return -1;
    }

    w5500_write_buf(BSB_SOCK_REG(sock), Sn_DIPR, dst_ip, 4);
    w5500_write16(BSB_SOCK_REG(sock), Sn_DPORT, dst_port);

    tx_wr = w5500_read16_stable(BSB_SOCK_REG(sock), Sn_TX_WR, 8);

    w5500_tx_write_ring(sock, tx_wr, payload, len);

    w5500_write16(BSB_SOCK_REG(sock), Sn_TX_WR, (u16)(tx_wr + len));
    w5500_write8(BSB_SOCK_REG(sock), Sn_CR, CMD_SEND);

    if (w5500_wait_cr_clear(sock, 100) != 0) {
        return -1;
    }

    return 0;
}

/* ============================================================
 * SPI init
 * ============================================================ */
static int init_spi(void)
{
    XSpi_Config cfg;

    cfg.BaseAddress  = SPI_BASEADDR;
    cfg.HasFifos     = XPAR_XSPI_0_HASFIFOS;
    cfg.SlaveOnly    = XPAR_XSPI_0_SLAVEONLY;
    cfg.NumSlaveBits = XPAR_XSPI_0_NUM_SS_BITS;

    if (XSpi_CfgInitialize(&Spi, &cfg, cfg.BaseAddress) != XST_SUCCESS) {
        return XST_FAILURE;
    }

    XSpi_SetOptions(&Spi, XSP_MASTER_OPTION | XSP_MANUAL_SSELECT_OPTION);
    XSpi_Start(&Spi);
    XSpi_IntrGlobalDisable(&Spi);
    spi_cs_deassert();
    return XST_SUCCESS;
}

/* ============================================================
 * W5500 reset
 * ============================================================ */
static void w5500_hw_reset(void)
{
    w5500_reset_pin_set(0);
    usleep(20000);
    w5500_reset_pin_set(1);
    usleep(100000);
}

static void w5500_reinit_full(void)
{
    w5500_hw_reset();
    w5500_soft_reset();
    w5500_set_sock_mem_sizes();
    w5500_load_sock_mem_sizes();
    w5500_set_network();
    sock_udp_open(SOCK_UDP, UDP_PORT_LOCAL);
}

/* ============================================================
 * Shared BRAM write / debug
 * ============================================================ */
static u32 pack_image_word_le(const u8 *img, u32 word_idx)
{
    u32 base = 4u * word_idx;

    return ((u32)img[base + 0u]) |
           ((u32)img[base + 1u] << 8) |
           ((u32)img[base + 2u] << 16) |
           ((u32)img[base + 3u] << 24);
}

static u32 image_sum_u8(const u8 *img)
{
    u32 i;
    u32 sum = 0u;

    for (i = 0u; i < IMG_PIXELS; i++) {
        sum += (u32)img[i];
    }

    return sum;
}

static u32 image_nonzero_count(const u8 *img)
{
    u32 i;
    u32 nz = 0u;

    for (i = 0u; i < IMG_PIXELS; i++) {
        if (img[i] != 0u) {
            nz++;
        }
    }

    return nz;
}

static void clear_shared_bram(void)
{
    u32 w;

    for (w = 0u; w < BRAM_WORD_COUNT; w++) {
        Xil_Out32(SHARED_BRAM_BASEADDR + (w * 4u), 0u);
    }
}

static void write_image_to_shared_bram_packed(const u8 *img)
{
    u32 w;

    for (w = 0u; w < BRAM_WORD_COUNT; w++) {
        Xil_Out32(SHARED_BRAM_BASEADDR + (w * 4u), pack_image_word_le(img, w));
    }
}

static void write_test_pattern_to_bram(void)
{
    u32 w;

    for (w = 0u; w < BRAM_WORD_COUNT; w++) {
        u32 base = 4u * w;
        u32 word = ((base + 0u) & 0xFFu) |
                   (((base + 1u) & 0xFFu) << 8) |
                   (((base + 2u) & 0xFFu) << 16) |
                   (((base + 3u) & 0xFFu) << 24);

        Xil_Out32(SHARED_BRAM_BASEADDR + (w * 4u), word);
    }
}

static void dump_bram_words(u32 nwords)
{
    u32 i;

    if (nwords > BRAM_WORD_COUNT) {
        nwords = BRAM_WORD_COUNT;
    }

    uart_puts("BRAM_WORDS\n");

    for (i = 0u; i < nwords; i++) {
        uart_puts("W");
        uart_put_u32_2(i);
        uart_puts("=0x");
        uart_put_hex32(Xil_In32(SHARED_BRAM_BASEADDR + (i * 4u)));
        uart_puts("\n");
    }
}

static void dump_first_bram_words(void)
{
    dump_bram_words(8u);
}

static void dump_image_bytes(const u8 *img, u32 nbytes)
{
    u32 i;

    if (nbytes > IMG_PIXELS) {
        nbytes = IMG_PIXELS;
    }

    uart_puts("IMG_BYTES\n");

    for (i = 0u; i < nbytes; i++) {
        uart_puts("B");
        uart_put_u32_2(i);
        uart_puts("=0x");
        uart_put_hex8(img[i]);
        uart_puts("\n");
    }
}

static int verify_bram_against_image(const u8 *img)
{
    u32 w;
    u32 mismatch_count = 0u;

    for (w = 0u; w < BRAM_WORD_COUNT; w++) {
        u32 exp = pack_image_word_le(img, w);
        u32 got = Xil_In32(SHARED_BRAM_BASEADDR + (w * 4u));

        if (got != exp) {
            if (mismatch_count < 8u) {
                uart_puts("BRAM_MISMATCH W");
                uart_put_u32_2(w);
                uart_puts(" EXP=0x");
                uart_put_hex32(exp);
                uart_puts(" GOT=0x");
                uart_put_hex32(got);
                uart_puts("\n");
            }
            mismatch_count++;
        }
    }

    if (mismatch_count == 0u) {
        uart_puts("BRAM_VERIFY_OK\n");
        return 1;
    }

    uart_puts("BRAM_VERIFY_FAIL COUNT=");
    uart_put_u32(mismatch_count);
    uart_puts("\n");
    return 0;
}

static void verify_hls_dbg_vs_bram(void)
{
    u32 hls_dbg[8];
    u32 i;
    u32 mismatch_count = 0u;

    hls_dbg[0] = cnn_read_reg(CNN_DBG_P0_DATA);
    hls_dbg[1] = cnn_read_reg(CNN_DBG_P1_DATA);
    hls_dbg[2] = cnn_read_reg(CNN_DBG_P2_DATA);
    hls_dbg[3] = cnn_read_reg(CNN_DBG_P3_DATA);
    hls_dbg[4] = cnn_read_reg(CNN_DBG_P4_DATA);
    hls_dbg[5] = cnn_read_reg(CNN_DBG_P5_DATA);
    hls_dbg[6] = cnn_read_reg(CNN_DBG_P6_DATA);
    hls_dbg[7] = cnn_read_reg(CNN_DBG_P7_DATA);

    for (i = 0u; i < 8u; i++) {
        u32 bram_word = Xil_In32(SHARED_BRAM_BASEADDR + (i * 4u));

        if (hls_dbg[i] != bram_word) {
            uart_puts("HLS_BRAM_MISMATCH P");
            uart_put_u32(i);
            uart_puts(" HLS=0x");
            uart_put_hex32(hls_dbg[i]);
            uart_puts(" BRAM=0x");
            uart_put_hex32(bram_word);
            uart_puts("\n");
            mismatch_count++;
        }
    }

    if (mismatch_count == 0u) {
        uart_puts("HLS_BRAM_VERIFY_OK\n");
    } else {
        uart_puts("HLS_BRAM_VERIFY_FAIL COUNT=");
        uart_put_u32(mismatch_count);
        uart_puts("\n");
    }
}

static u32 run_cnn_and_get_prediction(int *ok)
{
    u32 cnt = 0u;
    const u32 MAX_WAIT = 5000000u;

    cnn_start();

    while (!cnn_is_done()) {
        cnt++;
        if (cnt >= MAX_WAIT) {
            *ok = 0;
            uart_puts("CNN_TIMEOUT_CTRL=0x");
            uart_put_hex32(cnn_read_reg(CNN_AP_CTRL));
            uart_puts("\n");
            return 0u;
        }
    }

    *ok = 1;
    return cnn_get_predicted_digit();
}

static u32 be32_to_u32(const u8 *p)
{
    return ((u32)p[0] << 24) |
           ((u32)p[1] << 16) |
           ((u32)p[2] << 8)  |
           ((u32)p[3]);
}

static void u32_to_be32(u32 v, u8 *p)
{
    p[0] = (u8)((v >> 24) & 0xFFu);
    p[1] = (u8)((v >> 16) & 0xFFu);
    p[2] = (u8)((v >> 8)  & 0xFFu);
    p[3] = (u8)(v & 0xFFu);
}

static int send_result_udp_response(u32 seq, u8 label, u32 pred, int ok)
{
    rspbuf[0] = (u8)RSP_MAGIC0;
    rspbuf[1] = (u8)RSP_MAGIC1;
    rspbuf[2] = (u8)RSP_MAGIC2;
    rspbuf[3] = (u8)RSP_MAGIC3;

    u32_to_be32(seq, &rspbuf[4]);

    rspbuf[8]  = (u8)(pred & 0xFFu);
    rspbuf[9]  = (u8)(ok ? 1u : 0u);
    rspbuf[10] = label;
    rspbuf[11] = 0u;

    return sock_udp_sendto(SOCK_UDP, last_udp_src_ip, last_udp_src_port, rspbuf, RSP_LEN);
}

/* ============================================================
 * main
 * ============================================================ */
int main(void)
{
    u16 rxlen = 0u;

    gpio_init_outputs();
    uart_init();
    cnn_init();

    if (init_spi() != XST_SUCCESS) {
        while (1) {}
    }

    led_set(0);
    w5500_reset_pin_set(1);

    uart_puts("BOOT\n");

    w5500_reinit_full();

    uart_puts("NET OK\n");
    uart_puts("Commands:\n");
    uart_puts("  t = write pattern 0..195 to BRAM\n");
    uart_puts("  r = dump first 8 BRAM words\n");
    uart_puts("  R = dump all 49 BRAM words\n");
    uart_puts("  g = run CNN on current BRAM\n");
    uart_puts("  d = dump HLS debug regs\n");
    uart_puts("  x = dump raw HLS AXI-Lite regs\n");

    while (1)
    {
        int c = uart_getc_nb();

        /* ====================================================
         * Manual debug commands via UART
         * These commands keep all debug prints enabled.
         * ==================================================== */
        if (c >= 0) {
            if (c == 't') {
                clear_shared_bram();
                write_test_pattern_to_bram();
                uart_puts("PATTERN WRITTEN\n");
            }
            else if (c == 'r') {
                dump_first_bram_words();
            }
            else if (c == 'R') {
                dump_bram_words(BRAM_WORD_COUNT);
            }
            else if (c == 'g') {
                int ok;
                u32 pred;

                uart_puts("RUN_CURRENT_BRAM\n");
                dump_first_bram_words();

                led_set(1);
                pred = run_cnn_and_get_prediction(&ok);
                led_set(0);

                dump_hls_debug_regs_hex();
                verify_hls_dbg_vs_bram();

                if (ok) {
                    uart_puts("PRED=");
                    uart_put_u32(pred);
                    uart_puts("\n");
                } else {
                    uart_puts("TIMEOUT\n");
                }
            }
            else if (c == 'd') {
                dump_hls_debug_regs_hex();
                verify_hls_dbg_vs_bram();
            }
            else if (c == 'x') {
                dump_hls_regs_raw();
            }
        }

        /* ====================================================
         * UDP runtime path
         * Minimal output for large image tests.
         *
         * Prints only:
         * RESULT SEQ=<seq> GT=<label> PRED=<pred> OK/WRONG
         * ==================================================== */
        if (sock_udp_recv(SOCK_UDP, rxbuf, (u16)sizeof(rxbuf), &rxlen)) {
            if (rxlen == PKT_LEN &&
                rxbuf[0] == PKT_MAGIC0 &&
                rxbuf[1] == PKT_MAGIC1 &&
                rxbuf[2] == PKT_MAGIC2 &&
                rxbuf[3] == PKT_MAGIC3) {

                u32 seq   = be32_to_u32(&rxbuf[4]);
                u8  label = rxbuf[8];
                const u8 *img = &rxbuf[9];
                int ok;
                u32 pred;

                clear_shared_bram();
                write_image_to_shared_bram_packed(img);

                led_set(1);
                pred = run_cnn_and_get_prediction(&ok);
                led_set(0);

                uart_puts("RESULT SEQ=");
                uart_put_u32(seq);
                uart_puts(" GT=");
                uart_put_u32((u32)label);

                if (ok) {
                    uart_puts(" PRED=");
                    uart_put_u32(pred);

                    if (label <= 9u) {
                        if (pred == (u32)label) {
                            uart_puts(" OK");
                        } else {
                            uart_puts(" WRONG");
                        }
                    } else {
                        uart_puts(" WEB");
                    }
                } else {
                    uart_puts(" TIMEOUT");
                }

                if (send_result_udp_response(seq, label, pred, ok) == 0) {
                    uart_puts(" UDP_RSP_OK\n");
                } else {
                    uart_puts(" UDP_RSP_FAIL\n");
                }
            }
            else {
                uart_puts("RX_BAD LEN=");
                uart_put_u32((u32)rxlen);
                uart_puts("\n");
            }
        }

        usleep(1000);
    }

    return 0;
}