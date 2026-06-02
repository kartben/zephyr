/**
 * Copyright (c) 2024-2025, The Linux Foundation.
 * SPDX-License-Identifier: Apache-2.0
 */

function toggleDisplayMode(btn) {
  const catalog = document.getElementById("catalog");
  catalog.classList.toggle("compact");
  btn.classList.toggle("fa-bars");
  btn.classList.toggle("fa-th");
  btn.textContent = catalog.classList.contains("compact")
    ? " Switch to Card View"
    : " Switch to Compact View";
}

function populateFormFromURL() {
  const params = ["name", "arch", "vendor", "soc"];
  const hashParams = new URLSearchParams(window.location.hash.slice(1));
  params.forEach((param) => {
    const element = document.getElementById(param);
    if (hashParams.has(param)) {
      const value = hashParams.get(param);
      if (param === "soc") {
        value.split(",").forEach(soc =>
          element.querySelector(`option[value="${soc}"]`).selected = true);
      } else {
        element.value = value;
      }
    }
  });

  // Restore memory slider positions (run after initMemorySliders sets the max)
  ["ram", "rom"].forEach(type => {
    const minEl = document.getElementById(`${type}-min`);
    const maxEl = document.getElementById(`${type}-max`);
    if (!minEl || !maxEl) return;
    if (hashParams.has(`${type}-min`)) minEl.value = hashParams.get(`${type}-min`);
    if (hashParams.has(`${type}-max`)) maxEl.value = hashParams.get(`${type}-max`);
    updateMemorySlider(type);
  });

  // Restore visibility toggles from URL
  ["show-boards", "show-shields"].forEach(toggle => {
    if (hashParams.has(toggle)) {
      document.getElementById(toggle).checked = hashParams.get(toggle) === "true";
    }
  });

  // Restore supported features from URL
  if (hashParams.has("features")) {
    const features = hashParams.get("features").split(",");
    setTimeout(() => {
      features.forEach(feature => {
        const tagContainer = document.getElementById('hwcaps-tags');
        const tagInput = document.getElementById('hwcaps-input');

        const tagElement = document.createElement('span');
        tagElement.classList.add('tag');
        tagElement.textContent = feature;
        tagElement.onclick = () => {
          tagElement.remove();
          filterBoards();
        };
        tagContainer.insertBefore(tagElement, tagInput);
      });
      filterBoards();
    }, 0);
  }

  // Restore compatibles from URL
  if (hashParams.has("compatibles")) {
    const compatibles = hashParams.get("compatibles").split("|");
    setTimeout(() => {
      compatibles.forEach(compatible => {
        const tagContainer = document.getElementById('compatibles-tags');
        const tagInput = document.getElementById('compatibles-input');

        const tagElement = document.createElement('span');
        tagElement.classList.add('tag');
        tagElement.textContent = compatible;
        tagElement.onclick = () => {
          tagElement.remove();
          filterBoards();
        };
        tagContainer.insertBefore(tagElement, tagInput);
      });
      filterBoards();
    }, 0);
  }

  filterBoards();
}

/**
 * True when the URL hash encodes catalog filters or the #catalog fragment, so the page
 * should start at the board grid rather than the introductory content.
 */
function hashIndicatesFilteredCatalogView() {
  const fields = new Set(["name", "arch", "vendor", "soc", "features", "compatibles"]);
  for (const [key, value] of new URLSearchParams(window.location.hash.slice(1))) {
    if (key === "catalog") return true;
    if (fields.has(key) && value !== "") return true;
    if ((key === "show-boards" || key === "show-shields") && value === "false") return true;
  }
  return false;
}

