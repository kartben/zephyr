:orphan:

.. _west_projects_index:

West Projects index
###################

This page acts as an index of projects (modules) compatible with the
:ref:`West <west>` meta-tool.

It primarily lists the components declared in Zephyr's default
:zephyr_file:`manifest file <west.yml>`. See :ref:`external-contributions` for
more information about the contribution and review process for these imported
components.

It also maintains a registry of :ref:`external projects
<west_external_projects>`, maintained outside of the Zephyr Project, that can
easily be integrated into a Zephyr workspace.

Active Projects/Modules
+++++++++++++++++++++++

The projects below are enabled by default and will be downloaded when you
call :command:`west update`. Many of the projects or modules listed below are
essential for building generic Zephyr application and include among others
hardware support for many of the platforms available in Zephyr.

To disable any of the active modules, for example a specific HAL, use the
following commands::

        west config manifest.project-filter -- -hal_FOO
        west update

.. manifest-projects-table::
   :filter: active

Inactive and Optional Projects/Modules
++++++++++++++++++++++++++++++++++++++

The projects below are optional and will not be downloaded when you
call :command:`west update`. You can add any of the projects or modules listed below
and use them to write application code and extend your workspace with the added
functionality.

To enable any of the modules below, use the following commands::

        west config manifest.project-filter -- +nanopb
        west update

.. manifest-projects-table::
   :filter: inactive

.. _west_external_projects:

External Projects/Modules
++++++++++++++++++++++++++

The `West Modules Registry <https://github.com/beriberikix/west-modules-registry>`_
catalogs third-party modules compatible with Zephyr.
Each entry below includes the :file:`west.yml` snippet needed to integrate the
module into your workspace. See :ref:`west-manifest-import` for information on
recommended ways to do this while still inheriting the mandatory modules from
Zephyr's :file:`west.yml`.

.. wmr-catalog::

You can also browse community-contributed module documentation:

.. toctree::
   :titlesonly:
   :maxdepth: 1
   :glob:

   external/*
