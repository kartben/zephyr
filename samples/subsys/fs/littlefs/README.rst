.. zephyr:code-sample:: littlefs
   :name: LittleFS filesystem
   :relevant-api: file_system_api flash_area_api

   Use file system API over LittleFS.

Overview
********

This sample app demonstrates use of Zephyr's :ref:`file system API
<file_system_api>` over `littlefs`_, using file system with files that:

* count the number of times the system has booted
* holds binary pattern with properly incremented values in it

Other information about the file system is also displayed.

.. _littlefs:
   https://github.com/ARMmbed/littlefs

Requirements
************

Flash memory device
-------------------

The partition labeled "storage" will be used for the file system; see
:ref:`flash_map_api`.  If that area does not already have a
compatible littlefs file system its contents will be replaced by an
empty file system.  You will see diagnostics like this::

   [00:00:00.010,192] <inf> littlefs: LittleFS version 2.0, disk version 2.0
   [00:00:00.010,559] <err> littlefs: Corrupted dir pair at 0 1
   [00:00:00.010,559] <wrn> littlefs: can't mount (LFS -84); formatting

The error and warning are normal for a new file system.

After the file system is mounted you'll also see::

   [00:00:00.182,434] <inf> littlefs: filesystem mounted!
   [00:00:00.867,034] <err> fs: failed get file or dir stat (-2)

This error is also normal for Zephyr not finding a file (the boot count,
in this case).

Block device (e.g. SD card)
---------------------------

One needs to prepare the SD/MMC card with littlefs file system on
the host machine with the `lfs`_ program.

.. _lfs:
   https://www.thevtool.com/mounting-littlefs-on-linux-machine/

.. code-block:: console

   sudo chmod a+rw /dev/sda
   lfs -d -s -f --read_size=512 --prog_size=512 --block_size=512 --cache_size=512 --lookahead_size=8192 --format /dev/sda
   lfs -d -s -f --read_size=512 --prog_size=512 --block_size=512 --cache_size=512 --lookahead_size=8192 /dev/sda ./mnt_littlefs
   cd ./mnt_littlefs
   echo -en '\x01' > foo.txt
   cd -
   fusermount -u ./mnt_littlefs


Building and Running
********************

Flash memory device
-------------------

This example should work on any board that provides a "storage"
partition.  Two tested board targets are described below.

The sample is known to build for the following board targets:

* ``disco_l475_iot1``, ``stm32f429i_disc1``, ``stm32f746g_disco``,
  ``stm32h747i_disco/stm32h747xx/m7``, ``stm32h750b_dk``
* ``frdm_ke17z``, ``frdm_ke17z512``, ``lpcxpresso55s69/lpc55s69/cpu0``,
  ``mimxrt685_evk/mimxrt685s/cm33``, ``mimxrt685_aud_evk/mimxrt685s/cm33``,
  ``mimxrt1060_evk/mimxrt1062/qspi``, ``mimxrt1064_evk``, ``mimxrt1160_evk/mimxrt1166/cm7``
* ``mr_canhubk3``, ``s32k5xxcvb/s32k566/m7``, ``s32k5xxcvb/s32k566/r52``,
  ``s32z2xxdc2/s32z270/rtu0``, ``s32z2xxdc2/s32z270/rtu1``,
  ``s32z2xxdc2@D/s32z270/rtu0``, ``s32z2xxdc2@D/s32z270/rtu1``
* ``nrf52840dk/nrf52840``, ``nrf54l15dk/nrf54l15/cpuapp``, ``particle_xenon``
* ``native_sim``, ``qemu_x86``

The following board targets have additional build variants that place the file system on an
external or alternative flash device, selected with the matching ``EXTRA_CONF_FILE`` (and
``DTC_OVERLAY_FILE``) from the :file:`boards` directory:

* ``nrf52840dk/nrf52840``: external SPI NOR flash (``nrf52840dk_nrf52840_spi``) or QSPI NOR
  flash (``nrf52840dk_nrf52840_qspi``)
* ``nucleo_h743zi``: QSPI NOR flash (``nucleo_h743zi_qspi``)
* ``stm32l562e_dk``: OSPI NOR flash (``stm32l562e_dk_ospi``)
* ``frdm_rw612``: FlexSPI NAND flash through a flash translation layer (``frdm_rw612_nand``)

You can set ``CONFIG_APP_WIPE_STORAGE`` to force the file system to be
recreated.

Block device (e.g. SD card)
---------------------------

This example has been devised and initially tested on :zephyr:board:`nucleo_h743zi`
board. It can be also run on any other board with SD/MMC card connected to it.
It is also known to build for ``rcar_h3ulcb/r8a77951/a57``, ``rcar_salvator_xs``,
``stm32f746g_disco`` and ``stm32h747i_disco/stm32h747xx/m7``.

To build the test:

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/fs/littlefs
   :board: nucleo_h743zi
   :goals: build flash
   :gen-args: -DCONF_FILE=prj_blk.conf
   :compact:

At the moment, only two types of block devices are acceptable in this sample: SDMMC and MMC.

It is possible that both the ``zephyr,sdmmc-disk`` and ``zephyr,mmc-disk`` block devices will be
present and enabled in the final board dts and configuration files simultaneously, the mount
point name for the ``littlefs`` file system block device will be determined based on the
following logic:

* if the :kconfig:option:`CONFIG_DISK_DRIVER_SDMMC` configuration is defined, ``"SD"``
  will be used as the mount point name;
* if the :kconfig:option:`CONFIG_DISK_DRIVER_SDMMC` configuration is not defined, but the
  :kconfig:option:`CONFIG_DISK_DRIVER_MMC` configuration is defined, ``"SD2"`` will
  be used as the mount point name;
* if neither :kconfig:option:`CONFIG_DISK_DRIVER_SDMMC` nor :kconfig:option:`CONFIG_DISK_DRIVER_MMC`
  configurations are defined, the mount point name will not be determined, and an appropriate error
  will appear during the sample build.

NRF52840 Development Kit
========================

On this device the file system will be placed in the SOC flash.

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/fs/littlefs
   :board: nrf52840dk/nrf52840
   :goals: build
   :compact:

Particle Xenon
==============

On this device the file system will be placed on the external SPI NOR
flash memory.

.. zephyr-app-commands::
   :zephyr-app: samples/subsys/fs/littlefs
   :board: particle_xenon
   :goals: build
   :compact:
