.. _docker_development:

Developing with Docker
######################

The Zephyr Project publishes Docker images that bundle the Zephyr SDK and all the host
dependencies needed to build and test Zephyr applications. Containers created from these images
provide a ready-to-use, disposable development environment that leaves the rest of your system
untouched, and that closely matches the environment Zephyr's own continuous integration runs in.

Working in a container is convenient when you want to:

* try out Zephyr without installing anything on your host besides Docker
* reproduce a CI failure in the same environment it originally occurred in
* share a pinned, known-good set of tools across a team
* develop on a Linux distribution or OS version not covered by the :ref:`getting_started`

The images are Linux containers. They run natively on a Linux host, while Docker Desktop runs
them in a lightweight virtual machine on macOS and Windows. Building applications and running
them in emulation works the same on all three operating systems; accessing USB devices such as
debug probes and serial consoles does not, and is covered in `Working with hardware`_.

Zephyr Docker images
********************

The images are maintained in the `docker-image`_ repository and published to the GitHub
Container Registry:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Image
     - Contents

   * - ``ghcr.io/zephyrproject-rtos/ci-base``
     - The host dependencies required to build and test Zephyr applications, without the Zephyr
       SDK. Use it as a base for custom images that provide their own toolchains.

   * - ``ghcr.io/zephyrproject-rtos/ci``
     - Everything in ``ci-base``, plus the Zephyr SDK with toolchains for all supported
       architectures. Zephyr's continuous integration runs in this environment.

   * - ``ghcr.io/zephyrproject-rtos/zephyr-build``
     - Everything in ``ci``, plus additional tools that are useful for interactive development:
       a non-root user account, a VNC server for viewing display samples, ``vim``,
       ``usbutils``, and more. This "developer image" is the one used in the examples on this
       page.

In addition to the toolchains, the images contain the simulation and emulation tools Zephyr's
CI relies on, such as QEMU (from the Zephyr SDK), Renode, BabbleSim, and several Arm FVPs,
along with a Python environment in which west and the Python dependencies of the main Zephyr
repository are preinstalled.

Images are published for both ``linux/amd64`` and ``linux/arm64``. Version tags such as ``v0.29.3``
correspond to `docker-image releases`_, each of which ships a specific version of the Zephyr
SDK; the ``main`` tag follows the latest state of the ``main`` branch. When working on an older
Zephyr release, pick an image whose SDK version is compatible with it. In particular, use the
``v0.26-branch`` tag when working with Zephyr 3.7 LTS.

.. note::

   Since they target every architecture and simulation platform supported by Zephyr, the
   official images are quite large (several gigabytes to download). The `Embedded Containers`_
   project maintains slimmer, single-architecture Zephyr images that can be a better fit for
   downstream CI pipelines.

Install Docker
**************

.. tabs::

   .. group-tab:: Linux

      Install `Docker Engine`_ using the instructions for your distribution, and follow the
      `post-installation steps`_ to add yourself to the ``docker`` group so that you can use
      Docker as a regular user.

   .. group-tab:: macOS

      Install `Docker Desktop for Mac`_. On Apple silicon Macs, the ``arm64`` variant of the
      Zephyr images is used automatically.

   .. group-tab:: Windows

      Install `Docker Desktop for Windows`_ with the WSL 2 backend (the default on current
      versions of Windows).

Set up a workspace
******************

Keep your :term:`west workspace` on the host and mount it into the container: the source code
and everything you build survive the container itself, and you can keep working on the code
with your favorite editor or IDE on the host.

Create an empty directory for the workspace, then start a container with the directory mounted
at :file:`/workdir`:

