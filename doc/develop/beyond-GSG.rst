.. _beyond-gsg:

Beyond the Getting Started Guide
################################

The :ref:`getting_started` provides a straightforward path to set up your
development environment and build your first application. This page covers
additional topics for maintaining your Zephyr installation and advanced
development workflows.

.. _python-pip:

Python and pip
**************

Python 3 and pip\ [#pip]_ are used extensively by Zephyr for build scripts,
development tools, and documentation generation.

When installing Python packages, you may need the ``--user`` flag depending on
your OS. See `Installing Packages`_ for details.

- **Linux**: Ensure ``~/.local/bin`` is in your :envvar:`PATH` when using
  ``--user``, or programs won't be found. This flag avoids conflicts with
  system package managers.

- **macOS**: `Homebrew disables -\\-user`_ by default.

- **Windows**: See `Installing Packages`_ for ``--user`` guidance.

Use pip's ``-U`` flag to update packages to their latest version.

.. _gs_toolchain:

Install a Toolchain
*******************

Zephyr binaries are compiled and linked by a *toolchain* comprised of
a cross-compiler and related tools which are different from the compiler
and tools used for developing software that runs natively on your host
operating system.

You can install the :ref:`Zephyr SDK <toolchain_zephyr_sdk>` to get toolchains for all
supported architectures, or install an :ref:`alternate toolchain <toolchains>`
recommended by the SoC vendor or a specific board (check your specific
:ref:`board-level documentation <boards>`).

You can configure the Zephyr build system to use a specific toolchain by
setting :ref:`environment variables <env_vars>` such as
:envvar:`ZEPHYR_TOOLCHAIN_VARIANT <{TOOLCHAIN}_TOOLCHAIN_PATH>` to a supported
value, along with additional variable(s) specific to the toolchain variant.

.. _gs_toolchain_update:

Updating the Zephyr SDK toolchain
*********************************

When updating Zephyr SDK, check whether the :envvar:`ZEPHYR_TOOLCHAIN_VARIANT`
or :envvar:`ZEPHYR_SDK_INSTALL_DIR` environment variables are already set.

* If the variables are not set, the latest compatible version of Zephyr SDK will be selected
  by default. Proceed to next step without making any changes.

* If :envvar:`ZEPHYR_TOOLCHAIN_VARIANT` is set, the corresponding toolchain will be selected
  at build time. Zephyr SDK is identified by the value ``zephyr``.
  If the :envvar:`ZEPHYR_TOOLCHAIN_VARIANT` environment variable is not ``zephyr``, then either
  unset it or change its value to ``zephyr`` to make sure Zephyr SDK is selected.

* If the :envvar:`ZEPHYR_SDK_INSTALL_DIR` environment variable is set, it will override
  the default lookup location for Zephyr SDK. If you install Zephyr SDK to one
  of the :ref:`recommended locations <toolchain_zephyr_sdk_bundle_variables>`,
  you can unset this variable. Otherwise, set it to your chosen install location.

For more information about these environment variables in Zephyr, see :ref:`env_vars_important`.

Keeping Zephyr updated
***********************

After updating Zephyr source code with ``git pull``, synchronize modules and
Python dependencies:

.. code-block:: console

   cd zephyrproject/zephyr
   git pull
   west update
   west packages pip --install

.. _gs-board-aliases:

Board Aliases
*************

Developers who work with multiple boards may find explicit board names
cumbersome and want to use aliases for common targets.  This is
supported by a CMake file with content like this:

.. code-block:: cmake

   # Variable foo_BOARD_ALIAS=bar replaces BOARD=foo with BOARD=bar and
   # sets BOARD_ALIAS=foo in the CMake cache.
   set(pca10028_BOARD_ALIAS nrf51dk/nrf51822)
   set(pca10056_BOARD_ALIAS nrf52840dk/nrf52840)
   set(k64f_BOARD_ALIAS frdm_k64f)
   set(sltb004a_BOARD_ALIAS efr32mg_sltb004a)

and specifying its location in :envvar:`ZEPHYR_BOARD_ALIASES`.  This
enables use of aliases ``pca10028`` in contexts like
``cmake -DBOARD=pca10028`` and ``west -b pca10028``.

.. rubric:: Footnotes

.. [#pip]

   pip is Python's package installer. Its ``install`` command first tries to
   reuse packages and package dependencies already installed on your computer.
   If that is not possible, ``pip install`` downloads them from the Python
   Package Index (PyPI) on the Internet.

   The package versions requested by Zephyr's :file:`requirements.txt` may
   conflict with other requirements on your system, in which case you may
   want to set up a virtualenv for Zephyr development.

.. _information on -\\-user:
 https://packaging.python.org/tutorials/installing-packages/#installing-to-the-user-site
.. _Homebrew disables -\\-user:
 https://docs.brew.sh/Homebrew-and-Python#note-on-pip-install---user
.. _Installing Packages:
 https://packaging.python.org/tutorials/installing-packages/
