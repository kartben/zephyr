.. _boards:

Supported Boards
################

Zephyr project developers are continually adding board-specific support as
documented below.

If you are looking to add Zephyr support for a new board, please start with the
:ref:`board_porting_guide`.

When adding support documentation for each board, remember to use the template
available under :zephyr_file:`doc/templates/board.tmpl`.


.. toctree::
   :maxdepth: 2
   :glob:
   :hidden:

   */index

.. raw:: html

    <form class="filter-form">
      <input type="text" id="name" placeholder="Name" style="flex-basis:600px">
      <select id="arch">
        <option value="">Select Architecture</option>
        <!-- Architecture options will be populated dynamically -->
      </select>
      <select id="vendor">
        <option value="">Select Vendor</option>
        <!-- Vendor options will be populated dynamically -->
      </select>
      <input type="number" id="minRam" placeholder="Min RAM (KB)" />
      <input type="number" id="minFlash" placeholder="Min Flash (KB)" />

      <div class="capabilities" id="capabilities-container">
        <!-- Capability checkboxes will be populated dynamically -->
      </div>
    </form>

    <div id="number-of-matches" style="text-align: center;"></div>

    <div class="catalog" id="catalog">
      <!-- Board cards will be populated dynamically -->
    </div>

    <script>
      function formatSize(sizeInKB) {
        function smartRound(num) {
          return num.toLocaleString('en-US', { minimumFractionDigits: 0, maximumFractionDigits: 2 });
        }

        if (sizeInKB >= 1024 * 1024) {
          return smartRound(sizeInKB / (1024 * 1024)) + " GB";
        } else if (sizeInKB >= 1024) {
          return smartRound(sizeInKB / 1024) + " MB";
        } else {
          return smartRound(sizeInKB) + " KB";
        }
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

            // Get unique architectures, vendors and capabilities
            const archs = new Set();
            const vendors = new Set();
            const capabilitiesSet = new Set();

            // Process data to get vendors and capabilities
            Object.values(data).forEach((board) => {
              if (board.arch) {
                archs.add(board.arch);
              }
              if (board.vendor) {
                vendors.add(board.vendor);
              }
              if (Array.isArray(board.supported)) {
                board.supported.forEach((cap) => capabilitiesSet.add(cap));
              }
            });

            archToHumanReadable = {
              "arc": "Synopsys DesignWare ARC",
              "arm": "ARM",
              "arm64": "ARM 64",
              "mips": "MIPS",
              "nios2": "Altera Nios II",
              "posix": "POSIX",
              "riscv": "RISC-V",
              "sparc": "SPARC",
              "x86": "x86",
              "xtensa": "Xtensa"
            };

            // Populate architecture select, sorted alphabetically by human-readable name
            const archSelect = document.getElementById("arch");
            Array.from(archs)
              .sort((a, b) => archToHumanReadable[a].localeCompare(archToHumanReadable[b], undefined, { sensitivity: "base" }))
              .forEach((arch) => {
                const option = document.createElement("option");
                option.value = arch;
                option.textContent = archToHumanReadable[arch];
                archSelect.appendChild(option);
              });

            // Populate vendor select, sorted alphabetically
            const vendorSelect = document.getElementById("vendor");
            Array.from(vendors)
              .sort((a, b) => a.localeCompare(b, undefined, { sensitivity: "base" }))
              .forEach((vendor) => {
                const option = document.createElement("option");
                option.value = vendor;
                option.textContent = vendor;
                vendorSelect.appendChild(option);
              });

            // Populate capabilities checkboxes, sorted alphabetically
            const capabilitiesContainer = document.getElementById("capabilities-container");
            const capabilitiesArray = Array.from(capabilitiesSet);
            capabilitiesArray
              .sort((a, b) => {
                return a.localeCompare(b, undefined, { sensitivity: "base" });
              })
              .forEach((capability) => {
                const label = document.createElement("label");
                const checkbox = document.createElement("input");
                checkbox.type = "checkbox";
                checkbox.id = capability;
                checkbox.value = capability;
                label.appendChild(checkbox);
                label.appendChild(document.createTextNode(" " + capability));
                capabilitiesContainer.appendChild(label);
              });

            generateBoardCards(data);
            updateBoardCount();

            const form = document.querySelector(".filter-form");
            form.addEventListener("input", function () {
              filterBoards(data);
            });
          });
      });

      function generateBoardCards(data) {
        const catalog = document.getElementById("catalog");
        catalog.innerHTML = ""; // Clear existing content

        // sort boards alphabetically
        Object.values(data)
          .sort((a, b) => a.name.localeCompare(b.name, undefined, { sensitivity: "base" }))
          .forEach((board) => {
            const boardCard = document.createElement("div");
            boardCard.className = "board-card";

            // Set data attributes for filtering
            boardCard.setAttribute("data-arch", (board.arch || "").toLowerCase());
            boardCard.setAttribute("data-vendor", (board.vendor || "").toLowerCase());
            boardCard.setAttribute("data-ram", board.ram || "0");
            boardCard.setAttribute("data-flash", board.flash || "0");
            boardCard.setAttribute(
              "data-capabilities",
              (board.supported || []).join(" ").toLowerCase()
            );

            // Create image
            const img = document.createElement("img");
            if (board.image) {
              img.src = board.image.replace("/Users/kartben/zephyrproject/zephyr/", "/");
            } else {
              img.src = "https://via.placeholder.com/200";
            }
            img.alt = "Board Image";
            boardCard.appendChild(img);

            // Board name
            const h3 = document.createElement("h3");
            h3.textContent = board.name || "Unknown Board";
            boardCard.appendChild(h3);

            basicInfoPara = document.createElement("p");

            // Architecture
            const pArch = document.createTextNode("Architecture: ");
            pArch.textContent += (board.arch || "N/A");
            basicInfoPara.appendChild(pArch);
            basicInfoPara.appendChild(document.createElement("br"));

            // RAM
            const pRAM = document.createTextNode("RAM: ");
            pRAM.textContent += board.ram ? formatSize(board.ram) : "N/A";
            basicInfoPara.appendChild(pRAM);
            basicInfoPara.appendChild(document.createElement("br"));

            // Flash
            const pFlash = document.createTextNode("Flash: ");
            pFlash.textContent += board.flash ? formatSize(board.flash) : "N/A";
            basicInfoPara.appendChild(pFlash);

            boardCard.appendChild(basicInfoPara);

            // Capabilities
            const capabilitiesDiv = document.createElement("div");
            capabilitiesDiv.className = "capabilities";
            (board.supported || []).forEach((capability) => {
              const span = document.createElement("span");
              span.className = "capability-tag";
              span.textContent = capability;
              span.addEventListener("click", function (e) {
                e.stopPropagation();
                const capabilitiesContainer = document.getElementById("capabilities-container");
                const checkbox = capabilitiesContainer.querySelector(`input[value="${capability}"]`);
                checkbox.checked = !checkbox.checked;
                filterBoards(data);
              });
              capabilitiesDiv.appendChild(span);
            });
            boardCard.appendChild(capabilitiesDiv);

            // clicking on the boards card should open the board's README, that can be found at
            // ./<vendor>/<board_name>/README.rst
            boardCard.addEventListener("click", function () {
              window.open(`../${board.dir}/doc/index.html`, "_self");
            });

            document.getElementById("catalog").appendChild(boardCard);
          });
      }

      function updateBoardCount() {
        const boards = document.getElementsByClassName("board-card");
        const visibleBoards = Array.from(boards).filter((board) => board.style.display !== "none").length;
        const totalBoards = boards.length;
        document.getElementById("number-of-matches").textContent = `Showing ${visibleBoards} of ${totalBoards} boards`;
      }


      function filterBoards(data) {
        const nameInput = document.getElementById("name").value.toLowerCase();
        const archSelect = document.getElementById("arch").value.toLowerCase();
        const vendorSelect = document.getElementById("vendor").value.toLowerCase();
        const minRam = parseInt(document.getElementById("minRam").value, 10) || 0;
        const minFlash = parseInt(document.getElementById("minFlash").value, 10) || 0;

        console.log(archSelect);

        // Get selected capabilities
        const capabilitiesContainer = document.getElementById("capabilities-container");
        const selectedCapabilities = Array.from(
          capabilitiesContainer.querySelectorAll("input[type=checkbox]:checked")
        ).map((checkbox) => checkbox.value.toLowerCase());

        const boards = document.getElementsByClassName("board-card");

        Array.from(boards).forEach(function (board) {
          const boardName = board.querySelector("h3").textContent.toLowerCase();
          const boardArch = board.getAttribute("data-arch").toLowerCase();
          const boardVendor = board.getAttribute("data-vendor").toLowerCase();
          const boardRam = parseInt(board.getAttribute("data-ram"), 10);
          const boardFlash = parseInt(board.getAttribute("data-flash"), 10);
          const boardCapabilities = board.getAttribute("data-capabilities");

          let matches = true;

          if (nameInput && !boardName.includes(nameInput)) matches = false;
          if (archSelect && !boardArch.includes(archSelect)) matches = false;
          if (vendorSelect && boardVendor !== vendorSelect) matches = false;
          if (boardRam < minRam) matches = false;
          if (boardFlash < minFlash) matches = false;

          // Check capabilities
          for (const cap of selectedCapabilities) {
            if (!boardCapabilities.includes(cap)) {
              matches = false;
              break;
            }
          }

          // Ensure the "capability-tag" spans also have a class of "selected" if they are selected
          const capabilitiesDiv = board.querySelector(".capabilities");
          capabilitiesDiv.childNodes.forEach((span) => {
            if (selectedCapabilities.includes(span.textContent.toLowerCase())) {
              span.classList.add("selected");
            } else {
              span.classList.remove("selected");
            }
          });

          if (matches) {
            board.style.display = "block";
          } else {
            board.style.display = "none";
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