.. tabs::

   .. group-tab:: Linux

      .. code-block:: shell

         mkdir -p ~/zephyrproject
         docker run -it --rm -v ~/zephyrproject:/workdir \
             ghcr.io/zephyrproject-rtos/zephyr-build:main

   .. group-tab:: macOS

      .. code-block:: shell

         mkdir -p ~/zephyrproject
         docker run -it --rm -v ~/zephyrproject:/workdir \
             ghcr.io/zephyrproject-rtos/zephyr-build:main

   .. group-tab:: Windows

      .. tabs::

         .. code-tab:: bat

            mkdir %USERPROFILE%\zephyrproject
            docker run -it --rm -v %USERPROFILE%\zephyrproject:/workdir ^
                ghcr.io/zephyrproject-rtos/zephyr-build:main

         .. code-tab:: powershell

            mkdir $env:USERPROFILE\zephyrproject
            docker run -it --rm -v $env:USERPROFILE\zephyrproject:/workdir `
                ghcr.io/zephyrproject-rtos/zephyr-build:main

      .. note::

         Bind mounts of Windows directories can be slow. If build performance becomes an
         issue, keep the workspace in a Docker volume or in the WSL 2 file system instead.

The image is downloaded automatically the first time you use it. You end up in a shell in
:file:`/workdir`, logged in as ``user``, with west and all the tools needed to build Zephyr
already available.

Initialize the workspace and fetch Zephyr and its :ref:`modules <modules>`, as in the
:ref:`getting_started`:

.. code-block:: shell

   west init .
   west update

If you already have a west workspace on the host, mount it instead and skip this step. The
Python dependencies baked into the image match the Zephyr version that was current when the
image was released; when working on a newer revision, bring them up to date with
``west packages pip --install``.

.. tip::

   ``--rm`` deletes the container when you exit its shell. The workspace is safe since it lives
   on the host, but anything installed or configured elsewhere in the container is lost. To
   keep a container around and return to it later, drop ``--rm``, give the container a name,
   and restart it as needed:

   .. code-block:: shell

      docker run -it --name zephyr-dev -v ~/zephyrproject:/workdir \
          ghcr.io/zephyrproject-rtos/zephyr-build:main

      # later on:
      docker start -ai zephyr-dev

.. note::

   The ``user`` account in the image has UID and GID 1000, matching the default first user on
   most Linux distributions, so files created in the mounted workspace belong to your own user
   on the host. If your UID is different, build the image yourself with matching ``UID`` and
   ``GID`` build arguments, as described in the `docker-image`_ repository. If Git refuses to
   operate on the workspace with a "dubious ownership" error, allow the directory with
   ``git config --global --add safe.directory <path>``.

Build and run an application
****************************

Inside the container, applications are built, run, and tested exactly as described in
:ref:`application`. For instance, build and run the :zephyr:code-sample:`hello_world` sample in
QEMU:

.. zephyr-app-commands::
   :zephyr-app: samples/hello_world
   :host-os: unix
   :board: qemu_x86
   :goals: build run

To exit QEMU, type :kbd:`Ctrl-a`, then :kbd:`x`. :ref:`Twister <twister_script>` works out of
the box as well, in the same environment CI uses to run it.

Display samples
===============

The ``zephyr-build`` image runs a virtual display and a VNC server, which lets you interact
with applications that produce display output, such as the :zephyr:code-sample:`display`
sample built for :zephyr:board:`native_sim <native_sim>`. Publish the VNC port when starting
the container by adding ``-p 5900:5900`` to the ``docker run`` command, then build and run the
sample:

.. zephyr-app-commands::
   :zephyr-app: samples/drivers/display
   :host-os: unix
   :board: native_sim
   :goals: build run

Connect a VNC client to ``localhost:5900`` to see the sample's output. The default VNC
password is ``zephyr``.

Working with hardware
*********************

Flashing and debugging a board from inside a container requires the container to have access
to the board's USB interfaces, typically a debug probe and a serial console. How to make these
visible to the container, and whether it is possible at all, depends on the host operating
system.

.. tabs::

   .. group-tab:: Linux

      Docker on Linux can pass host devices directly to containers. For a board that shows up
      on the host as :file:`/dev/ttyACM0`, add a ``--device`` option to the ``docker run``
      command:

      .. code-block:: shell

         docker run -it --rm --device /dev/ttyACM0 -v ~/zephyrproject:/workdir \
             ghcr.io/zephyrproject-rtos/zephyr-build:main

      ``--device`` maps the device node as it exists when the container starts: the board must
      already be plugged in, and unplugging it invalidates the mapping until the container is
      restarted. For flashing and debugging tools that access the probe through libusb
      (OpenOCD, pyOCD, and most others), mount the USB bus instead and allow USB device access
      with a device cgroup rule (189 is the major number assigned to USB device nodes). Unlike
      ``--device``, this keeps working when devices are unplugged and plugged back in:

      .. code-block:: shell

         docker run -it --rm -v /dev/bus/usb:/dev/bus/usb \
             --device-cgroup-rule='c 189:* rmw' -v ~/zephyrproject:/workdir \
             ghcr.io/zephyrproject-rtos/zephyr-build:main

      Running the container with ``--privileged -v /dev:/dev`` is a simpler but much blunter
      alternative that exposes all host devices to the container.

      Also keep in mind that:

      * udev runs on the host, not in the container. The udev rules that many flashing tools
        depend on must be installed on the host; see :ref:`setting-udev-rules`.
      * Device nodes keep their host ownership and permissions inside the container. The
        ``user`` account is a member of the ``plugdev`` group and may use ``sudo``, but serial
        ports usually belong to a group (such as ``dialout``) it is not a member of. In that
        case, add the device's group to the container user, for example with
        ``--group-add $(stat -c '%g' /dev/ttyACM0)``.

   .. group-tab:: macOS

      Docker Desktop runs containers in a virtual machine that has no access to the host's USB
      devices, and there is currently no supported way of passing a USB device through to a
      container on macOS.

      The most practical approach is to build in the container and flash from the host. Build
      artifacts land in the mounted workspace, so they are directly visible on the host: point
      the flashing tool for your board (see :ref:`flash-debug-host-tools`) at, for example,
      :file:`build/zephyr/zephyr.hex`, or copy the UF2 file to boards that enumerate as USB
      mass storage. Similarly, use a terminal program on the host to open the board's serial
      console.

      For ``west flash`` itself to be usable from the host, install west and the board's
      flashing tool on the host, and mount the workspace at the same absolute path inside the
      container as on the host:

      .. code-block:: shell

         docker run -it --rm -v $HOME/zephyrproject:$HOME/zephyrproject \
             -w $HOME/zephyrproject ghcr.io/zephyrproject-rtos/zephyr-build:main

      The paths recorded in build directories created this way are valid on the host too, so a
      ``west flash`` from the host workspace picks up the artifacts built in the container.

      Another option is to run a debug server that supports network connections, such as
      SEGGER's J-Link Remote Server or OpenOCD, directly on the host, and connect to it from
      within the container through ``host.docker.internal``.

   .. group-tab:: Windows

      Docker Desktop runs containers in a WSL 2 virtual machine that cannot see the host's USB
      devices by default. `usbipd-win`_ can attach USB devices to WSL 2, making them available
      to containers:

      #. Install usbipd-win, for example with ``winget install usbipd``.

      #. List the USB devices connected to the machine and note the bus ID of the one you want
         to share (for example ``2-4``):

         .. code-block:: bat

            usbipd list

      #. From an elevated (administrator) terminal, allow the device to be shared. This only
         needs to be done once per device:

         .. code-block:: bat

            usbipd bind --busid 2-4

      #. Attach the device to WSL 2. Run this again whenever the device is replugged, or pass
         ``--auto-attach`` to have usbipd reattach it automatically:

         .. code-block:: bat

            usbipd attach --wsl --busid 2-4

      The device is now handled by the WSL 2 kernel, which is shared with Docker Desktop, and
      shows up in containers like it does on a native Linux host. Start the container with the
      appropriate ``--device`` option as described in the Linux instructions, for example
      ``--device /dev/ttyACM0``.

      .. note::

         While attached to WSL 2, the device is not usable from Windows itself. Detach it with
         ``usbipd detach --busid <BUSID>``, or unplug it, to hand it back to Windows.

Flashing and debugging
======================

Once the board's USB interfaces are visible from within the container, ``west flash`` and
``west debug`` work as they do in a native environment. The Zephyr SDK bundled with the images
provides OpenOCD, so boards flashed through OpenOCD work out of the box. Boards whose runners
rely on other :ref:`flash and debug host tools <flash-debug-host-tools>`, such as pyOCD or the
J-Link tools, need those tools installed into the container first, either manually or through
a `custom image <Custom images_>`_.

Serial console
==============

The images come with pyserial preinstalled, so the quickest way to open the board's serial
console from within the container is miniterm:

.. code-block:: shell

   python -m serial.tools.miniterm /dev/ttyACM0 115200

Press :kbd:`Ctrl-]` to exit. You can also use a serial terminal program on the host instead of
going through the container at all (on Windows, detach the device from WSL 2 first).

Custom images
*************

The official images are designed to serve as bases for customized ones. Extend ``zephyr-build``
or ``ci`` when you need additional tools, for example a vendor's flashing utility:

.. code-block:: docker

   FROM ghcr.io/zephyrproject-rtos/ci:v0.29.3

   # Install additional tools here.

Base your image on ``ci-base`` instead if you want to provide your own toolchain rather than
the full Zephyr SDK. The Dockerfiles of the official images live in the `docker-image`_
repository, which also describes how to rebuild them locally, for instance with a user account
matching your host UID.

The images can also be used with tooling built on top of containers, such as
`Development Containers`_: point your :file:`devcontainer.json` at one of them (or at a custom
image derived from them) to develop in a container from Visual Studio Code and other compatible
tools, including GitHub Codespaces.

.. _docker-image: https://github.com/zephyrproject-rtos/docker-image
.. _docker-image releases: https://github.com/zephyrproject-rtos/docker-image/releases
.. _Embedded Containers: https://github.com/embeddedcontainers/zephyr
.. _Docker Engine: https://docs.docker.com/engine/install/
.. _post-installation steps: https://docs.docker.com/engine/install/linux-postinstall/
.. _Docker Desktop for Mac: https://docs.docker.com/desktop/setup/install/mac-install/
.. _Docker Desktop for Windows: https://docs.docker.com/desktop/setup/install/windows-install/
.. _usbipd-win: https://github.com/dorssel/usbipd-win
.. _Development Containers: https://containers.dev/
