/*
 * StitchKeys layout fingerprint module
 *
 * Every 500 ms: probe all 6 TCA9548A channels for PCF8574 devices at
 * addresses 0x20–0x27, build a canonical fingerprint string, and emit it
 * over CDC ACM USB serial whenever the layout changes.
 *
 * Serial output (one line, newline-terminated):
 *   LAYOUT:ch0:0x21,ch1:0x20,ch1:0x23\n
 *   LAYOUT:empty\n   <- no devices found
 *
 * The fingerprint is order-independent within a channel (addresses sorted
 * ascending) and covers only channels that have at least one device.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdio.h>

/* ── Hardware constants ─────────────────────────────────────────────── */

#define TCA9548A_ADDR    0x70
#define PCF8574_ADDR_MIN 0x20
#define PCF8574_ADDR_MAX 0x27
#define PCF8574_NUM_ADDR (PCF8574_ADDR_MAX - PCF8574_ADDR_MIN + 1)  /* 8 */
#define NUM_CHANNELS     6

/* ── Timing ─────────────────────────────────────────────────────────── */

#define POLL_MS    500   /* layout re-scan period */
#define STARTUP_MS 2500  /* grace period for USB CDC ACM to enumerate */
#define MUX_SETTLE_US 500 /* TCA9548A channel-select settling time */

/* ── Thread parameters ──────────────────────────────────────────────── */

#define FP_STACK_SIZE 2048
#define FP_PRIORITY   10   /* well below ZMK main/kscan threads */

/* ── Module state ───────────────────────────────────────────────────── */

static const struct device *i2c_bus;
static const struct device *cdc_uart;

/*
 * Per-channel presence bitmask.
 * Bit N set → PCF8574 at address (0x20 + N) responded on that channel.
 */
static uint8_t active_layout[NUM_CHANNELS];
static char    last_fp[180] = "";

/* ── TCA9548A channel control ───────────────────────────────────────── */

/*
 * Select one TCA9548A channel by writing BIT(ch) to its address.
 * Pass ch >= NUM_CHANNELS to deselect all channels (write 0x00).
 */
static void mux_select(uint8_t ch)
{
    uint8_t sel = (ch < NUM_CHANNELS) ? (uint8_t)BIT(ch) : 0U;
    /* Ignore return value: I2C errors during probing are expected */
    (void)i2c_write(i2c_bus, &sel, 1, TCA9548A_ADDR);
}

/* ── PCF8574 presence probe ─────────────────────────────────────────── */

/*
 * Attempt a one-byte I2C read from addr on the currently-selected channel.
 * Returns true if a device acknowledged, false on NACK / error.
 */
static bool pcf_present(uint8_t addr)
{
    uint8_t buf;
    return i2c_read(i2c_bus, &buf, 1, addr) == 0;
}

/* ── Full layout scan ───────────────────────────────────────────────── */

/*
 * Iterate all 6 channels; on each, probe all 8 PCF8574 addresses.
 * Results written into out[NUM_CHANNELS] as address-presence bitmasks.
 * Leaves TCA9548A with all channels deselected.
 */
static void layout_scan(uint8_t out[NUM_CHANNELS])
{
    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
        mux_select(ch);
        k_busy_wait(MUX_SETTLE_US);

        out[ch] = 0;
        for (uint8_t n = 0; n < PCF8574_NUM_ADDR; n++) {
            if (pcf_present(PCF8574_ADDR_MIN + n)) {
                out[ch] |= BIT(n);
            }
        }
    }

    mux_select(0xFF); /* deselect all channels */
}

/* ── Fingerprint builder ────────────────────────────────────────────── */

/*
 * Convert layout bitmasks into a canonical, comma-separated string.
 * Format: "chC:0xAA" per (channel, address) pair, channels ascending,
 * addresses ascending within each channel.
 * Writes "empty" if no devices present.
 */
static void fp_build(const uint8_t layout[NUM_CHANNELS],
                     char *buf, size_t sz)
{
    int  off = 0;
    bool any = false;

    for (uint8_t ch = 0; ch < NUM_CHANNELS; ch++) {
        for (uint8_t n = 0; n < PCF8574_NUM_ADDR; n++) {
            if (!(layout[ch] & BIT(n))) {
                continue;
            }
            if (any) {
                off += snprintf(buf + off, sz - (size_t)off, ",");
            }
            off += snprintf(buf + off, sz - (size_t)off,
                            "ch%u:0x%02x",
                            (unsigned)ch,
                            (unsigned)(PCF8574_ADDR_MIN + n));
            any = true;
        }
    }

    if (!any) {
        snprintf(buf, sz, "empty");
    }
}

/* ── CDC ACM output ─────────────────────────────────────────────────── */

static void cdc_send(const char *s)
{
    for (; *s != '\0'; s++) {
        uart_poll_out(cdc_uart, (unsigned char)*s);
    }
}

/* ── Fingerprint thread ─────────────────────────────────────────────── */

static void fp_thread_fn(void *a, void *b, void *c)
{
    ARG_UNUSED(a);
    ARG_UNUSED(b);
    ARG_UNUSED(c);

    i2c_bus  = DEVICE_DT_GET(DT_NODELABEL(i2c0));
    cdc_uart = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

    if (!device_is_ready(i2c_bus) || !device_is_ready(cdc_uart)) {
        return;
    }

    /* Wait for USB CDC ACM to enumerate on the host */
    k_sleep(K_MSEC(STARTUP_MS));

    while (true) {
        uint8_t new_layout[NUM_CHANNELS] = {0};
        layout_scan(new_layout);

        char fp[160];
        fp_build(new_layout, fp, sizeof(fp));

        if (strcmp(fp, last_fp) != 0) {
            memcpy(last_fp,       fp,         sizeof(last_fp));
            memcpy(active_layout, new_layout, sizeof(active_layout));

            char line[180];
            snprintf(line, sizeof(line), "LAYOUT:%s\n", fp);
            cdc_send(line);
        }

        k_sleep(K_MSEC(POLL_MS));
    }
}

K_THREAD_DEFINE(fp_tid, FP_STACK_SIZE,
                fp_thread_fn, NULL, NULL, NULL,
                FP_PRIORITY, 0, 0);
