/*
 * Copyright (c) 2023 Grinn
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_MFD_AD559X_H_
#define ZEPHYR_INCLUDE_DRIVERS_MFD_AD559X_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

#define AD559X_REG_SEQ_ADC        0x02U
#define AD559X_REG_GEN_CTRL       0x03U
#define AD559X_REG_ADC_CONFIG     0x04U
#define AD559X_REG_LDAC_EN        0x05U
#define AD559X_REG_GPIO_PULLDOWN  0x06U
#define AD559X_REG_READ_AND_LDAC  0x07U
#define AD559X_REG_GPIO_OUTPUT_EN 0x08U
#define AD559X_REG_GPIO_SET       0x09U
#define AD559X_REG_GPIO_INPUT_EN  0x0AU
#define AD559X_REG_PD_REF_CTRL    0x0BU
#define AD559X_REG_IO_TS_CONFIG   0x0DU

#define AD559X_DAC_RANGE BIT(4)
#define AD559X_ADC_RANGE BIT(5)
#define AD559X_EN_REF    BIT(9)

#define AD559X_PIN_MAX 8U

enum ad559x_pin_mode {
	AD559X_PIN_MODE_NONE,
	AD559X_PIN_MODE_ADC,
	AD559X_PIN_MODE_DAC,
	AD559X_PIN_MODE_GPIO,
};

/**
 * @defgroup mdf_interface_ad559x MFD AD559X interface
 * @ingroup mfd_interfaces
 * @{
 */

/**
 * @brief Check if the chip has a pointer byte map
 *
 * @param[in] dev Pointer to MFD device
 *
 * @retval true if chip has a pointer byte map, false if it has normal register map
 */
bool mfd_ad559x_has_pointer_byte_map(const struct device *dev);

/**
 * @brief Read raw data from the chip
 *
 * @param[in] dev Pointer to MFD device
 * @param[in] val Pointer to data buffer
 * @param[in] len Number of bytes to be read
 *
 * @retval 0 if success
 * @retval negative errno if failure
 */
int mfd_ad559x_read_raw(const struct device *dev, uint8_t *val, size_t len);

/**
 * @brief Write raw data to chip
 *
 * @param[in] dev Pointer to MFD device
 * @param[in] val Data to be written
 * @param[in] len Number of bytes to be written
 *
 * @retval 0 if success
 * @retval negative errno if failure
 */
int mfd_ad559x_write_raw(const struct device *dev, uint8_t *val, size_t len);

/**
 * @brief Read data from provided register
 *
 * @param[in] dev Pointer to MFD device
 * @param[in] reg Register to be read
 * @param[in] reg_data Additional data passed to selected register
 * @param[in] val Pointer to data buffer
 *
 * @retval 0 if success
 * @retval negative errno if failure
 */
int mfd_ad559x_read_reg(const struct device *dev, uint8_t reg, uint8_t reg_data, uint16_t *val);

/**
 * @brief Write data to provided register
 *
 * @param[in] dev Pointer to MFD device
 * @param[in] reg Register to be written
 * @param[in] val Data to be written
 *
 * @retval 0 if success
 * @retval negative errno if failure
 */
int mfd_ad559x_write_reg(const struct device *dev, uint8_t reg, uint16_t val);

/**
 * @brief Claim and configure an ADC channel.
 *
 * @param[in] dev Pointer to MFD device
 * @param[in] client_dev ADC child device claiming the channel
 * @param[in] channel Channel to configure
 *
 * @retval 0 if success
 * @retval -EBUSY if another child already owns the channel
 * @retval -EINVAL if the channel number is invalid
 * @retval negative errno if a register write fails
 */
int mfd_ad559x_adc_channel_setup(const struct device *dev, const struct device *client_dev,
				 uint8_t channel);

/**
 * @brief Claim and configure a DAC channel.
 *
 * @param[in] dev Pointer to MFD device
 * @param[in] client_dev DAC child device claiming the channel
 * @param[in] channel Channel to configure
 *
 * @retval 0 if success
 * @retval -EBUSY if another child already owns the channel
 * @retval -EINVAL if the channel number is invalid
 * @retval negative errno if a register write fails
 */
int mfd_ad559x_dac_channel_setup(const struct device *dev, const struct device *client_dev,
				 uint8_t channel);

/**
 * @brief Claim and configure a GPIO pin.
 *
 * @param[in] dev Pointer to MFD device
 * @param[in] client_dev GPIO child device claiming the pin
 * @param[in] pin Pin to configure
 * @param[in] flags GPIO configuration flags
 * @param[in] value Current GPIO output register value
 *
 * @retval 0 if success
 * @retval -EBUSY if another child already owns the pin
 * @retval -EINVAL if the pin number or flags are invalid
 * @retval -ENOTSUP if the flags request an unsupported mode
 * @retval negative errno if a register write fails
 */
int mfd_ad559x_gpio_pin_configure(const struct device *dev, const struct device *client_dev,
				  uint8_t pin, gpio_flags_t flags, uint8_t value);

/**
 * @brief Release a GPIO pin claim.
 *
 * @param[in] dev Pointer to MFD device
 * @param[in] client_dev GPIO child device releasing the pin
 * @param[in] pin Pin to release
 *
 * @retval 0 if success
 * @retval -EBUSY if another child owns the pin
 * @retval -EINVAL if the pin number is invalid
 * @retval negative errno if a register write fails
 */
int mfd_ad559x_gpio_pin_release(const struct device *dev, const struct device *client_dev,
				uint8_t pin);

/**
 * @brief Read ADC channel data from the chip
 *
 * @param[in] dev Pointer to MFD device
 * @param[in] channel Channel to read
 * @param[out] result ADC channel value read
 *
 * @retval 0 if success
 * @retval negative errno if failure
 */
int mfd_ad559x_read_adc_chan(const struct device *dev, uint8_t channel, uint16_t *result);

/**
 * @brief Write ADC channel data to the chip
 *
 * @param[in] dev Pointer to MFD device
 * @param[in] channel Channel to write to
 * @param[in] value DAC channel value
 *
 * @retval 0 if success
 * @retval negative errno if failure
 */
int mfd_ad559x_write_dac_chan(const struct device *dev, uint8_t channel, uint16_t value);

/**
 * @brief Read GPIO port from the chip
 *
 * @param[in] dev Pointer to MFD device
 * @param[out] value GPIO port value
 *
 * @retval 0 if success
 * @retval negative errno if failure
 */
int mfd_ad559x_gpio_port_get_raw(const struct device *dev, uint16_t *value);
/**
 * @}
 */

#ifdef __cplusplus
}
#endif

#endif /* ZEPHYR_INCLUDE_DRIVERS_MFD_AD559X_H_ */
