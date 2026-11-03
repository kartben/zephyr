.. _docker_dev_env:

Docker Development Environment
##############################

The Zephyr Project publishes Docker images that contain everything needed to
build, run, and test Zephyr applications: the host dependencies installed in the
:ref:`getting_started`, the :ref:`Zephyr SDK <toolchain_zephyr_sdk>`, Zephyr's
Python dependencies, and the emulators and simulators used by the project's
:ref:`continuous integration <Continuous Integration>` (CI). Developing inside a
container created from one of these images is an alternative to installing all
of these tools natively on your machine.

This page describes how to set up such an environment and use it to perform the
typical operations covered elsewhere in this documentation: fetching the source
code, building applications, running them in emulation, running tests with
:ref:`Twister <twister_script>`, building the documentation, and flashing
hardware.

.. note::

   The commands on this page use the ``docker`` command line tool and a POSIX
   shell. They also work with `Podman`_, with the differences described in
   :ref:`docker_podman`. On Windows, run them from a WSL 2 terminal, or adapt
   them to PowerShell.

When to use a container
***********************

A container-based environment is a good fit when you:

* want a working environment quickly, without installing and maintaining the
  toolchains and host dependencies on your machine;
* need to reproduce a CI failure locally, in the exact environment used by the
  Zephyr Project's CI;
* work on a host or Linux distribution that is not covered by the
  :ref:`getting_started`, or on which the required tool versions are not
  available;
* need several Zephyr versions with different tool requirements side by side,
  or want to share a reproducible environment with a team.

It also comes with trade-offs to be aware of:

* the images are Linux-based (Ubuntu 24.04) and several gigabytes in size;
* flashing and debugging hardware from inside the container requires passing
  USB devices through to it, which is only possible on Linux hosts (see
  :ref:`docker_flash`);
* on macOS and Windows, containers run inside a virtual machine, and builds
  performed on a workspace shared with the host are slower than native builds.

Available images
****************

The images are built from the `zephyrproject-rtos/docker-image`_ repository and
published on the GitHub Container Registry:

.. list-table::
   :header-rows: 1
   :widths: 35 65

   * - Image
     - Contents

   * - ``ghcr.io/zephyrproject-rtos/ci-base``
     - All host tools and dependencies needed to build and test Zephyr, but no
       toolchain. Use it as a base image when you need to provide your own
       :ref:`toolchain <toolchains>`.

   * - ``ghcr.io/zephyrproject-rtos/ci``
     - ``ci-base`` plus the :ref:`Zephyr SDK <toolchain_zephyr_sdk>` and
       additional toolchains. This is the image the Zephyr Project's CI runs
       in. It runs as ``root`` and has no default working directory.

   * - ``ghcr.io/zephyrproject-rtos/zephyr-build``
     - ``ci`` plus conveniences for interactive use: it runs as an unprivileged
       ``user`` account with password-less ``sudo``, uses :file:`/workdir` as
       its working directory, and starts a VNC server so that graphical
       applications can be displayed on the host (see :ref:`docker_vnc`). This
       is the image to use for day-to-day development, and the one used in the
       examples on this page.

Among other things, the ``ci`` and ``zephyr-build`` images include:

* the Zephyr SDK, installed under :file:`/opt/toolchains` and registered in the
  CMake package registry, with :envvar:`ZEPHYR_TOOLCHAIN_VARIANT` set to
  ``zephyr``, so that builds pick it up without any further configuration;
* the LLVM toolchain, as well as toolchains for architectures not covered by the
  Zephyr SDK, such as TriCore and Hexagon;
* a Python virtual environment in :file:`/opt/python/venv`, already on the
  :envvar:`PATH`, with ``west`` and the Python packages required by Zephyr,
  MCUboot and TF-M installed;
* the QEMU and OpenOCD builds shipped with the Zephyr SDK, as well as Renode,
  :ref:`BabbleSim <bsim>` (in :file:`/opt/bsim`) and Arm Fixed Virtual
  Platforms (FVPs), so that most emulated and simulated :ref:`boards <boards>`
  can be run;