function updateURL() {
  const params = ["name", "arch", "vendor", "soc"];
  const hashParams = new URLSearchParams(window.location.hash.slice(1));

  params.forEach((param) => {
    const element = document.getElementById(param);
    if (param === "soc") {
      const selectedSocs = [...element.selectedOptions].map(({ value }) => value);
      selectedSocs.length ? hashParams.set(param, selectedSocs.join(",")) : hashParams.delete(param);
    } else {
      element.value ? hashParams.set(param, element.value) : hashParams.delete(param);
    }
  });

  // Persist memory slider positions only when non-default
  ["ram", "rom"].forEach(type => {
    const minEl = document.getElementById(`${type}-min`);
    const maxEl = document.getElementById(`${type}-max`);
    if (!minEl || !maxEl) return;
    const minVal = Number.parseInt(minEl.value, 10);
    const maxVal = Number.parseInt(maxEl.value, 10);
    const maxPossible = Number.parseInt(maxEl.max, 10);
    minVal > 0 ? hashParams.set(`${type}-min`, minVal) : hashParams.delete(`${type}-min`);
    maxVal < maxPossible ? hashParams.set(`${type}-max`, maxVal) : hashParams.delete(`${type}-max`);
  });

  ["show-boards", "show-shields"].forEach(toggle => {
    const isChecked = document.getElementById(toggle).checked;
    isChecked ? hashParams.delete(toggle) : hashParams.set(toggle, "false");
  });

  // Add supported features to URL
  const selectedHWTags = [...document.querySelectorAll('#hwcaps-tags .tag')].map(tag => tag.textContent);
  selectedHWTags.length ? hashParams.set("features", selectedHWTags.join(",")) : hashParams.delete("features");

  // Add compatibles to URL
  const selectedCompatibles = [...document.querySelectorAll('#compatibles-tags .tag')].map(tag => tag.textContent);
  selectedCompatibles.length ? hashParams.set("compatibles", selectedCompatibles.join("|")) : hashParams.delete("compatibles");

  window.history.replaceState({}, "", `#${hashParams.toString()}`);
}

function fillSocFamilySelect() {
  const socFamilySelect = document.getElementById("family");

  Object.keys(socs_data).sort().forEach(f => {
    socFamilySelect.add(new Option(f));
  });
}

function fillSocSeriesSelect(families, selectOnFill = false) {
  const socSeriesSelect = document.getElementById("series");

  families = families?.length ? families : Object.keys(socs_data);
  let allSeries = [...new Set(families.flatMap(f => Object.keys(socs_data[f])))];

  socSeriesSelect.innerHTML = "";
  allSeries.sort().map(s => {
    const option = new Option(s, s, selectOnFill, selectOnFill);
    socSeriesSelect.add(option);
  });
}

function fillSocSocSelect(families, series = undefined, selectOnFill = false) {
  const socSocSelect = document.getElementById("soc");

  families = families?.length ? families : Object.keys(socs_data);
  series = series?.length ? series : families.flatMap(f => Object.keys(socs_data[f]));
  matchingSocs = [...new Set(families.flatMap(f => series.flatMap(s => socs_data[f][s] || [])))];

  socSocSelect.innerHTML = "";
  matchingSocs.sort().forEach((soc) => {
    socSocSelect.add(new Option(soc, soc, selectOnFill, selectOnFill));
  });
}

