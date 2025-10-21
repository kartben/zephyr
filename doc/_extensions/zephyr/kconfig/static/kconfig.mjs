/*
 * Copyright (c) 2022 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

const DB_FILE = 'kconfig.json';
const RESULTS_PER_PAGE_OPTIONS = [10, 25, 50];
let zephyr_gh_base_url;
let zephyr_version;

/* search state */
let db;
let searchOffset;
let maxResults = RESULTS_PER_PAGE_OPTIONS[0];

/* elements */
let input;
let searchTools;
let summaryText;
let results;
let navigation;
let navigationPagesText;
let navigationPrev;
let navigationNext;

/**
 * Show an error message.
 * @param {String} message Error message.
 */
function showError(message) {
    const admonition = document.createElement('div');
    admonition.className = 'admonition error';
    results.replaceChildren(admonition);

    const admonitionTitle = document.createElement('p');
    admonitionTitle.className = 'admonition-title';
    admonition.appendChild(admonitionTitle);

    const admonitionTitleText = document.createTextNode('Error');
    admonitionTitle.appendChild(admonitionTitleText);

    const admonitionContent = document.createElement('p');
    admonition.appendChild(admonitionContent);

    const admonitionContentText = document.createTextNode(message);
    admonitionContent.appendChild(admonitionContentText);
}

/**
 * Show a progress message.
 * @param {String} message Progress message.
 */
function showProgress(message) {
    const p = document.createElement('p');
    p.className = 'centered';
    results.replaceChildren(p);

    const pText = document.createTextNode(message);
    p.appendChild(pText);
}

/**
 * Generate a GitHub link for a given file path in the Zephyr repository.
 * @param {string} path - The file path in the repository.
 * @param {number} [line] - Optional line number to link to.
 * @param {string} [mode=blob] - The mode (blob or edit). Defaults to 'blob'.
 * @param {string} [revision=main] - The branch, tag, or commit hash. Defaults to 'main'.
 * @returns {string} - The generated GitHub URL.
 */
function getGithubLink(path, line, mode = "blob", revision = "main") {
    if (!zephyr_gh_base_url) {
        return;
    }

    let url = [
        zephyr_gh_base_url,
        mode,
        revision,
        path
    ].join("/");

    if (line !== undefined){
        url +=  `#L${line}`;
    }

    return url;
}


/**
 * Render a Kconfig literal property.
 * @param {Element} parent Parent element.
 * @param {String} title Title.
 * @param {Element} contentElement Content Element.
 */
function renderKconfigPropLiteralElement(parent, title, contentElement)
{
    const term = document.createElement('dt');
    parent.appendChild(term);

    const termText = document.createTextNode(title);
    term.appendChild(termText);

    const details = document.createElement('dd');
    parent.appendChild(details);

    const code = document.createElement('code');
    code.className = 'docutils literal';
    details.appendChild(code);

    const literal = document.createElement('span');
    literal.className = 'pre';
    code.appendChild(literal);

    literal.appendChild(contentElement);
}

/**
 * Render a Kconfig literal property.
 * @param {Element} parent Parent element.
 * @param {String} title Title.
 * @param {String} content Content.
 */
function renderKconfigPropLiteral(parent, title, content) {
    const contentElement = document.createTextNode(content);
    renderKconfigPropLiteralElement(parent, title, contentElement);
}

/**
 * Render a Kconfig list property.
 * @param {Element} parent Parent element.
 * @param {String} title Title.
 * @param {list} elements List of elements.
 * @param {boolean} linkElements Whether to link elements (treat each element
 *                  as an unformatted option)
 */
function renderKconfigPropList(parent, title, elements, linkElements) {
    if (elements.length === 0) {
        return;
    }

    const term = document.createElement('dt');
    parent.appendChild(term);

    const termText = document.createTextNode(title);
    term.appendChild(termText);

    const details = document.createElement('dd');
    parent.appendChild(details);

    const list = document.createElement('ul');
    list.className = 'simple';
    details.appendChild(list);

    elements.forEach(element => {
        const listItem = document.createElement('li');
        list.appendChild(listItem);

        if (linkElements) {
            const link = document.createElement('a');
            link.href = '#' + element;
            listItem.appendChild(link);

            const linkText = document.createTextNode(element);
            link.appendChild(linkText);
        } else {
            /* using HTML since element content is pre-formatted */
            listItem.innerHTML = element;
        }
    });
}