* Doxygen and Graphviz, needed to :ref:`build this documentation <zephyr_doc>`.

Image tags
==========

Each image is published with the following tags:

``main``
   Built from the ``main`` branch of the ``docker-image`` repository and kept in
   sync with the tool versions required by Zephyr's ``main`` branch. Use it when
   working on the latest Zephyr code.

``vX.Y.Z`` (for example ``v0.29.3``)
   Versioned releases. Zephyr's CI workflows pin a specific release, so using
   the same tag gives you the environment that validated a given Zephyr
   revision. The version in use can be found in the ``container:`` entries of
   the workflows in :zephyr_file:`.github/workflows`, for example in
   :zephyr_file:`.github/workflows/twister.yaml`.

``vX.Y-branch`` (for example ``v0.26-branch``)
   The latest release of a maintenance branch. Use it when working on a Zephyr
   release branch, as the tool versions in newer images may not be compatible
   with older Zephyr code. For instance, ``v0.26-branch`` is the image series
   used with Zephyr 3.7 LTS.

.. note::

   CI workflows reference ``ghcr.io/zephyrproject-rtos/ci-repo-cache``, a
   variant of the ``ci`` image that additionally contains a cached clone of the
   Zephyr workspace to speed up CI jobs. Its tag is the ``ci`` version followed
   by a build date. The same tools are available in the corresponding ``ci`` and
   ``zephyr-build`` images.

.. note::

   The images are also mirrored on Docker Hub, for example as
   ``docker.io/zephyrprojectrtos/zephyr-build``.

Install Docker
**************

.. tabs::

   .. group-tab:: Linux

      Install `Docker Engine`_ following the instructions for your
      distribution. To run containers without ``sudo``, add your user to the
      ``docker`` group, then log out and back in:

      .. code-block:: bash

         sudo usermod -aG docker $USER

      Alternatively, install `Podman`_, which runs containers without a
      privileged daemon. See :ref:`docker_podman` for the differences.

   .. group-tab:: macOS

      Install `Docker Desktop for Mac`_ (or another container runtime such as
      `Podman`_). Containers run in a lightweight Linux virtual machine whose
      resources (CPUs, memory, disk) can be adjusted in the Docker Desktop
      settings; give it several CPU cores to keep build times reasonable.

   .. group-tab:: Windows

      Install `Docker Desktop for Windows`_ with its default WSL 2 backend,
      which requires the `Windows Subsystem for Linux (WSL)`_ to be installed.
      Run the commands on this page from a terminal in a WSL 2 distribution
      such as Ubuntu.

      .. note::

         For best performance, keep the Zephyr workspace on the WSL 2 file
         system (for example in your home directory in the Ubuntu
         distribution) rather than on a Windows drive mounted under
         :file:`/mnt/c`. Builds on a workspace stored on a Windows drive are
         significantly slower.

Get the image
*************

Pull the developer image. This downloads several gigabytes, so it may take a
while:

.. code-block:: shell

   docker pull ghcr.io/zephyrproject-rtos/zephyr-build:main

Run the same command again later to update to the latest ``main`` image.

.. _docker_workspace:

Set up the workspace
********************