function setupHWCapabilitiesField() {
  let selectedTags = [];

  const tagContainer = document.getElementById('hwcaps-tags');
  const tagInput = document.getElementById('hwcaps-input');
  const datalist = document.getElementById('tag-list');

  const tagCounts = Array.from(document.querySelectorAll('.board-card')).reduce((acc, board) => {
    (board.getAttribute('data-supported-features') || '').split(' ').forEach(tag => {
      acc[tag] = (acc[tag] || 0) + 1;
    });
    return acc;
  }, {});

  const allTags = Object.keys(tagCounts).sort();

  function addTag(tag) {
    if (selectedTags.includes(tag) || tag === "" || !allTags.includes(tag)) return;
    selectedTags.push(tag);

    const tagElement = document.createElement('span');
    tagElement.classList.add('tag');
    tagElement.textContent = tag;
    tagElement.onclick = () => removeTag(tag);
    tagContainer.insertBefore(tagElement, tagInput);

    tagInput.value = '';
    updateDatalist();
  }

  function removeTag(tag) {
    selectedTags = selectedTags.filter(t => t !== tag);
    document.querySelectorAll('.tag').forEach(el => {
      if (el.textContent.includes(tag)) el.remove();
    });
    updateDatalist();
  }

  function updateDatalist() {
    datalist.innerHTML = '';
    const filteredTags = allTags.filter(tag => !selectedTags.includes(tag));

    filteredTags.forEach(tag => {
      const option = document.createElement('option');
      option.value = tag;
      datalist.appendChild(option);
    });

    filterBoards();
  }

  tagInput.addEventListener('input', () => {
    if (allTags.includes(tagInput.value)) {
      addTag(tagInput.value);
    }
  });

  // Add tag when pressing the Enter key
  tagInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && allTags.includes(tagInput.value)) {
      addTag(tagInput.value);
      e.preventDefault();
    }
  });

  // Delete tag when pressing the Backspace key
  tagInput.addEventListener('keydown', (e) => {
    if (e.key === 'Backspace' && tagInput.value === '' && selectedTags.length > 0) {
      removeTag(selectedTags[selectedTags.length - 1]);
    }
  });

  updateDatalist();
}

function setupCompatiblesField() {
  let selectedCompatibles = [];

  const tagContainer = document.getElementById('compatibles-tags');
  const tagInput = document.getElementById('compatibles-input');
  const datalist = document.getElementById('compatibles-list');

  // Collect all unique compatibles from boards
  const allCompatibles = Array.from(document.querySelectorAll('.board-card')).reduce((acc, board) => {
    (board.getAttribute('data-compatibles') || '').split(' ').forEach(compat => {
      if (compat && !acc.includes(compat)) {
        acc.push(compat);
      }
    });
    return acc;
  }, []);

  allCompatibles.sort();

  function addCompatible(compatible) {
    if (selectedCompatibles.includes(compatible) || compatible === "") return;
    selectedCompatibles.push(compatible);

    const tagElement = document.createElement('span');
    tagElement.classList.add('tag');
    tagElement.textContent = compatible;
    tagElement.onclick = () => removeCompatible(compatible);
    tagContainer.insertBefore(tagElement, tagInput);

    tagInput.value = '';
    updateDatalist();
  }

  function removeCompatible(compatible) {
    selectedCompatibles = selectedCompatibles.filter(c => c !== compatible);
    document.querySelectorAll('.tag').forEach(el => {
      if (el.textContent === compatible && el.parentElement === tagContainer) {
        el.remove();
      }
    });
    updateDatalist();
  }

  function updateDatalist() {
    datalist.innerHTML = '';
    const filteredCompatibles = allCompatibles.filter(c => !selectedCompatibles.includes(c));

    filteredCompatibles.forEach(compatible => {
      const option = document.createElement('option');
      option.value = compatible;
      datalist.appendChild(option);
    });

    filterBoards();
  }

  tagInput.addEventListener('input', () => {
    if (allCompatibles.includes(tagInput.value)) {
      addCompatible(tagInput.value);
    }
  });

  tagInput.addEventListener('keydown', (e) => {
    if (e.key === 'Enter' && tagInput.value) {
      addCompatible(tagInput.value);
      e.preventDefault();
    } else if (e.key === 'Backspace' && tagInput.value === '' && selectedCompatibles.length > 0) {
      removeCompatible(selectedCompatibles[selectedCompatibles.length - 1]);
    }
  });

  updateDatalist();
}

