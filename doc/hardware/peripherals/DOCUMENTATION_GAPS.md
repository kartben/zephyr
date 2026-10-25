# Peripheral Documentation Gap Analysis

This document identifies peripheral driver classes that are missing or have incomplete
documentation in `doc/hardware/peripherals/`.

## Peripherals with Completely Missing Documentation

The following peripheral drivers have headers in `include/zephyr/drivers/` but have no
corresponding documentation in `doc/hardware/peripherals/`:

| Peripheral | Location |
|------------|----------|
| cellular | `include/zephyr/drivers/cellular.h` |
| disk | `include/zephyr/drivers/disk.h` |
| espi_saf (eSPI SAF) | `include/zephyr/drivers/espi_saf.h` |
| ethernet | `include/zephyr/drivers/ethernet/` |
| firmware | `include/zephyr/drivers/firmware/` |
| fpga | `include/zephyr/drivers/fpga.h` |
| ieee802154 | `include/zephyr/drivers/ieee802154/` |
| interrupt_controller | `include/zephyr/drivers/interrupt_controller/` |
| led_strip | `include/zephyr/drivers/led_strip.h` |
| memc (Memory Controller) | `include/zephyr/drivers/memc/` |
| mfd (Multi-Function Device) | `include/zephyr/drivers/mfd/` |
| mic_privacy | `include/zephyr/drivers/mic_privacy/` |
| mipi_dbi | `include/zephyr/drivers/mipi_dbi.h` |
| mipi_dsi | `include/zephyr/drivers/mipi_dsi.h` |
| modem | `include/zephyr/drivers/modem/` |
| pm_cpu_ops | `include/zephyr/drivers/pm_cpu_ops.h` |
| power | `include/zephyr/drivers/power/` |
| sip_svc (SIP Service) | `include/zephyr/drivers/sip_svc/` |
| swdp (Serial Wire Debug Port) | `include/zephyr/drivers/swdp.h` |
| syscon (System Controller) | `include/zephyr/drivers/syscon.h` |
| tee (Trusted Execution Environment) | `include/zephyr/drivers/tee.h` |

## Peripherals with Stub Documentation (API Reference Only)

These documentation files exist but contain only the minimal API reference (typically
around 13 lines) with no descriptive content:

| Peripheral | Doc File | Lines |
|------------|----------|-------|
| adc | `adc.rst` | 13 |
| counter | `counter.rst` | 13 |
| i2c_eeprom_target | `i2c_eeprom_target.rst` | 12 |
| ipm | `ipm.rst` | 13 |
| pcie | `pcie.rst` | 13 |
| pwm | `pwm.rst` | 13 |
| spi | `spi.rst` | 13 |
| watchdog | `watchdog.rst` | 13 |

## Peripherals with Incomplete Documentation

These documentation files exist but have only basic content (under 25 lines):

| Peripheral | Doc File | Lines |
|------------|----------|-------|
| bbram | `bbram.rst` | 21 |
| clock_control | `clock_control.rst` | 22 |
| crc | `crc.rst` | 23 |
| dac | `dac.rst` | 21 |
| entropy | `entropy.rst` | 17 |
| hwspinlock | `hwspinlock.rst` | 21 |
| mbox | `mbox.rst` | 18 |
| mdio | `mdio.rst` | 19 |
| psi5 | `psi5.rst` | 22 |
| sent | `sent.rst` | 22 |
| tgpio | `tgpio.rst` | 20 |

## Notes

Some drivers are documented in other sections of the documentation rather than in `doc/hardware/peripherals/`:

| Peripheral | Documentation Location |
|------------|------------------------|
| bluetooth | `doc/connectivity/bluetooth/` |
| cache | `doc/hardware/cache/` |
| dai (Digital Audio Interface) | `doc/hardware/peripherals/audio/dai.rst` |
| i2s (Inter-IC Sound) | `doc/hardware/peripherals/audio/i2s.rst` |
| lora | `doc/connectivity/lora_lorawan/` |
| pinctrl | `doc/hardware/pinctrl/` |
| usb | `doc/connectivity/usb/` |
| usb_c | `doc/connectivity/usb/` |
| virtualization | `doc/hardware/virtualization/` |
| virtio | `doc/hardware/virtualization/virtio.rst` |
| wifi | `doc/connectivity/networking/api/wifi.rst` |

## Reference: Well-Documented Peripherals

For comparison, these peripherals have comprehensive documentation (50+ lines):

- gpio.rst (153 lines) - Example of excellent documentation
- i3c.rst (362 lines)
- rtc.rst (205 lines)
- mspi.rst (202 lines)
- stepper.rst (107 lines)
- w1.rst (100 lines)
- can/ (640 lines total across subdirectory)
- sensor/ (403 lines total across subdirectory)
