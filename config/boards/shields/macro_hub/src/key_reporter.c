#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/drivers/uart.h>
#include <string.h>
#include <stdio.h>

#define MUX_ADDR        0x70
#define NUM_CHANNELS    6
#define ADDRS_PER_CH    4

static const uint8_t key_addrs[ADDRS_PER_CH] = { 0x18, 0x19, 0x1A, 0x1B };

static const struct device *i2c_dev;
static const struct device *cdc_dev;

/* Select one channel on the TCA9548A mux */
static void mux_select(int ch) {
    uint8_t val = (ch >= 0 && ch < 8) ? (1 << ch) : 0x00;
    i2c_write(i2c_dev, &val, 1, MUX_ADDR);
}

/* Returns true if a PCA9557 responds at this address */
static bool probe(uint8_t addr) {
    uint8_t buf;
    return i2c_read(i2c_dev, &buf, 1, addr) == 0;
}

/* Send a string out over USB serial */
static void cdc_send(const char *s) {
    while (*s) {
        uart_poll_out(cdc_dev, *s++);
    }
}

/* Scans all 6 channels and builds a response like:
   KEYS:0:18,0:19,1:18,2:1a\n
   Sends it over USB serial. */
static void run_scan(void) {
    char resp[256];
    int  pos   = 0;
    bool first = true;

    pos += snprintf(resp + pos, sizeof(resp) - pos, "KEYS:");

    for (int ch = 0; ch < NUM_CHANNELS; ch++) {
        mux_select(ch);
        k_sleep(K_MSEC(5));  /* let the mux settle */

        for (int a = 0; a < ADDRS_PER_CH; a++) {
            if (probe(key_addrs[a])) {
                if (!first) {
                    pos += snprintf(resp + pos, sizeof(resp) - pos, ",");
                }
                pos += snprintf(resp + pos, sizeof(resp) - pos,
                                "%d:%02x", ch, key_addrs[a]);
                first = false;
            }
        }
    }

    mux_select(-1);  /* deselect all channels */
    pos += snprintf(resp + pos, sizeof(resp) - pos, "\n");
    cdc_send(resp);
}

/* Background thread — waits for "SCAN\n" from the configure tab,
   then replies with the list of detected keys. */
static void reporter_thread(void *a, void *b, void *c) {
    k_sleep(K_SECONDS(2));  /* wait for USB to finish initialising */

    char    buf[16];
    uint8_t idx = 0;

    while (1) {
        uint8_t ch;
        if (uart_poll_in(cdc_dev, &ch) == 0) {
            if (ch == '\n' || idx >= sizeof(buf) - 1) {
                buf[idx] = '\0';
                idx = 0;
                if (strncmp(buf, "SCAN", 4) == 0) {
                    run_scan();
                }
            } else {
                buf[idx++] = ch;
            }
        } else {
            k_sleep(K_MSEC(10));
        }
    }
}

K_THREAD_DEFINE(reporter_tid, 1024,
                reporter_thread, NULL, NULL, NULL,
                K_PRIO_PREEMPT(10), 0, 0);

static int key_reporter_init(void) {
    i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));
    cdc_dev = DEVICE_DT_GET(DT_NODELABEL(cdc_acm_uart0));

    if (!device_is_ready(i2c_dev) || !device_is_ready(cdc_dev)) {
        return -ENODEV;
    }
    return 0;
}

SYS_INIT(key_reporter_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