document.addEventListener("DOMContentLoaded", function () {
  const form = document.querySelector(".filter-form");

  // sort vendors alphabetically
  vendorSelect = document.getElementById("vendor");
  vendorOptions = Array.from(vendorSelect.options).slice(1);
  vendorOptions.sort((a, b) => a.text.localeCompare(b.text));
  while (vendorSelect.options.length > 1) {
    vendorSelect.remove(1);
  }
  vendorOptions.forEach((option) => {
    vendorSelect.appendChild(option);
  });

  fillSocFamilySelect();
  fillSocSeriesSelect();
  fillSocSocSelect();
  initMemorySliders();
  populateFormFromURL();

  setupHWCapabilitiesField();
  setupCompatiblesField();

  socFamilySelect = document.getElementById("family");
  socFamilySelect.addEventListener("change", () => {
    const selectedFamilies = [...socFamilySelect.selectedOptions].map(({ value }) => value);
    fillSocSeriesSelect(selectedFamilies, true);
    fillSocSocSelect(selectedFamilies, undefined, true);
    filterBoards();
  });

  socSeriesSelect = document.getElementById("series");
  socSeriesSelect.addEventListener("change", () => {
    const selectedFamilies = [...socFamilySelect.selectedOptions].map(({ value }) => value);
    const selectedSeries = [...socSeriesSelect.selectedOptions].map(({ value }) => value);
    fillSocSocSelect(selectedFamilies, selectedSeries, true);
    filterBoards();
  });

  socSocSelect = document.getElementById("soc");
  socSocSelect.addEventListener("change", () => {
    filterBoards();
  });

  boardsToggle = document.getElementById("show-boards");
  boardsToggle.addEventListener("change", () => {
    filterBoards();
  });

  shieldsToggle = document.getElementById("show-shields");
  shieldsToggle.addEventListener("change", () => {
    filterBoards();
  });

  form.addEventListener("input", function () {
    filterBoards();
  });

  form.addEventListener("submit", function (event) {
    event.preventDefault();
  });

  filterBoards();

  setTimeout(() => {
    if (!hashIndicatesFilteredCatalogView()) return;
    document.getElementById("nb-matches")?.scrollIntoView({ behavior: "smooth", block: "start" });
  }, 0);
});

function resetForm() {
  const form = document.querySelector(".filter-form");
  form.reset();
  fillSocFamilySelect();
  fillSocSeriesSelect();
  fillSocSocSelect();

  document.getElementById("show-boards").checked = true;
  document.getElementById("show-shields").checked = true;

  // Clear supported features
  document.querySelectorAll('#hwcaps-tags .tag').forEach(tag => tag.remove());
  document.getElementById('hwcaps-input').value = '';

  // Clear compatibles
  document.querySelectorAll('#compatibles-tags .tag').forEach(tag => tag.remove());
  document.getElementById('compatibles-input').value = '';

  // Reset memory sliders to full range
  ["ram", "rom"].forEach(type => {
    const minEl = document.getElementById(`${type}-min`);
    const maxEl = document.getElementById(`${type}-max`);
    if (!minEl || !maxEl) return;
    minEl.value = 0;
    maxEl.value = maxEl.max;
    updateMemorySlider(type);
  });

  filterBoards();
}

function updateBoardCount() {
  const boards = Array.from(document.getElementsByClassName("board-card"));
  const visible = boards.filter(board => !board.classList.contains("hidden"));
  const shields = boards.filter(board => board.classList.contains("shield"));
  const visibleShields = visible.filter(board => board.classList.contains("shield"));

  document.getElementById("nb-matches").textContent =
    `Showing ${visible.length - visibleShields.length} of ${boards.length - shields.length} boards,`
    + ` ${visibleShields.length} of ${shields.length} shields`;
}

function wildcardMatch(pattern, str) {
  // Convert wildcard pattern to regex
  // Escape special regex characters except *
  const regexPattern = pattern
    .replace(/[.+?^${}()|[\]\\]/g, '\\$&')
    .replace(/\*/g, '.*');
  const regex = new RegExp(`^${regexPattern}$`, "i");
  return regex.test(str);
}

function formatMemoryKiB(kib) {
  if (kib <= 0) return "0";
  if (kib >= 1024 && kib % 1024 === 0) return `${kib / 1024} MiB`;
  return `${kib} KiB`;
}