/**
 * Render a Kconfig list property.
 * @param {Element} parent Parent element.
 * @param {list} elements List of elements.
 * @returns
 */
function renderKconfigDefaults(parent, defaults, alt_defaults) {
    if (defaults.length === 0 && alt_defaults.length === 0) {
        return;
    }

    const term = document.createElement('dt');
    parent.appendChild(term);

    const termText = document.createTextNode('Defaults');
    term.appendChild(termText);

    const details = document.createElement('dd');
    parent.appendChild(details);

    if (defaults.length > 0) {
        const list = document.createElement('ul');
        list.className = 'simple';
        details.appendChild(list);

        defaults.forEach(entry => {
            const listItem = document.createElement('li');
            list.appendChild(listItem);

            /* using HTML since default content may be pre-formatted */
            listItem.innerHTML = entry;
        });
    }

    if (alt_defaults.length > 0) {
        const list = document.createElement('ul');
        list.className = 'simple';
        list.style.display = 'none';
        details.appendChild(list);

        alt_defaults.forEach(entry => {
            const listItem = document.createElement('li');
            list.appendChild(listItem);

            /* using HTML since default content may be pre-formatted */
            listItem.innerHTML = `
                ${entry[0]}
                <em>at</em>
                <code class="docutils literal">
                    <span class"pre">${entry[1]}</span>
                </code>`;
        });

        const show = document.createElement('a');
        show.onclick = () => {
            if (list.style.display === 'none') {
                list.style.display = 'block';
            } else {
                list.style.display = 'none';
            }
        };
        details.appendChild(show);

        const showText = document.createTextNode('Show/Hide other defaults');
        show.appendChild(showText);
    }
}

/**
 * Highlight text by wrapping matches in <mark> tags.
 * @param {String} text Text to highlight.
 * @param {Array} indices Array of [start, end] indices to highlight.
 * @returns {Element} Document fragment with highlighted text.
 */
function highlightText(text, indices) {
    const fragment = document.createDocumentFragment();

    if (!indices || indices.length === 0) {
        fragment.appendChild(document.createTextNode(text));
        return fragment;
    }

    /* sort and merge overlapping indices */
    const sorted = [...indices].sort((a, b) => a[0] - b[0]);
    const merged = [];
    sorted.forEach(([start, end]) => {
        if (merged.length === 0 || merged[merged.length - 1][1] < start) {
            merged.push([start, end]);
        } else {
            merged[merged.length - 1][1] = Math.max(merged[merged.length - 1][1], end);
        }
    });

    /* build fragment with highlighted text */
    let lastIndex = 0;
    merged.forEach(([start, end]) => {
        if (lastIndex < start) {
            fragment.appendChild(document.createTextNode(text.substring(lastIndex, start)));
        }
        const mark = document.createElement('mark');
        mark.appendChild(document.createTextNode(text.substring(start, end)));
        fragment.appendChild(mark);
        lastIndex = end;
    });

    if (lastIndex < text.length) {
        fragment.appendChild(document.createTextNode(text.substring(lastIndex)));
    }

    return fragment;
}

/**
 * Render a Kconfig entry.
 * @param {Object} entry Kconfig entry.
 * @param {Object} highlightIndices Map with keys corresponding to properties of the Kconfig entry
          (e.g. name, prompt, ...) and values containing arrays of [start, end] indices to
          highlight.
 */
