/*
 * Minimal NXP PCA9557 I2C GPIO expander driver.
 *
 * Register map (identical to PCA9534 and TCA6408A):
 *   0x00  Input port   (read-only:  current pin logic levels)
 *   0x01  Output port  (read/write: output latch)
 *   0x02  Polarity inv (read/write: inverts input readings)
 *   0x03  Config       (read/write: 1 = input, 0 = output)
 *
 * PCA9557 has internal ~100 kΩ pull-ups on all I/O pins.
 */

#define DT_DRV_COMPAT nxp_pca9557

#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/util.h>

#define PCA9557_REG_INPUT    0x00
#define PCA9557_REG_OUTPUT   0x01
#define PCA9557_REG_POLARITY 0x02
#define PCA9557_REG_CONFIG   0x03

struct pca9557_config {
	struct gpio_driver_config common;
	struct i2c_dt_spec i2c;
};

struct pca9557_data {
	struct gpio_driver_data common;
	uint8_t output;
	uint8_t config; /* 1 = input, 0 = output */
};

static int pca9557_pin_configure(const struct device *dev, gpio_pin_t pin,
				  gpio_flags_t flags)
{
	const struct pca9557_config *cfg = dev->config;
	struct pca9557_data *data = dev->data;
	int ret;

	/* Pull-down not available; pull-up is always present on inputs */
	if (flags & GPIO_PULL_DOWN) {
		return -ENOTSUP;
	}

	if (flags & GPIO_INPUT) {
		data->config |= BIT(pin);
	} else {
		data->config &= ~BIT(pin);
		if (flags & GPIO_OUTPUT_INIT_HIGH) {
			data->output |= BIT(pin);
		} else if (flags & GPIO_OUTPUT_INIT_LOW) {
			data->output &= ~BIT(pin);
		}
	}

	ret = i2c_reg_write_byte_dt(&cfg->i2c, PCA9557_REG_CONFIG, data->config);
	if (ret < 0) {
		return ret;
	}

	if (!(flags & GPIO_INPUT)) {
		ret = i2c_reg_write_byte_dt(&cfg->i2c, PCA9557_REG_OUTPUT,
					    data->output);
	}

	return ret;
}

static int pca9557_port_get_raw(const struct device *dev,
				 gpio_port_value_t *value)
{
	const struct pca9557_config *cfg = dev->config;
	uint8_t buf;
	int ret;

	ret = i2c_reg_read_byte_dt(&cfg->i2c, PCA9557_REG_INPUT, &buf);
	if (ret < 0) {
		return ret;
	}
	*value = buf;
	return 0;
}

static int pca9557_port_set_masked_raw(const struct device *dev,
					gpio_port_pins_t mask,
					gpio_port_value_t value)
{
	const struct pca9557_config *cfg = dev->config;
	struct pca9557_data *data = dev->data;

	data->output = (data->output & ~mask) | (value & mask);
	return i2c_reg_write_byte_dt(&cfg->i2c, PCA9557_REG_OUTPUT, data->output);
}

static int pca9557_port_set_bits_raw(const struct device *dev,
				      gpio_port_pins_t pins)
{
	return pca9557_port_set_masked_raw(dev, pins, pins);
}

static int pca9557_port_clear_bits_raw(const struct device *dev,
					gpio_port_pins_t pins)
{
	return pca9557_port_set_masked_raw(dev, pins, 0U);
}

static int pca9557_port_toggle_bits(const struct device *dev,
				     gpio_port_pins_t pins)
{
	const struct pca9557_config *cfg = dev->config;
	struct pca9557_data *data = dev->data;

	data->output ^= pins;
	return i2c_reg_write_byte_dt(&cfg->i2c, PCA9557_REG_OUTPUT, data->output);
}

static int pca9557_pin_interrupt_configure(const struct device *dev,
					    gpio_pin_t pin,
					    enum gpio_int_mode mode,
					    enum gpio_int_trig trig)
{
	return -ENOTSUP;
}

static int pca9557_manage_callback(const struct device *dev,
				    struct gpio_callback *callback,
				    bool set)
{
	return -ENOTSUP;
}

static const struct gpio_driver_api pca9557_gpio_api = {
	.pin_configure           = pca9557_pin_configure,
	.port_get_raw            = pca9557_port_get_raw,
	.port_set_masked_raw     = pca9557_port_set_masked_raw,
	.port_set_bits_raw       = pca9557_port_set_bits_raw,
	.port_clear_bits_raw     = pca9557_port_clear_bits_raw,
	.port_toggle_bits        = pca9557_port_toggle_bits,
	.pin_interrupt_configure = pca9557_pin_interrupt_configure,
	.manage_callback         = pca9557_manage_callback,
};

static int pca9557_init(const struct device *dev)
{
	const struct pca9557_config *cfg = dev->config;
	struct pca9557_data *data = dev->data;

	if (!i2c_is_ready_dt(&cfg->i2c)) {
		return -ENODEV;
	}

	/* All pins as inputs by default (matches PCA9557 power-on reset state) */
	data->config = 0xFF;
	data->output = 0x00;

	return i2c_reg_write_byte_dt(&cfg->i2c, PCA9557_REG_CONFIG, data->config);
}

#define PCA9557_DEVICE(inst)						       \
	static const struct pca9557_config pca9557_config_##inst = {	       \
		.common = {						       \
			.port_pin_mask = (gpio_port_pins_t)0xFF,		       \
		},							       \
		.i2c = I2C_DT_SPEC_INST_GET(inst),			       \
	};								       \
	static struct pca9557_data pca9557_data_##inst;			       \
	DEVICE_DT_INST_DEFINE(inst,					       \
			      pca9557_init, NULL,			       \
			      &pca9557_data_##inst,			       \
			      &pca9557_config_##inst,			       \
			      POST_KERNEL,				       \
			      CONFIG_GPIO_PCA9557_INIT_PRIORITY,	       \
			      &pca9557_gpio_api);

DT_INST_FOREACH_STATUS_OKAY(PCA9557_DEVICE)