function niceMemoryMax(kib) {
  if (kib <= 0) return 64;
  let n = 1;
  while (n < kib) n <<= 1;
  return n;
}

function updateMemorySlider(type) {
  const minEl = document.getElementById(`${type}-min`);
  const maxEl = document.getElementById(`${type}-max`);
  const fill = document.getElementById(`${type}-range-fill`);
  const label = document.getElementById(`${type}-range-label`);
  const maxEndpoint = document.getElementById(`${type}-max-endpoint`);

  if (!minEl || !maxEl || !fill || !label) return;

  const minVal = Number.parseInt(minEl.value, 10);
  const maxVal = Number.parseInt(maxEl.value, 10);
  const sliderMax = Number.parseInt(minEl.max, 10);

  const pctMin = (minVal / sliderMax) * 100;
  const pctMax = (maxVal / sliderMax) * 100;

  fill.style.left = `${pctMin}%`;
  fill.style.width = `${pctMax - pctMin}%`;

  if (maxEndpoint) maxEndpoint.textContent = formatMemoryKiB(sliderMax);

  const atMin = minVal <= 0;
  const atMax = maxVal >= sliderMax;

  if (atMin && atMax) {
    label.textContent = "Any";
  } else if (atMin) {
    label.textContent = `≤ ${formatMemoryKiB(maxVal)}`;
  } else if (atMax) {
    label.textContent = `≥ ${formatMemoryKiB(minVal)}`;
  } else {
    label.textContent = `${formatMemoryKiB(minVal)} – ${formatMemoryKiB(maxVal)}`;
  }
}

function onMemorySlider(type) {
  const minEl = document.getElementById(`${type}-min`);
  const maxEl = document.getElementById(`${type}-max`);
  if (!minEl || !maxEl) return;

  /* Keep the two thumbs from crossing each other */
  let minVal = Number.parseInt(minEl.value, 10);
  let maxVal = Number.parseInt(maxEl.value, 10);
  if (minVal > maxVal) {
    if (document.activeElement === minEl) {
      minEl.value = maxVal;
    } else {
      maxEl.value = minVal;
    }
  }

  updateMemorySlider(type);
  filterBoards();
}

function initMemorySliders() {
  const boards = Array.from(document.querySelectorAll(".board-card"));

  ["ram", "rom"].forEach(type => {
    const values = boards
      .map(b => Number.parseInt(b.getAttribute(`data-${type}`) || "", 10))
      .filter(v => !Number.isNaN(v) && v > 0);

    if (!values.length) return;

    const maxBytes = Math.max(...values);
    const maxKiB = niceMemoryMax(Math.ceil(maxBytes / 1024));

    const minEl = document.getElementById(`${type}-min`);
    const maxEl = document.getElementById(`${type}-max`);
    if (!minEl || !maxEl) return;

    minEl.max = maxKiB;
    maxEl.max = maxKiB;
    minEl.value = 0;
    maxEl.value = maxKiB;

    updateMemorySlider(type);
  });
}

function memorySliderRange(type) {
  /* Returns {minBytes, maxBytes} from the slider pair for 'type' ("ram" or "rom").
   * maxBytes is Infinity when the max slider sits at its maximum position. */
  const KIB = 1024;
  const minEl = document.getElementById(`${type}-min`);
  const maxEl = document.getElementById(`${type}-max`);
  const minKiB = minEl ? Number.parseInt(minEl.value, 10) : 0;
  const maxKiB = maxEl ? Number.parseInt(maxEl.value, 10) : Infinity;
  const maxPossible = maxEl ? Number.parseInt(maxEl.max, 10) : Infinity;
  return {
    minBytes: minKiB * KIB,
    maxBytes: (maxKiB >= maxPossible) ? Infinity : maxKiB * KIB,
  };
}