function renderKconfigEntry(entry, highlightIndices) {
    const container = document.createElement('dl');
    container.className = 'kconfig';

    /* title (name and permalink) */
    const title = document.createElement('dt');
    title.className = 'sig sig-object';
    container.appendChild(title);

    const name = document.createElement('span');
    name.className = 'pre';
    title.appendChild(name);

    const highlightedName = highlightText(entry.name, highlightIndices.name);
    name.appendChild(highlightedName);

    const permalink = document.createElement('a');
    permalink.className = 'headerlink';
    permalink.href = '#' + entry.name;
    title.appendChild(permalink);

    const permalinkText = document.createTextNode('\uf0c1');
    permalink.appendChild(permalinkText);

    /* details */
    const details = document.createElement('dd');
    container.append(details);

    /* prompt and help */
    const prompt = document.createElement('p');
    details.appendChild(prompt);

    const promptTitle = document.createElement('em');
    prompt.appendChild(promptTitle);

    if (entry.prompt) {
        const highlightedPrompt = highlightText(entry.prompt, highlightIndices.prompt);
        promptTitle.appendChild(highlightedPrompt);
    } else {
        const promptTitleText = document.createTextNode('No prompt - not directly user assignable.');
        promptTitle.appendChild(promptTitleText);
    }

    if (entry.help) {
        const help = document.createElement('p');
        details.appendChild(help);

        const helpText = document.createTextNode(entry.help);
        help.appendChild(helpText);
    }

    /* symbol properties (defaults, selects, etc.) */
    const props = document.createElement('dl');
    props.className = 'field-list simple';
    details.appendChild(props);

    renderKconfigPropLiteral(props, 'Type', entry.type);
    if (entry.dependencies) {
        renderKconfigPropList(props, 'Dependencies', [entry.dependencies]);
    }
    renderKconfigDefaults(props, entry.defaults, entry.alt_defaults);
    renderKconfigPropList(props, 'Selects', entry.selects, false);
    renderKconfigPropList(props, 'Selected by', entry.selected_by, true);
    renderKconfigPropList(props, 'Implies', entry.implies, false);
    renderKconfigPropList(props, 'Implied by', entry.implied_by, true);
    renderKconfigPropList(props, 'Ranges', entry.ranges, false);
    renderKconfigPropList(props, 'Choices', entry.choices, false);

    /* symbol location with permalink */
    const locationElement = document.createTextNode(`${entry.filename}:${entry.linenr}`);
    locationElement.class = "pre";

    let locationPermalink = getGithubLink(entry.filename, entry.linenr, "blob", zephyr_version);
    if (locationPermalink) {
        const locationPermalinkElement = document.createElement('a');
        locationPermalinkElement.href = locationPermalink;
        locationPermalinkElement.appendChild(locationElement);
        renderKconfigPropLiteralElement(props, 'Location', locationPermalinkElement);
    } else {
        renderKconfigPropLiteralElement(props, 'Location', locationElement);
    }

    renderKconfigPropLiteral(props, 'Menu path', entry.menupath);

    return container;
}

/**
 * Calculate field-length norm for a given field.
 * Shorter fields get higher scores (more relevant).
 * @param {number} fieldLength Length of the field.
 * @returns {number} Field-length norm score (0-1, higher is better).
 */
function calculateFieldNorm(fieldLength) {
    /* use inverse square root to favor shorter fields */
    return 1 / Math.sqrt(fieldLength || 1);
}

/**
 * Calculate match score for a single field.
 * @param {string} field Field text to search in.
 * @param {RegExp[]} regexes Array of regex patterns to match.
 * @param {number} keyWeight Weight of this field (higher = more important).
 * @param {Array} highlightIndices Array to populate with match positions.
 * @returns {number} Match score for this field.
 */
