.. _boards:

Supported Boards and Shields
############################

If you are looking to add Zephyr support for a new board, please start with the
:ref:`board_porting_guide`.

When adding support documentation for each board, remember to use the template
available under :zephyr_file:`doc/templates/board.tmpl`.

Use the interactive search form below to filter the list of supported boards and shields.
Shields are a special type of board that are designed to be stacked on top of another board to
provide additional functionality.

.. toctree::
   :maxdepth: 2
   :glob:
   :hidden:

   */index

.. raw:: html

    <style>
      .hidden {
        display: none !important;
      }

      .filter-form {
        display: flex;
        flex-wrap: wrap;
        gap: 10px;
        margin-bottom: 20px;
        padding: 10px;
        border-radius: 8px;
        box-shadow: 0 2px 5px rgba(0, 0, 0, 0.1);
      }

      .filter-form input[type="text"],
      .filter-form input[type="number"],
      .filter-form select {
        padding: 10px;
        border: 1px solid #ccc;
        border-radius: 4px;
        flex: 1 1 200px;
        background-color: var(--input-background-color);
        color: var(--body-color);
        transition: all 0.3s ease;
      }

      .filter-form select {
        width: 100%;
        text-overflow: ellipsis;
        overflow: hidden;
        white-space: nowrap;
      }

      .filter-form input[type="text"]:focus,
      .filter-form input[type="number"]:focus,
      .filter-form select:focus {
        border-color: var(--input-focus-border-color);
      }

      #catalog {
        display: flex;
        flex-wrap: wrap;
        gap: 20px;
        justify-content: center;
        margin-top: 20px;
      }

      .board-card {
        flex: 1 1 calc(33.3% - 20px);
        /* Three cards per row */
        max-width: calc(33.3% - 20px);
        border-radius: 8px;
        padding: 15px 20px;
        background-color: var(--admonition-note-background-color);
        box-shadow: 0 4px 8px rgba(0, 0, 0, 0.1);
        display: flex;
        flex-direction: column;
        align-items: center;
        transition: transform 0.3s ease;
      }

      .board-card:hover, .board-card:focus {
        box-shadow: 0 6px 12px rgba(0, 0, 0, 0.15);
        transform: translateY(-5px);
        text-decoration: none;
      }

      .board-card .picture {
        width: auto;
        height: auto;
        min-height: 100px;
        max-height: 180px;
        border-radius: 4px;
        display: block;
        margin-left: auto;
        margin-right: auto;
        display: flex;
        align-items: center;
        flex-grow: 1;
        padding: 10px 0px;
      }

      .board-card .no-picture {
        font-size: 5em;
        color: var(--admonition-note-title-background-color);
        justify-content: center;
      }

      @media (max-width: 1024px) {
        .board-card {
          flex: 1 1 calc(50% - 20px);
          max-width: calc(50% - 20px);
        }
      }

      @media (max-width: 768px) {
        .board-card {
          flex: 1 1 calc(100% - 20px);
          max-width: calc(100% - 20px);
        }

        .board-card .picture {
          min-height: 60px;
          max-height: 120px;
        }
      }

      .board-card .vendor {
        font-size: 12px;
        color: var(--admonition-note-color);
        font-weight: 900;
        margin-bottom: 18px;
        opacity: 0.5;
      }

      .board-card img {
        max-height: 100%;
        max-width: 100%;
        object-fit: contain;
      }

      .board-card .board-name {
        font-family: var(--header-font-family);
        margin: auto 0 5px;
        text-align: center;
        font-size: 18px;
        font-weight: 500;
        color: var(--body-color);
        padding-top: 10px;
      }

      .board-card .arch {
        margin: 5px 0;
        text-align: center;
        font-size: 12px;
        font-weight: 100;
        color: var(--body-color);
      }

      #catalog.compact {
        display: block;
        list-style-type: disc;
        margin: 20px;
      }

      #catalog.compact .board-card {
        display: list-item;
        padding: 4px 0;
        border: none;
        background-color: transparent;
        box-shadow: none;
        list-style-position: outside;
        max-width: none;
      }

      #catalog.compact .board-card .vendor,
      #catalog.compact .board-card .picture,
      #catalog.compact .board-card .shield-indicator{
        display: none;
      }

      #catalog.compact .board-card .board-name {
        display: inline;
        font-family: var(--system-font-family);
        text-align: left;
        font-size: 16px;
        font-weight: normal;
        color: var(--body-color);
        margin: 0;
        padding: 0;
      }

      #catalog.compact .board-card .arch {
        display: inline;
      }

      #catalog.compact .board-card .arch::before {
        content: " (";
      }

      #catalog.compact .board-card .arch::after {
        content: ")";
      }

      #catalog.compact .board-card:hover {
        box-shadow: none;
        transform: none;
        text-decoration: underline;
      }
    </style>

    <form class="filter-form">
      <input type="text" id="name" placeholder="Name" style="flex-basis: 600px" />
      <select id="arch">
        <option value="">Select Architecture</option>
        <!-- Architecture options will be populated dynamically -->
      </select>
      <select id="vendor">
        <option value="">Select Vendor</option>
        <!-- Vendor options will be populated dynamically -->
      </select>
    </form>

    <div id="form-options" style="text-align: center; margin-bottom: 20px">
      <button
        id="reset-filters"
        class="btn btn-info btn-disabled fa fa-times"
        onclick="document.querySelector('.filter-form').reset(); filterBoards(); updateURL();"
      >
        Reset Filters
      </button>
      <button id="toggle-compact" class="btn btn-info fa fa-bars" onclick="toggleDisplayMode(this)">
        Switch to Compact Mode
      </button>
    </div>

    <div id="nb-matches" style="text-align: center"></div>

    <div id="catalog">
      <!-- Board cards will be populated dynamically -->
    </div>

    <script>
      // Function to alternate between compact and card view
      function toggleDisplayMode(btn) {
        const catalog = document.getElementById("catalog");
        catalog.classList.toggle("compact");
        btn.classList.toggle("fa-bars");
        btn.classList.toggle("fa-th");
        btn.textContent = catalog.classList.contains("compact")
          ? " Switch to Card View"
          : " Switch to Compact Mode";
      }

      // Function to get URL parameters from hash and populate form fields
      function populateFormFromURL() {
        const params = ["name", "arch", "vendor"];
        const hashParams = new URLSearchParams(window.location.hash.slice(1)); // remove '#' and parse

        params.forEach(param => {
          const element = document.getElementById(param);
          if (hashParams.has(param)) {
            element.value = hashParams.get(param);
          }
        });

        // Trigger filtering based on hash parameters
        filterBoards();
      }

      // Function to update the hash based on form inputs
      function updateURL() {
        const params = ["name", "arch", "vendor"];
        const hashParams = new URLSearchParams(window.location.hash.slice(1)); // remove '#' and parse

        params.forEach(param => {
          const value = document.getElementById(param).value;
          value ? hashParams.set(param, value) : hashParams.delete(param);
        });

        // Update the URL hash without reloading the page
        window.history.replaceState({}, "", `#${hashParams.toString()}`);
      }

      document.addEventListener("DOMContentLoaded", function () {
        let data;

        // Guess path to the board catalog JSON file based on the download link on this page
        const boardCatalogLink = document.querySelector("a.reference.download.internal[href$='.json']");

        // Fetch the JSON data
        fetch(boardCatalogLink.href)
          .then((response) => response.json())
          .then((jsonData) => {
            data = jsonData;

            const archs = new Set();
            const vendors = Object.fromEntries(
                Object.entries(data.vendors).filter(([vendor]) =>
                  Object.values(data.boards).some(board => board.vendor === vendor)
                )
              );

            Object.values(data.boards).forEach((board) => {
              board.archs?.forEach((arch) => archs.add(arch));
            });

            archToHumanReadable = {
              arc: "Synopsys DesignWare ARC",
              arm: "ARM",
              arm64: "ARM 64",
              mips: "MIPS",
              nios2: "Altera Nios II",
              posix: "POSIX",
              riscv: "RISC-V",
              sparc: "SPARC",
              x86: "x86",
              xtensa: "Xtensa",
            };

            // Populate architecture select, sorted alphabetically by human-readable name
            const archSelect = document.getElementById("arch");
            Array.from(archs)
              .sort((a, b) =>
                archToHumanReadable[a].localeCompare(archToHumanReadable[b], undefined, {
                  sensitivity: "base",
                })
              )
              .forEach((arch) => {
                const option = document.createElement("option");
                option.value = arch;
                option.textContent = archToHumanReadable[arch];
                archSelect.appendChild(option);
              });

            // Populate vendor select, sorted alphabetically
            const vendorSelect = document.getElementById("vendor");
            Array.from(Object.entries(vendors))
              .sort((a, b) => a[1].localeCompare(b[1], undefined, { sensitivity: "base" }))
              .forEach(([prefix, vendor]) => {
                const option = document.createElement("option");
                option.value = prefix;
                option.textContent = vendor;
                vendorSelect.appendChild(option);
              });

            createCards(data.boards);
            updateBoardCount();
            populateFormFromURL();

            const form = document.querySelector(".filter-form");

            // Prevent form submission
            form.addEventListener("submit", function (event) {
              event.preventDefault();
            });

            form.addEventListener("input", function () {
              filterBoards(data.boards);
              updateURL();
            });
          });
      });

      function createCards(data) {
        const catalog = document.getElementById("catalog");
        catalog.innerHTML = ""; // Clear existing content

        // Sort boards alphabetically
        Object.entries(data)
          .sort(([keyA, boardA], [keyB, boardB]) =>
            boardA.full_name.localeCompare(boardB.full_name, undefined, { sensitivity: "base" })
          )
          .forEach(([name, board]) => {
            const boardCard = document.createElement("a");
            boardCard.className = "board-card";
            if (board.doc_page) {
              boardCard.href = "../" + board.doc_page.replace(".rst", ".html");
            } else {
              boardCard.href = "#";
            }
            boardCard.tabIndex = 0;

            // Set data attributes for filtering
            boardCard.setAttribute(
              "data-arch",
              (board.archs || []).map((arch) => arch.toLowerCase()).join(" ")
            );
            boardCard.setAttribute("data-vendor", (board.vendor || "").toLowerCase());

            // Create vendor name element
            const vendorName = document.createElement("div");
            vendorName.className = "vendor";
            vendorName.textContent = board.vendor || "Unknown Vendor";
            boardCard.appendChild(vendorName);

            // Image element
            let imgElement;
            if (board.image) {
              imgElement = document.createElement("img");
              imgElement.alt = `A picture of the ${board.full_name} board`;
              imgElement.src = board.image;
            } else {
              imgElement = document.createElement("div");
              imgElement.className = "no-picture fa fa-microchip";
            }
            imgElement.classList.add("picture");
            boardCard.appendChild(imgElement);

            // Add shield indicator if the board is a shield
            if (board.type === "shield") {
              const shieldIndicator = document.createElement("div");
              shieldIndicator.className = "shield-indicator fa fa-shield";
              shieldIndicator.style.fontSize = "1.5em";
              shieldIndicator.style.color = "#4CAF50"; // Customize color if needed
              boardCard.appendChild(shieldIndicator);
            }

            // Board name
            boardDiv = document.createElement("div");
            boardDiv.className = "board-name";
            boardDiv.textContent = board.full_name || "Unknown Board";
            boardCard.appendChild(boardDiv);

            // Board architecture info
            archDiv = document.createElement("div");
            archDiv.className = "arch";
            // If the board has archs, display them in human-readable form, otherwise display "N/A"
            const archText =
              board.archs && board.archs.length > 0
                ? board.archs.map((arch) => archToHumanReadable[arch]).join(", ")
                : "";
            archDiv.textContent = `${archText}`;
            boardCard.appendChild(archDiv);

            catalog.appendChild(boardCard);
          });
      }

      function updateBoardCount() {
        const boards = document.getElementsByClassName("board-card");
        const visibleBoards = Array.from(boards).filter(
          (board) => !board.classList.contains("hidden")
        ).length;
        const totalBoards = boards.length;
        document.getElementById(
          "nb-matches"
        ).textContent = `Showing ${visibleBoards} of ${totalBoards}`;
      }

      function filterBoards(data) {
        const nameInput = document.getElementById("name").value.toLowerCase();
        const archSelect = document.getElementById("arch").value.toLowerCase();
        const vendorSelect = document.getElementById("vendor").value.toLowerCase();

        const resetFiltersBtn = document.getElementById("reset-filters");
        if (nameInput || archSelect || vendorSelect) {
          resetFiltersBtn.classList.remove("btn-disabled");
        } else {
          resetFiltersBtn.classList.add("btn-disabled");
        }

        const boards = document.getElementsByClassName("board-card");

        Array.from(boards).forEach(function (board) {
          const boardName = board.querySelector("h3").textContent.toLowerCase();
          const boardArchs = board.getAttribute("data-arch").toLowerCase().split(" ");
          const boardVendor = board.getAttribute("data-vendor").toLowerCase();

          let matches = true;

          if (nameInput && !boardName.includes(nameInput)) matches = false;
          if (archSelect && !boardArchs.includes(archSelect)) matches = false;
          if (vendorSelect && boardVendor !== vendorSelect) matches = false;

          if (matches) {
            board.classList.remove("hidden");
          } else {
            board.classList.add("hidden");
          }
        });

        updateBoardCount();
      }
    </script>


:download:`Raw board catalog <_catalog/board_catalog.json>`.

.. _boards-shields:

Shields
#######

.. toctree::
   :maxdepth: 1
   :glob:

   shields/**/*