function memoryInRange(boardBytes, minBytes, maxBytes, isShield) {
  /* Returns false when the board should be excluded by a memory range filter.
   * Shields are never filtered by memory because they have no data-ram/data-rom. */
  if (isShield) return true;
  if (Number.isNaN(boardBytes)) return (minBytes <= 0 && maxBytes >= Infinity);
  return boardBytes >= minBytes && (maxBytes >= Infinity || boardBytes <= maxBytes);
}

function filterBoards() {
  const nameInput = document.getElementById("name").value.toLowerCase();
  const archSelect = document.getElementById("arch").value;

  const { minBytes: ramMinBytes, maxBytes: ramMaxBytes } = memorySliderRange("ram");
  const { minBytes: romMinBytes, maxBytes: romMaxBytes } = memorySliderRange("rom");

  const vendorSelect = document.getElementById("vendor").value;
  const socSocSelect = document.getElementById("soc");
  const showBoards = document.getElementById("show-boards").checked;
  const showShields = document.getElementById("show-shields").checked;

  // Get selected hardware capability tags
  const selectedHWTags = [...document.querySelectorAll('#hwcaps-tags .tag')].map(tag => tag.textContent);

  // Get selected compatible tags
  const selectedCompatibles = [...document.querySelectorAll('#compatibles-tags .tag')].map(tag => tag.textContent);

  const resetFiltersBtn = document.getElementById("reset-filters");
  const ramFiltered = ramMinBytes > 0 || ramMaxBytes < Infinity;
  const romFiltered = romMinBytes > 0 || romMaxBytes < Infinity;
  if (nameInput || archSelect || ramFiltered || romFiltered || vendorSelect || socSocSelect.selectedOptions.length || selectedHWTags.length || selectedCompatibles.length || !showBoards || !showShields) {
    resetFiltersBtn.classList.remove("btn-disabled");
  } else {
    resetFiltersBtn.classList.add("btn-disabled");
  }

  const boards = document.getElementsByClassName("board-card");

  Array.from(boards).forEach(function (board) {
    const boardName = board.getAttribute("data-name").toLowerCase();
    const boardArchs = (board.getAttribute("data-arch") || "").split(" ").filter(Boolean);
    const boardRam = Number.parseInt(board.getAttribute("data-ram") || "", 10);
    const boardRom = Number.parseInt(board.getAttribute("data-rom") || "", 10);
    const boardVendor = board.getAttribute("data-vendor") || "";
    const boardSocs = (board.getAttribute("data-socs") || "").split(" ").filter(Boolean);
    const boardSupportedFeatures = (board.getAttribute("data-supported-features") || "").split(" ").filter(Boolean);
    const boardCompatibles = (board.getAttribute("data-compatibles") || "").split(" ").filter(Boolean);
    const isShield = board.classList.contains("shield");

    let matches = true;

    const selectedSocs = [...socSocSelect.selectedOptions].map(({ value }) => value);

    if ((isShield && !showShields) || (!isShield && !showBoards)) {
      matches = false;
    } else {
      // Check if board matches all selected compatibles (with wildcard support)
      const compatiblesMatch = selectedCompatibles.length === 0 ||
        selectedCompatibles.every((pattern) =>
          boardCompatibles.some((compatible) => wildcardMatch(pattern, compatible))
        );

      matches =
        !(nameInput && !boardName.includes(nameInput)) &&
        !(archSelect && !boardArchs.includes(archSelect)) &&
        memoryInRange(boardRam, ramMinBytes, ramMaxBytes, isShield) &&
        memoryInRange(boardRom, romMinBytes, romMaxBytes, isShield) &&
        !(vendorSelect && boardVendor !== vendorSelect) &&
        (selectedSocs.length === 0 || selectedSocs.some((soc) => boardSocs.includes(soc))) &&
        (selectedHWTags.length === 0 || selectedHWTags.every((tag) => boardSupportedFeatures.includes(tag))) &&
        compatiblesMatch;
    }

    board.classList.toggle("hidden", !matches);
  });

  updateURL();
  updateBoardCount();
}