function calculateFieldScore(field, regexes, keyWeight, highlightIndices) {
    if (!field) {
        return 0;
    }

    let totalScore = 0;
    let matchedRegexCount = 0;

    regexes.forEach(regex => {
        const regexGlobal = new RegExp(regex.source, regex.flags + 'g');
        let bestMatchScore = 0;
        let match;

        while ((match = regexGlobal.exec(field)) !== null) {
            highlightIndices.push([match.index, match.index + match[0].length]);

            /* calculate match quality based on position and completeness */
            const matchLength = match[0].length;
            const matchPosition = match.index;

            /* exact match bonus */
            const exactBonus = (match[0].toLowerCase() === field.toLowerCase()) ? 0.5 : 0;

            /* start position bonus (earlier matches score higher) */
            const positionScore = 1 - (matchPosition / field.length);

            /* match length relative to search term */
            const lengthScore = matchLength / regex.source.length;

            /* combine scores */
            const matchScore = (positionScore * 0.3 + lengthScore * 0.7 + exactBonus);

            bestMatchScore = Math.max(bestMatchScore, matchScore);

            if (match.index === regexGlobal.lastIndex) {
                regexGlobal.lastIndex++;
            }
        }

        if (bestMatchScore > 0) {
            matchedRegexCount++;
            totalScore += bestMatchScore;
        }
    });

    /* if not all regexes matched, return 0 */
    if (matchedRegexCount < regexes.length) {
        return 0;
    }

    /* apply field-length norm */
    const fieldNorm = calculateFieldNorm(field.length);

    /* apply key weight and field norm */
    return (totalScore / regexes.length) * keyWeight * fieldNorm;
}

/** Perform a search and display the results. */
function doSearch() {
    /* replace current state (to handle back button) */
    history.replaceState({
        value: input.value,
        searchOffset: searchOffset
    }, '', window.location);

    /* nothing to search for */
    if (!input.value) {
        results.replaceChildren();
        navigation.style.visibility = 'hidden';
        searchTools.style.visibility = 'hidden';
        return;
    }

    /* perform search */
    const regexes = input.value.trim().split(/\s+/).map(
        element => new RegExp(element, 'i')
    );

    const scoredResults = [];

    db.forEach(entry => {
        const name = entry.name;
        const prompt = entry.prompt || "";
        const highlightIndices = {
            name: [],
            prompt: []
        };

        const NAME_WEIGHT = 2.0;
        const PROMPT_WEIGHT = 1.0;

        const nameScore = calculateFieldScore(name, regexes, NAME_WEIGHT, highlightIndices.name);
        const promptScore = calculateFieldScore(prompt, regexes, PROMPT_WEIGHT, highlightIndices.prompt);

        const totalScore = nameScore + promptScore;
        if (totalScore > 0) {
            scoredResults.push({
                entry,
                highlightIndices,
                score: totalScore
            });
        }
    });

    scoredResults.sort((a, b) => b.score - a.score);
    
    const count = scoredResults.length;
    const searchResults = scoredResults.slice(searchOffset, searchOffset + maxResults);

    /* show results count and search tools */
    summaryText.nodeValue = `${count} options match your search.`;
    searchTools.style.visibility = 'visible';

    /* update navigation */
    navigation.style.visibility = 'visible';
    navigationPrev.disabled = searchOffset - maxResults < 0;
    navigationNext.disabled = searchOffset + maxResults > count;

    const currentPage = Math.floor(searchOffset / maxResults) + 1;
    const totalPages = Math.floor(count / maxResults) + 1;
    navigationPagesText.nodeValue = `Page ${currentPage} of ${totalPages}`;

    /* render Kconfig entries */
    results.replaceChildren();
    searchResults.forEach(result => {
        results.appendChild(renderKconfigEntry(result.entry, result.highlightIndices));
    });
}

/** Do a search from URL hash */
function doSearchFromURL() {
    const rawOption = window.location.hash.substring(1);
    if (!rawOption) {
        return;
    }

    const option = decodeURIComponent(rawOption);
    if (option.startsWith('!')) {
        input.value = option.substring(1);
    } else {
        input.value = '^' + option + '$';
    }

    searchOffset = 0;
    doSearch();
}

