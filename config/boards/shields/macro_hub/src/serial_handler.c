/*
 * Serial handler for stitchkeys macro_hub.
 *
 * Listens on CDC ACM UART for commands from the configure website:
 *   SK:VERSION\n          → SK:VERSION:1\n
 *   SK:KEY:<s>:<m>:<u>\n  → SK:OK\n   (set slot s, modifier m, usage u — all hex)
 *   SK:DUMP\n             → SK:DUMP:<m0>:<u0>,<m1>:<u1>,...\n
 *
 * Settings are persisted to NVS under the "skkeys" namespace.
 */

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/settings/settings.h>
#include <zephyr/init.h>
#include <string.h>
#include "key_overrides.h"

#define UART_DEVICE_NODE DT_NODELABEL(cdc_acm_uart0)
#define RX_BUF_SIZE 128

struct key_override key_overrides[MACRO_HUB_KEY_COUNT];

static uint8_t rx_buf[RX_BUF_SIZE];
static int rx_pos;

/* ── Helpers ──────────────────────────────────────────────────────────────── */

static const struct device *uart_dev(void)
{
    return DEVICE_DT_GET(UART_DEVICE_NODE);
}

static void send_char(uint8_t c)
{
    uart_poll_out(uart_dev(), c);
}

static void send_str(const char *s)
{
    while (*s) send_char((uint8_t)*s++);
}

static void send_hex2(uint8_t v)
{
    const char hex[] = "0123456789abcdef";
    send_char(hex[v >> 4]);
    send_char(hex[v & 0xf]);
}

/* strtol is available in Zephyr's minimal libc */
static long parse_hex(const char *s, char **end)
{
    return strtol(s, end, 16);
}

static long parse_dec(const char *s, char **end)
{
    return strtol(s, end, 10);
}

/* ── Command processor ────────────────────────────────────────────────────── */

static void process_line(const char *line)
{
    if (strcmp(line, "SK:VERSION") == 0) {
        send_str("SK:VERSION:1\n");
        return;
    }

    if (strcmp(line, "SK:DUMP") == 0) {
        send_str("SK:DUMP:");
        for (int i = 0; i < MACRO_HUB_KEY_COUNT; i++) {
            if (i > 0) send_char(',');
            send_hex2(key_overrides[i].modifier);
            send_char(':');
            send_hex2(key_overrides[i].usage);
        }
        send_char('\n');
        return;
    }

    if (strncmp(line, "SK:KEY:", 7) == 0) {
        /* SK:KEY:<slot_dec>:<modifier_hex>:<usage_hex> */
        const char *p = line + 7;
        char *end;
        long slot = parse_dec(p, &end);
        if (*end != ':' || slot < 0 || slot >= MACRO_HUB_KEY_COUNT) {
            send_str("SK:ERR:bad slot\n");
            return;
        }
        long mod = parse_hex(end + 1, &end);
        if (*end != ':') {
            send_str("SK:ERR:bad modifier\n");
            return;
        }
        long usage = parse_hex(end + 1, &end);

        key_overrides[slot].modifier = (uint8_t)mod;
        key_overrides[slot].usage    = (uint8_t)usage;
        key_overrides_save();
        send_str("SK:OK\n");
        return;
    }

    send_str("SK:ERR:unknown command\n");
}

/* ── UART IRQ callback ────────────────────────────────────────────────────── */

static void uart_irq_cb(const struct device *dev, void *user_data)
{
    ARG_UNUSED(user_data);

    if (!uart_irq_update(dev)) return;

    while (uart_irq_rx_ready(dev)) {
        uint8_t ch;
        if (uart_fifo_read(dev, &ch, 1) <= 0) break;

        if (ch == '\n' || ch == '\r') {
            if (rx_pos > 0) {
                rx_buf[rx_pos] = '\0';
                process_line((char *)rx_buf);
                rx_pos = 0;
            }
        } else if (rx_pos < RX_BUF_SIZE - 1) {
            rx_buf[rx_pos++] = ch;
        }
    }
}

/* ── Zephyr Settings backend ──────────────────────────────────────────────── */

static int sk_settings_set(const char *key, size_t len,
                            settings_read_cb read_cb, void *cb_arg)
{
    if (strcmp(key, "bindings") == 0 && len == sizeof(key_overrides)) {
        read_cb(cb_arg, key_overrides, len);
    }
    return 0;
}

static struct settings_handler sk_settings = {
    .name  = "skkeys",
    .h_set = sk_settings_set,
};

void key_overrides_save(void)
{
    settings_save_one("skkeys/bindings", key_overrides, sizeof(key_overrides));
}

void key_overrides_load(void)
{
    settings_register(&sk_settings);
    settings_load_subtree("skkeys");
}

/* ── Init ─────────────────────────────────────────────────────────────────── */

static int serial_handler_init(void)
{
    const struct device *dev = uart_dev();
    if (!device_is_ready(dev)) return -ENODEV;

    uart_irq_callback_user_data_set(dev, uart_irq_cb, NULL);
    uart_irq_rx_enable(dev);

    send_str("SK:READY\n");
    return 0;
}

SYS_INIT(serial_handler_init, APPLICATION, CONFIG_APPLICATION_INIT_PRIORITY);