Zephyr's source code and its modules are managed in a :ref:`west workspace
<west-workspaces>`. Keep the workspace on your host machine and *bind mount* it
into the container: your files persist when the container is removed, and you
can keep using your favorite editor and tools on the host.

The ``zephyr-build`` image expects the workspace to be mounted at
:file:`/workdir`.

#. On the host, create an empty directory for the workspace. You can also reuse
   an existing workspace created by following the :ref:`getting_started`; in
   that case, skip the ``west init`` and ``west update`` step below.

   .. code-block:: shell

      mkdir -p ~/zephyrproject

#. Start a container with the workspace mounted:

   .. code-block:: shell

      docker run -it --rm \
        -v ~/zephyrproject:/workdir \
        ghcr.io/zephyrproject-rtos/zephyr-build:main

   Where:

   * ``-it`` attaches your terminal to an interactive shell in the container;
   * ``--rm`` removes the container when you exit the shell. Nothing is lost
     since the workspace lives on the host, but any change made outside of
     :file:`/workdir` (such as packages installed with ``apt``) is discarded.
     See :ref:`docker_persistent` for keeping a container around;
   * ``-v ~/zephyrproject:/workdir`` mounts the workspace at :file:`/workdir` in
     the container.

   Once the container has started, you get a shell prompt as the ``user``
   account, in :file:`/workdir`. All the commands in the following sections are
   run at that prompt.

#. Initialize the workspace and fetch Zephyr and its :ref:`modules <modules>`:

   .. only:: not release

      .. code-block:: shell

         west init -m https://github.com/zephyrproject-rtos/zephyr /workdir
         west update

   .. only:: release

      .. parsed-literal::

         west init -m https://github.com/zephyrproject-rtos/zephyr /workdir --mr v |zephyr-version-ltrim|
         west update

   ``west`` and Zephyr's Python dependencies are preinstalled in the image, so
   the virtual environment, ``pip install west`` and ``west packages`` steps of
   the :ref:`getting_started` are not needed. See :ref:`docker_python_deps` if
   you check out a Zephyr revision with different requirements.

The Zephyr SDK is already installed and registered in the image, so there is
nothing else to set up: you can move on to building applications.

.. _docker_build_run:

Build and run an application
****************************

Everything described from :ref:`getting_started_run_sample` onwards in the
Getting Started Guide applies unchanged inside the container.

Build the :zephyr:code-sample:`hello_world` sample for the ``qemu_x86`` emulated
board and run it in QEMU (press :kbd:`Ctrl-a`, then :kbd:`x` to exit QEMU):

.. code-block:: shell

   cd /workdir/zephyr
   west build -p always -b qemu_x86 samples/hello_world
   west build -t run

Or build it as a native Linux executable for :zephyr:board:`native_sim` and run
it directly (press :kbd:`Ctrl-C` to exit):

.. code-block:: shell

   west build -p always -b native_sim samples/hello_world
   ./build/zephyr/zephyr.exe

Building for real hardware works the same way. Replace ``<your-board-name>``
with the name of your board:

.. code-block:: shell

   west build -p always -b <your-board-name> samples/basic/blinky

Since the workspace is shared with the host, the build output is directly
available there too, for example in :file:`~/zephyrproject/zephyr/build/zephyr`.
See :ref:`docker_flash` for how to program it onto your board.

Run tests with Twister
**********************

:ref:`Twister <twister_script>` works out of the box for emulated and simulated
platforms, since the image contains the same emulators and simulators as CI. For
example, to build and run the :zephyr:code-sample:`hello_world` sample on a few
of them:

.. code-block:: shell

   cd /workdir/zephyr
   west twister -p native_sim -p qemu_x86 -p qemu_cortex_m3 -T samples/hello_world

To run the tests of a directory on the same set of platforms CI uses, add the
``--integration`` option:

.. code-block:: shell

   west twister --integration -T tests/kernel/threads

Twister writes its :ref:`reports <twister_output>` to :file:`twister-out` in
the current directory, so they are also available on the host.

.. _docker_docs:

Build the documentation
***********************

The image contains the tools required to :ref:`build this documentation
<zephyr_doc>` in HTML format, except for the documentation-specific Python
packages. The Python virtual environment of the image is owned by ``root``, so
install them with ``sudo`` before building:

.. code-block:: shell

   cd /workdir/zephyr
   sudo /opt/python/venv/bin/pip install -r doc/requirements.txt
   cd doc
   make html-fast

The generated documentation can then be opened on the host from
:file:`~/zephyrproject/zephyr/doc/_build/html/index.html`.

.. _docker_ci:

Run CI checks locally
*********************

Because the container provides the same environment as CI, it is a convenient
way to :ref:`run CI checks locally <CI Tests>` before opening a pull request.
For example, to run the compliance checks on the commits of your branch:

.. code-block:: shell

   cd /workdir/zephyr
   ./scripts/ci/check_compliance.py -c origin/main..HEAD

Similarly, ``west twister --integration`` as shown above builds and runs tests
on the platforms CI would use for them.

.. _docker_flash:

Flash and debug hardware
************************

A container has no access to the USB devices of the host by default. There are
two ways of programming a board with an application built in the container.

Flash from the host
===================

Since the workspace is shared with the host, the build output (for example
:file:`build/zephyr/zephyr.hex`, :file:`zephyr.bin`, or :file:`zephyr.elf`) is
available there. Use the programming tool of your board on the host to flash it;
for boards that expose a USB mass storage bootloader, it is usually enough to
drag and drop the binary onto the corresponding drive. Check your board's page
in :ref:`boards` for the tools and file format it expects.

This works on every operating system, and is the only option on macOS, or on
Windows when using Docker Desktop.

.. note::

   If you also have a native Zephyr installation on the host, you can run
   :ref:`west flash <west-flashing>` there against the build directory produced
   in the container. Note that the build directory records the location of the
   tools used inside the container, so runners relying on tools from the Zephyr
   SDK (such as OpenOCD) may require passing the host path of the tool
   explicitly with the runner's own option (for example ``--openocd``). Run
   ``west flash -H`` for a list of runner options.

Flash from the container (Linux)
================================

On Linux, USB devices can be made available to the container so that
:ref:`west flash <west-flashing>` and :ref:`west debug <west-debugging>` work as
they do natively.

#. On the host, install the udev rules granting non-root users access to your
   debug probe, as described in :ref:`setting-udev-rules`. Device permissions
   are managed by the host, not by the container.

#. Start the container with access to the host's USB devices:

   .. code-block:: shell

      docker run -it --rm \
        -v ~/zephyrproject:/workdir \
        --privileged -v /dev/bus/usb:/dev/bus/usb \
        ghcr.io/zephyrproject-rtos/zephyr-build:main

   ``--privileged`` gives the container access to all host devices, and
   mounting :file:`/dev/bus/usb` makes devices plugged in after the container
   has started visible as well. If you prefer not to run a privileged
   container, individual devices can be passed instead with ``--device``, for
   example ``--device /dev/ttyACM0`` for a serial port; such devices must be
   connected before the container is started.

#. Inside the container, flash and debug as usual:

   .. code-block:: shell

      cd /workdir/zephyr
      west build -p always -b <your-board-name> samples/basic/blinky
      west flash
      west debug

Runners that rely on tools included in the image, such as OpenOCD from the
Zephyr SDK or pyOCD, work directly. Runners that require vendor-specific tools
(for example J-Link, nrfutil, or STM32CubeProgrammer) need those tools to be
installed in the container first; see :ref:`docker_custom_image`. Refer to
:ref:`flash-debug-host-tools` for the tools each runner needs.

.. tip::

   To connect a debugger running on the host (for example from an IDE) to a
   debug server started in the container with ``west debugserver``, publish the
   server's port with ``-p``, such as ``-p 3333:3333`` for OpenOCD.

.. note::

   On Windows, USB devices can be attached to a WSL 2 distribution with
   `usbipd-win`_. A Docker Engine installed directly inside that distribution
   can then access them as described above; the containers managed by Docker
   Desktop cannot.

.. _docker_vnc:

Display graphical applications
******************************

Samples that use a display, such as the :zephyr:code-sample:`lvgl` sample built
for :zephyr:board:`native_sim`, open a window using SDL when they run. The
``zephyr-build`` image starts a virtual X server and a VNC server on port 5900
when the container starts. Publish that port to view such applications from the
host:

.. code-block:: shell

   docker run -it --rm \
     -v ~/zephyrproject:/workdir \
     -p 5900:5900 \
     ghcr.io/zephyrproject-rtos/zephyr-build:main

Then build and run the sample inside the container:

.. code-block:: shell

   cd /workdir/zephyr
   west build -p always -b native_sim/native/64 samples/subsys/display/lvgl
   west build -t run

Connect a VNC client to ``localhost:5900`` (on macOS, the built-in Screen
Sharing application can be used by opening ``vnc://localhost:5900``). The
password is ``zephyr``.

.. _docker_devcontainer:

Use the image with Visual Studio Code
*************************************

The `Dev Containers`_ extension for Visual Studio Code lets the editor run its
terminal, IntelliSense and debugging features inside a container while the user
interface stays on the host. To use the Zephyr image this way, create a
:file:`.devcontainer/devcontainer.json` file at the top level of your workspace
(:file:`~/zephyrproject`, next to the :file:`.west` directory):

.. code-block:: json

   {
     "name": "Zephyr",
     "image": "ghcr.io/zephyrproject-rtos/zephyr-build:main",
     "workspaceFolder": "/workdir",
     "workspaceMount": "source=${localWorkspaceFolder},target=/workdir,type=bind",
     "remoteUser": "user",
     "forwardPorts": [5900],
     "customizations": {
       "vscode": {
         "extensions": ["ms-vscode.cpptools-extension-pack"]
       }
     }
   }

Then open the :file:`~/zephyrproject` folder in VS Code and run the
:guilabel:`Dev Containers: Reopen in Container` command from the command palette
(:kbd:`Ctrl+Shift+P`). Once the container has started, the integrated terminal
runs inside it, and you can follow the :ref:`vscode_ide` guide from the
:file:`compile_commands.json` generation step onwards to set up code navigation.

To flash and debug hardware from within the container on Linux, add the
options described in :ref:`docker_flash` to the configuration:

.. code-block:: json

   {
     "runArgs": ["--privileged", "-v", "/dev/bus/usb:/dev/bus/usb"]
   }

.. _docker_podman:

Use Podman instead of Docker
****************************

`Podman`_ is a daemonless container engine compatible with the Docker command
line. All the commands on this page work by replacing ``docker`` with
``podman``, with two things to keep in mind when running rootless containers:

* Podman maps the ``user`` account of the image (UID 1000) to a subordinate UID
  of your host account, so files created in the container appear as owned by
  another user on the host. Pass ``--userns=keep-id:uid=1000,gid=1000`` to map
  your host account to UID 1000 inside the container instead.
* On hosts with SELinux enabled (such as Fedora), append ``:Z`` to the volume
  option so that the workspace gets a label the container is allowed to access.

For example:

.. code-block:: shell

   podman run -it --rm \
     --userns=keep-id:uid=1000,gid=1000 \
     -v ~/zephyrproject:/workdir:Z \
     ghcr.io/zephyrproject-rtos/zephyr-build:main

Tips and troubleshooting
************************

.. _docker_persistent:

Keeping a container around
==========================

The ``--rm`` option used on this page discards the container when you exit it.
To keep changes made outside of the workspace (packages installed with ``apt``,
a Git configuration, the CMake package registry populated by
``west zephyr-export``, shell history, and so on), give the container a name
and omit ``--rm``:

.. code-block:: shell

   docker run -it --name zephyr \
     -v ~/zephyrproject:/workdir \
     ghcr.io/zephyrproject-rtos/zephyr-build:main

The container stops when you exit the shell. Start it again and reattach with:

.. code-block:: shell

   docker start -ai zephyr

To open an additional shell in a running container, for example to monitor a
serial console while a build is running, use:

.. code-block:: shell

   docker exec -it zephyr bash

.. _docker_python_deps:

Updating the Python dependencies
================================

The Python packages in the image match the requirements of Zephyr's ``main``
branch at the time the image was built. When working on a revision with
different requirements, for example if ``west update`` or ``west build`` report
a missing Python module, install the packages required by your checkout in the
image's virtual environment. Since it is owned by ``root``, ``sudo`` is needed:

.. code-block:: shell

   cd /workdir/zephyr
   sudo env "PATH=$PATH" west packages pip --install

The packages are installed in the container, not in the workspace; with a
container started with ``--rm``, this has to be repeated each time a new
container is created. Consider :ref:`keeping the container <docker_persistent>`
or :ref:`building a custom image <docker_custom_image>` instead.

.. _docker_file_ownership:

File ownership and Git errors
=============================

The ``user`` account in the image has UID and GID 1000, which matches the first
user account on most Linux distributions. If your host account has a different
UID, files created in the container (build directories, ``west update``
checkouts) show up as owned by another user on the host, and Git may refuse to
operate on the workspace in the container with an error such as:

.. code-block:: none

   fatal: detected dubious ownership in repository at '/workdir/zephyr'

The recommended fix is to run the container with your host UID and GID. Since
that account does not exist in the image, also provide a writable home
directory and point the build system to the Zephyr SDK explicitly, as the CMake
package registry of the ``user`` account is not used in this case:

.. code-block:: shell

   docker run -it --rm \
     --user "$(id -u):$(id -g)" \
     -e HOME=/tmp \
     -v ~/zephyrproject:/workdir \
     ghcr.io/zephyrproject-rtos/zephyr-build:main

Then, in the container:

.. code-block:: shell

   export ZEPHYR_SDK_INSTALL_DIR=/opt/toolchains/zephyr-sdk-${ZSDK_VERSION}

Note that ``sudo`` is not available when running as an account other than
``user``. Alternatively, :ref:`build the images yourself <docker_custom_image>`
with your own UID and GID, or, as Zephyr's CI does, tell Git to trust the
workspace despite the ownership mismatch:

.. code-block:: shell

   git config --global --add safe.directory '*'

Working behind a proxy
======================

Pass your proxy settings to the container with ``-e``, so that ``west``,
``git`` and ``pip`` can reach the Internet:

.. code-block:: shell

   docker run -it --rm \
     -e http_proxy -e https_proxy -e no_proxy \
     -v ~/zephyrproject:/workdir \
     ghcr.io/zephyrproject-rtos/zephyr-build:main

When a variable is passed to ``-e`` without a value, its value is taken from
the host environment.

.. _docker_custom_image:

Customizing the image
=====================

To add tools that are not part of the image, such as vendor-specific flashing
tools, create a :file:`Dockerfile` that extends it, for example:

.. code-block:: dockerfile

   FROM ghcr.io/zephyrproject-rtos/zephyr-build:main

   USER root
   RUN apt-get update && \
       apt-get install -y --no-install-recommends <package> && \
       rm -rf /var/lib/apt/lists/*
   USER user

Then build it and use it in place of the upstream image:

.. code-block:: shell

   docker build -t zephyr-build-custom .
   docker run -it --rm -v ~/zephyrproject:/workdir zephyr-build-custom

The images can also be rebuilt entirely from the
`zephyrproject-rtos/docker-image`_ repository, for example to use a UID and GID
matching your host account (see :ref:`docker_file_ownership`):

.. code-block:: shell

   git clone https://github.com/zephyrproject-rtos/docker-image
   cd docker-image
   docker build -f Dockerfile.base \
     --build-arg UID=$(id -u) --build-arg GID=$(id -g) \
     -t zephyr-ci-base:local .
   docker build -f Dockerfile.ci --build-arg BASE_IMAGE=zephyr-ci-base:local \
     -t zephyr-ci:local .
   docker build -f Dockerfile.devel --build-arg BASE_IMAGE=zephyr-ci:local \
     -t zephyr-build:local .

Refer to the README of that repository for the available build arguments.

.. _zephyrproject-rtos/docker-image: https://github.com/zephyrproject-rtos/docker-image
.. _Docker Engine: https://docs.docker.com/engine/install/
.. _Docker Desktop for Mac: https://docs.docker.com/desktop/setup/install/mac-install/
.. _Docker Desktop for Windows: https://docs.docker.com/desktop/setup/install/windows-install/
.. _Windows Subsystem for Linux (WSL): https://learn.microsoft.com/windows/wsl/install
.. _Podman: https://podman.io/
.. _Dev Containers: https://code.visualstudio.com/docs/devcontainers/containers
.. _usbipd-win: https://github.com/dorssel/usbipd-win