function setupKconfigSearch() {
    /* populate kconfig-search container */
    const container = document.getElementById('__kconfig-search');
    if (!container) {
        console.error("Couldn't find Kconfig search container");
        return;
    }

    /* create input field */
    const inputContainer = document.createElement('div');
    inputContainer.className = 'input-container'
    container.appendChild(inputContainer)

    input = document.createElement('input');
    input.placeholder = 'Type a Kconfig option name (RegEx allowed)';
    input.type = 'text';
    inputContainer.appendChild(input);

    const copyLinkButton = document.createElement('button');
    copyLinkButton.title = "Copy link to results";
    copyLinkButton.onclick = () => {
        if (!window.isSecureContext) {
            console.error("Cannot copy outside of a secure context");
            return;
        }

        const copyURL = window.location.protocol + '//' + window.location.host +
        window.location.pathname + '#!' + input.value;

        navigator.clipboard.writeText(encodeURI(copyURL));
    }
    inputContainer.appendChild(copyLinkButton)

    const copyLinkText = document.createTextNode('🔗');
    copyLinkButton.appendChild(copyLinkText);

    /* create search tools container */
    searchTools = document.createElement('div');
    searchTools.className = 'search-tools';
    searchTools.style.visibility = 'hidden';
    container.appendChild(searchTools);

    /* create search summary */
    const searchSummaryContainer = document.createElement('div');
    searchTools.appendChild(searchSummaryContainer);

    const searchSummary = document.createElement('p');
    searchSummaryContainer.appendChild(searchSummary);

    summaryText = document.createTextNode('');
    searchSummary.appendChild(summaryText);

    /* create results per page selector */
    const resultsPerPageContainer = document.createElement('div');
    resultsPerPageContainer.className = 'results-per-page-container';
    searchTools.appendChild(resultsPerPageContainer);

    const resultsPerPageTitle = document.createElement('span');
    resultsPerPageTitle.className = 'results-per-page-title';
    resultsPerPageContainer.appendChild(resultsPerPageTitle);

    const resultsPerPageTitleText = document.createTextNode('Results per page:');
    resultsPerPageTitle.appendChild(resultsPerPageTitleText);

    const resultsPerPageSelect = document.createElement('select');
    resultsPerPageSelect.onchange = (event) => {
        maxResults = parseInt(event.target.value);
        searchOffset = 0;
        doSearch();
    }
    resultsPerPageContainer.appendChild(resultsPerPageSelect);

    RESULTS_PER_PAGE_OPTIONS.forEach((value, index) => {
        const option = document.createElement('option');
        option.value = value;
        option.text = value;
        option.selected = index === 0;
        resultsPerPageSelect.appendChild(option);
    });

    /* create search results container */
    results = document.createElement('div');
    container.appendChild(results);

    /* create search navigation */
    navigation = document.createElement('div');
    navigation.className = 'search-nav';
    navigation.style.visibility = 'hidden';
    container.appendChild(navigation);

    navigationPrev = document.createElement('button');
    navigationPrev.className = 'btn';
    navigationPrev.disabled = true;
    navigationPrev.onclick = () => {
        searchOffset -= maxResults;
        doSearch();
        window.scroll(0, 0);
    }
    navigation.appendChild(navigationPrev);

    const navigationPrevText = document.createTextNode('Previous');
    navigationPrev.appendChild(navigationPrevText);

    const navigationPages = document.createElement('p');
    navigation.appendChild(navigationPages);

    navigationPagesText = document.createTextNode('');
    navigationPages.appendChild(navigationPagesText);

    navigationNext = document.createElement('button');
    navigationNext.className = 'btn';
    navigationNext.disabled = true;
    navigationNext.onclick = () => {
        searchOffset += maxResults;
        doSearch();
        window.scroll(0, 0);
    }
    navigation.appendChild(navigationNext);

    const navigationNextText = document.createTextNode('Next');
    navigationNext.appendChild(navigationNextText);

    /* load database */
    showProgress('Loading database...');

    fetch(DB_FILE)
        .then(response => response.json())
        .then(json => {
            db = json["symbols"];
            zephyr_gh_base_url = json["gh_base_url"];
            zephyr_version = json["zephyr_version"];

            results.replaceChildren();

            /* perform initial search */
            doSearchFromURL();

            /* install event listeners */
            input.addEventListener('input', () => {
                searchOffset = 0;
                doSearch();
            });

            /* install hash change listener (for links) */
            window.addEventListener('hashchange', doSearchFromURL);

            /* handle back/forward navigation */
            window.addEventListener('popstate', (event) => {
                if (!event.state) {
                    return;
                }

                input.value = event.state.value;
                searchOffset = event.state.searchOffset;
                doSearch();
            });
        })
        .catch(error => {
            showError(`Kconfig database could not be loaded (${error})`);
        });
}

setupKconfigSearch();
