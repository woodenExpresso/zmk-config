/*
 * PCF8574 8-bit I2C GPIO expander driver.
 *
 * Protocol: no register address byte.
 *   Read  — i2c_read 1 byte  → current logic levels on P0–P7
 *   Write — i2c_write 1 byte → bit=1 quasi-bidirectional input (internal pull-up)
 *                               bit=0 output driven low
 *
 * Power-on reset: all pins quasi-bidirectional (0xFF).
 */

#define DT_DRV_COMPAT stitchkeys_pcf8574

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/util.h>

struct pcf8574_config {
	struct gpio_driver_config common;
	struct i2c_dt_spec i2c;
};

struct pcf8574_data {
	struct gpio_driver_data common;
	uint8_t latch; /* 1 = quasi-input/high, 0 = output-low */
};

static int pcf8574_pin_configure(const struct device *dev, gpio_pin_t pin,
				  gpio_flags_t flags)
{
	const struct pcf8574_config *cfg = dev->config;
	struct pcf8574_data *data = dev->data;

	if (flags & GPIO_PULL_DOWN) {
		return -ENOTSUP;
	}

	if (flags & GPIO_INPUT) {
		data->latch |= BIT(pin); /* quasi-bidi: write 1 to use as input */
	} else {
		if (flags & GPIO_OUTPUT_INIT_HIGH) {
			data->latch |= BIT(pin);
		} else {
			data->latch &= ~BIT(pin);
		}
	}

	return i2c_write_dt(&cfg->i2c, &data->latch, 1);
}

static int pcf8574_port_get_raw(const struct device *dev,
				 gpio_port_value_t *value)
{
	const struct pcf8574_config *cfg = dev->config;
	uint8_t buf;
	int ret;

	ret = i2c_read_dt(&cfg->i2c, &buf, 1);
	if (ret < 0) {
		return ret;
	}
	*value = buf;
	return 0;
}

static int pcf8574_port_set_masked_raw(const struct device *dev,
					gpio_port_pins_t mask,
					gpio_port_value_t value)
{
	const struct pcf8574_config *cfg = dev->config;
	struct pcf8574_data *data = dev->data;

	data->latch = (data->latch & ~mask) | (value & mask);
	return i2c_write_dt(&cfg->i2c, &data->latch, 1);
}

static int pcf8574_port_set_bits_raw(const struct device *dev,
				      gpio_port_pins_t pins)
{
	return pcf8574_port_set_masked_raw(dev, pins, pins);
}

static int pcf8574_port_clear_bits_raw(const struct device *dev,
					gpio_port_pins_t pins)
{
	return pcf8574_port_set_masked_raw(dev, pins, 0U);
}

static int pcf8574_port_toggle_bits(const struct device *dev,
				     gpio_port_pins_t pins)
{
	const struct pcf8574_config *cfg = dev->config;
	struct pcf8574_data *data = dev->data;

	data->latch ^= pins;
	return i2c_write_dt(&cfg->i2c, &data->latch, 1);
}

static int pcf8574_pin_interrupt_configure(const struct device *dev,
					    gpio_pin_t pin,
					    enum gpio_int_mode mode,
					    enum gpio_int_trig trig)
{
	return -ENOTSUP;
}

static int pcf8574_manage_callback(const struct device *dev,
				    struct gpio_callback *callback,
				    bool set)
{
	return -ENOTSUP;
}

static const struct gpio_driver_api pcf8574_gpio_api = {
	.pin_configure           = pcf8574_pin_configure,
	.port_get_raw            = pcf8574_port_get_raw,
	.port_set_masked_raw     = pcf8574_port_set_masked_raw,
	.port_set_bits_raw       = pcf8574_port_set_bits_raw,
	.port_clear_bits_raw     = pcf8574_port_clear_bits_raw,
	.port_toggle_bits        = pcf8574_port_toggle_bits,
	.pin_interrupt_configure = pcf8574_pin_interrupt_configure,
	.manage_callback         = pcf8574_manage_callback,
};

static int pcf8574_init(const struct device *dev)
{
	const struct pcf8574_config *cfg = dev->config;
	struct pcf8574_data *data = dev->data;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		return -ENODEV;
	}

	/* All pins quasi-bidirectional inputs — matches power-on reset state */
	data->latch = 0xFF;
	return i2c_write_dt(&cfg->i2c, &data->latch, 1);
}

#define PCF8574_DEVICE(inst)						       \
	static const struct pcf8574_config pcf8574_config_##inst = {	       \
		.common = {						       \
			.port_pin_mask = (gpio_port_pins_t)0xFF,	       \
		},							       \
		.i2c = I2C_DT_SPEC_INST_GET(inst),			       \
	};								       \
	static struct pcf8574_data pcf8574_data_##inst;			       \
	DEVICE_DT_INST_DEFINE(inst,					       \
			      pcf8574_init, NULL,			       \
			      &pcf8574_data_##inst,			       \
			      &pcf8574_config_##inst,			       \
			      POST_KERNEL,				       \
			      80,					       \
			      &pcf8574_gpio_api);

DT_INST_FOREACH_STATUS_OKAY(PCF8574_DEVICE)
