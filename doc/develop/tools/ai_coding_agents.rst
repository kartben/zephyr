.. _ai_coding_agents:

AI coding agents
################

AI coding assistants and agent-enabled IDEs can connect to external tools and
knowledge sources through the :abbr:`MCP (Model Context Protocol)`, an open
standard for exchanging tool calls and structured results with a model. Zephyr
offers two MCP servers that complement each other:

- the hosted documentation server described in :ref:`kapa_ai`, which answers
  questions from the project's documentation and source code;
- the local ``west mcp`` server described on this page, which lets an agent
  inspect and drive the west workspace it is working in: list boards and
  modules, build applications, read build results, and run tests.

.. note::

   This page is about a program that runs on the development host. It is
   unrelated to the :ref:`MCP server library <mcp_server_interface>`
   (:kconfig:option:`CONFIG_MCP_SERVER`) that lets a Zephyr application itself
   act as an MCP server on a device.

Installing
**********

``west mcp`` needs the optional ``mcp`` Python package, which is listed in
:file:`scripts/requirements-extras.txt`. Install it into the Python
environment that runs west, for example:

.. code-block:: console

   pip install "mcp>=2.1"

or install all optional packages with ``west packages pip --install``.

Connecting an agent
*******************

The server speaks MCP over standard input and output and is started by the
client, so it is configured like any other local MCP server. The command is
``west mcp``, run from anywhere inside the workspace. Most clients read a JSON
configuration file; for instance:

.. tabs::

   .. group-tab:: Claude Code

      Run the following in the workspace, or add the equivalent entry to
      :file:`.mcp.json`:

      .. code-block:: console

         claude mcp add zephyr -- west mcp

   .. group-tab:: VS Code

      In :file:`.vscode/mcp.json`:

      .. code-block:: json

         {
           "servers": {
             "zephyr": {
               "type": "stdio",
               "command": "west",
               "args": ["mcp"]
             }
           }
         }

   .. group-tab:: Cursor and others

      In :file:`.cursor/mcp.json` or the client's equivalent:

      .. code-block:: json

         {
           "mcpServers": {
             "zephyr": {
               "command": "west",
               "args": ["mcp"]
             }
           }
         }

If ``west`` is not on the client's ``PATH`` (for example when it lives in a
Python virtual environment), use the interpreter of that environment instead:
``"command": "/path/to/.venv/bin/python", "args": ["-m", "west", "mcp"]``.
Set the client's working directory to the workspace when it does not do so
already.

.. warning::

   Do not start the server with ``west -v mcp``: the debug output west prints
   before the command runs would corrupt the protocol stream.

What the agent can do
*********************

The server describes itself to the agent, including how build targets, snippets,
sysbuild domains and pristine builds work, so an agent needs no prior knowledge
of the workspace layout. It exposes the following tools:

.. list-table::
   :header-rows: 1
   :widths: 25 55 20

   * - Tool
     - Purpose
     - Side effects
   * - ``workspace_info``
     - Paths, west and Zephyr versions, ``build.*`` defaults and what the
       server allows.
     - none
   * - ``list_projects``, ``list_modules``
     - Manifest projects and Zephyr modules.
     - none
   * - ``list_boards``, ``board_info``
     - Search boards; get their revisions, SoCs, variants and every valid
       ``west build -b`` target string.
     - none
   * - ``list_tests``
     - Twister test cases discovered under given directories.
     - none
   * - ``build_dir_info``, ``kconfig``, ``devicetree_query``, ``runners_info``
     - Inspect a build directory: board, sysbuild domains, CMake cache,
       ``.config`` values, final devicetree nodes and runner configuration.
     - none
   * - ``twister_results``
     - Summaries and details from an existing :file:`twister.json`.
     - none
   * - ``build``
     - ``west build`` with board, build directory, pristine mode, snippets,
       shields, extra configuration files and CMake arguments; returns parsed
       diagnostics and the log tail.
     - writes the build directory
   * - ``twister_run``
     - Twister, narrowed by test directories, scenarios, platforms and tags;
       reports progress, returns the summary and failed instances.
     - writes the output directory
   * - ``flash``
     - ``west flash``. Only offered with ``--allow-hardware``.
     - programs the connected board

Parameter-free views of the workspace and of a build directory are also
published as MCP resources (``zephyr://workspace``, ``zephyr://build/<dir>/info``,
``.../config`` and ``.../runners``) for clients that let users attach resources
to a conversation.

Long operations report their progress to the client and can be cancelled; the
server kills the underlying west process when that happens. Only one build,
test run or flash runs at a time. The complete output of every operation is
kept in a log file whose path is part of the result; the log directory is a
temporary directory unless ``--log-dir`` is given.

Environment
***********

The server is bound to one workspace and inherits the environment of the
client that starts it, so it is worth knowing how each piece is found:

- **The workspace** is located from the server's current directory, like every
  west command. Clients start the server in the project folder, so opening the
  IDE anywhere inside the workspace is enough. For an application that lives
  outside the workspace, point the client's working directory (or a launcher
  script) at the workspace and pass the application directory with ``--root``.
- **west and its Python dependencies** come from the interpreter that runs the
  server; the builds and test runs it starts use the same interpreter. Use the
  virtual environment's Python as the client's ``command`` rather than relying
  on ``west`` being on the client's ``PATH``.
- **ZEPHYR_BASE** is derived from the workspace; no variable is needed.
- **The toolchain** is found through the CMake package registry that
  ``west sdk install`` writes, so it needs no variable either. Setups that rely
  on exported variables such as ``ZEPHYR_TOOLCHAIN_VARIANT`` must pass them in
  the client configuration (``env`` in most clients, ``envFile`` in VS Code).
- **Host tools** such as ``cmake``, ``ninja`` and ``dtc`` must be on the
  ``PATH`` of the client process. An IDE launched from a desktop icon, rather
  than from a shell, typically has a minimal ``PATH`` and does not see them.

``west mcp --check`` prints what the server sees, including the tools it can
find and the SDK it detects, and the ``workspace_info`` tool reports the same
to the agent with hints about what to fix.

When the client cannot be configured precisely enough, a small launcher script
pins everything and is used as the ``command``:

.. code-block:: sh

   #!/bin/sh
   cd ~/zephyrproject || exit 1
   . ~/zephyrproject/.venv/bin/activate
   export PATH="/opt/homebrew/bin:$PATH"
   exec west mcp --root ~/my-app "$@"

Safety
******

The server is read-only by default, apart from creating build and test output
directories:

- ``flash`` is only registered when the server is started with
  ``--allow-hardware``, and twister options that drive hardware (for example
  ``--device-testing``) are rejected without it.
- Every path an agent passes must lie inside the workspace, or inside one of
  the directories given with ``--root``. Symbolic links are resolved before
  this check.
- ``west update`` and interactive commands such as ``west debug`` or
  ``menuconfig`` are not exposed.

.. code-block:: console

   west mcp --allow-hardware --root ~/my-app --log-dir ~/west-mcp-logs

The ``west`` sub-processes run with the environment of the server, so anything
available to a shell in the workspace is available to the agent's builds.
