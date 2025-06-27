#!/usr/bin/env python3

import os
import json
from pathlib import Path
from get_maintainer import Maintainers

def get_repo_structure(start_path='.'):
    """Generate a tree structure of the repository."""
    structure = {}

    for root, dirs, files in os.walk(start_path):
        # Skip .git directory and other hidden directories
        dirs[:] = [d for d in dirs if not d.startswith('.')]

        rel_path = os.path.relpath(root, start_path)
        if rel_path == '.':
            rel_path = ''

        current_dict = structure
        if rel_path:
            for part in rel_path.split(os.sep):
                current_dict = current_dict[part]

        # Add directories
        for d in dirs:
            current_dict[d] = {}

        # Add files
        for f in files:
            current_dict[f] = None

    return structure

def get_maintainer_info(path, maintainers):
    """Get maintainer information for a path."""
    areas = maintainers.path2areas(path)
    if not areas:
        return {
            'status': 'unmaintained',
            'maintainers': [],
            'collaborators': [],
            'labels': []
        }

    # Combine information from all matching areas
    status = 'unmaintained'
    all_maintainers = set()
    all_collaborators = set()
    all_labels = set()

    # Determine status based on precedence: maintained > odd fixes > unmaintained
    if any(area.status == 'maintained' for area in areas):
        status = 'maintained'
    elif any(area.status == 'odd fixes' for area in areas):
        status = 'odd fixes'
    # If neither 'maintained' nor 'odd fixes' are found, status remains 'unmaintained'

    for area in areas:
        # Collect all maintainers, collaborators, and labels regardless of the final status
        all_maintainers.update(area.maintainers)
        all_collaborators.update(area.collaborators)
        all_labels.update(area.labels)

    return {
        'status': status,
        'maintainers': list(all_maintainers),
        'collaborators': list(all_collaborators),
        'labels': list(all_labels)
    }

def process_structure(structure, maintainers, current_path=''):
    """Process the structure and add maintainer information."""
    result = {}

    for name, content in structure.items():
        path = os.path.join(current_path, name)
        if content is None:  # It's a file
            result[name] = {
                'type': 'file',
                'maintainer_info': get_maintainer_info(path, maintainers)
            }
        else:  # It's a directory
            # Process contents first
            processed_contents = process_structure(content, maintainers, path)

            # Get direct maintainer info for the directory
            dir_maintainer_info = get_maintainer_info(path, maintainers)

            # If directory isn't directly maintained, analyze contents
            if dir_maintainer_info['status'] == 'unmaintained':
                has_contents = False
                all_maintained = True
                all_odd_fixes = True
                all_maintainers = set()
                all_collaborators = set()
                all_labels = set()

                # Recursively check contents status
                def check_contents_status(contents):
                    nonlocal has_contents, all_maintained, all_odd_fixes
                    nonlocal all_maintainers, all_collaborators, all_labels

                    for item in contents.values():
                        has_contents = True
                        status = item['maintainer_info']['status']

                        # Update status flags
                        if status == 'unmaintained':
                            all_maintained = False
                            all_odd_fixes = False
                        elif status == 'odd fixes':
                            all_maintained = False

                        # Collect maintainers from contents
                        all_maintainers.update(item['maintainer_info'].get('maintainers', []))
                        all_collaborators.update(item['maintainer_info'].get('collaborators', []))
                        all_labels.update(item['maintainer_info'].get('labels', []))

                        # If it's a directory, check its contents too
                        if item['type'] == 'directory' and 'contents' in item:
                            check_contents_status(item['contents'])

                check_contents_status(processed_contents)

                # Update directory status based on contents
                if has_contents:
                    if all_maintained:
                        dir_maintainer_info['status'] = 'maintained'
                    elif all_odd_fixes:
                        dir_maintainer_info['status'] = 'odd fixes'

                    if dir_maintainer_info['status'] != 'unmaintained':
                        # Include aggregated maintainers from subdirectories
                        dir_maintainer_info['maintainers'] = list(all_maintainers)
                        dir_maintainer_info['collaborators'] = list(all_collaborators)
                        dir_maintainer_info['labels'] = list(all_labels)
                        dir_maintainer_info['inherited'] = True
                        dir_maintainer_info['inherited_from'] = 'subdirectories'

            result[name] = {
                'type': 'directory',
                'maintainer_info': dir_maintainer_info,
                'contents': processed_contents
            }

    return result

def generate_html(data):
    """Generate the HTML file with the interactive visualization."""
    # Add metadata with generation timestamp
    from datetime import datetime

    data_with_metadata = {'generated_at': datetime.now().isoformat(), 'data': data}

    # Write data to JSON file
    with open('maintainers_data.json', 'w') as f:
        json.dump(data_with_metadata, f)

    # Rename the HTML file to index.html for GitHub Pages
    html = '''
    <!DOCTYPE html>
    <html>
    <head>
        <title>Zephyr Repository Maintainership</title>
        <meta charset="utf-8">
        <meta name="viewport" content="width=device-width, initial-scale=1">
        <link href="https://cdn.jsdelivr.net/npm/bootstrap@5.1.3/dist/css/bootstrap.min.css" rel="stylesheet">
        <style>
            body {
                background-color: #f8f9fa;
                font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, "Helvetica Neue", Arial, sans-serif;
            }
            .tree-view {
                font-family: 'Monaco', 'Menlo', 'Ubuntu Mono', 'Consolas', monospace;
                font-size: 14px;
                line-height: 1.6;
                padding: 20px;
                background: white;
                border-radius: 8px;
                box-shadow: 0 2px 4px rgba(0,0,0,0.05);
                width: 100%;
                height: 80vh;
                overflow: auto;
                contain: layout style paint;
            }
            .tree-container {
                position: relative;
                will-change: transform;
            }
            .directory {
                color: #2c5282;
                font-weight: 600;
            }
            .file {
                color: #2d3748;
            }
            .maintained {
                background-color: rgba(72, 187, 120, 0.2);
                border-radius: 4px;
                padding: 2px 6px;
                margin: 1px 0;
            }
            .maintained.inherited {
                background-color: rgba(72, 187, 120, 0.1);
                border: 1px dashed rgba(72, 187, 120, 0.4);
            }
            .odd-fixes {
                background-color: rgba(236, 201, 75, 0.2);
                border-radius: 4px;
                padding: 2px 6px;
                margin: 1px 0;
            }
            .unmaintained {
                background-color: rgba(245, 101, 101, 0.2);
                border-radius: 4px;
                padding: 2px 6px;
                margin: 1px 0;
            }
            .tooltip {
                position: absolute;
                display: none;
                background: white;
                border: 1px solid #e2e8f0;
                padding: 12px;
                border-radius: 6px;
                box-shadow: 0 4px 6px rgba(0,0,0,0.1);
                z-index: 1000;
                max-width: 350px;
                font-size: 13px;
                line-height: 1.5;
                pointer-events: none;
                opacity: 0;
                transition: opacity 0.1s ease-in-out;
                will-change: opacity, transform;
            }
            .tooltip.visible {
                opacity: 1;
            }
            .item {
                position: relative;
                padding: 2px 0;
                contain: layout style;
            }
            .item.directory {
                cursor: pointer;
            }
            .item.directory:hover {
                background-color: rgba(0, 0, 0, 0.05);
                border-radius: 4px;
            }
            .item.file {
                cursor: default;
            }
            .item-wrapper {
                display: flex;
                align-items: center;
                min-width: 0;
            }
            .toggle {
                flex: 0 0 20px;
                display: inline-flex;
                align-items: center;
                justify-content: center;
                cursor: pointer;
                user-select: none;
                color: #4a5568;
            }
            .item-content {
                flex: 1;
                min-width: 0;
                display: flex;
                align-items: center;
                justify-content: space-between;
            }
            .item-name {
                overflow: hidden;
                text-overflow: ellipsis;
                white-space: nowrap;
                min-width: 0;
                flex: 1;
            }
            .status-summary {
                margin-left: 12px;
                font-size: 12px;
                color: #718096;
                white-space: nowrap;
            }
            .status-count {
                display: inline-block;
                padding: 1px 6px;
                border-radius: 3px;
                margin-left: 4px;
            }
            .status-count.maintained {
                background-color: rgba(72, 187, 120, 0.2);
                color: #2f855a;
            }
            .status-count.odd-fixes {
                background-color: rgba(236, 201, 75, 0.2);
                color: #b7791f;
            }
            .status-count.unmaintained {
                background-color: rgba(245, 101, 101, 0.2);
                color: #c53030;
            }
            .loading {
                color: #718096;
                font-style: italic;
            }
            .card {
                border: none;
                box-shadow: 0 2px 4px rgba(0,0,0,0.05);
            }
            .card-body {
                padding: 20px;
            }
            .legend-item {
                display: inline-block;
                padding: 8px 16px;
                border-radius: 4px;
                margin-right: 10px;
                font-size: 14px;
            }
            h1 {
                font-size: 24px;
                font-weight: 600;
                color: #2d3748;
                margin-bottom: 24px;
            }
            .tooltip strong {
                color: #4a5568;
                font-weight: 600;
            }
            .search-container {
                margin-bottom: 16px;
            }
            .search-input {
                width: 100%;
                padding: 8px 12px;
                border: 1px solid #d1d5db;
                border-radius: 6px;
                font-size: 14px;
            }
            .search-results {
                margin-top: 8px;
                font-size: 12px;
                color: #6b7280;
            }
            .search-match {
                background-color: rgba(59, 130, 246, 0.08);
                border-radius: 4px;
                position: relative;
            }
            .search-match::after {
                content: "";
                position: absolute;
                left: 0;
                top: 0;
                bottom: 0;
                width: 3px;
                background-color: #3b82f6;
                border-radius: 2px 0 0 2px;
            }
        </style>
    </head>
    <body>
        <div class="container-fluid py-4">
            <h1>Zephyr Repository Maintainership</h1>
            <div class="alert alert-info" role="alert">
                This visualization is automatically updated daily. Last update: <span id="last-update"></span>
            </div>
            <div class="row">
                <div class="col-md-8">
                    <div class="search-container">
                        <input type="text" class="search-input" placeholder="Search files and directories (min 2 chars)..." id="search-input">
                        <div class="search-results" id="search-results"></div>
                    </div>
                    <div id="tree-view" class="tree-view">
                        <div class="tree-container" id="tree-container"></div>
                    </div>
                </div>
                <div class="col-md-4">
                    <div class="card">
                        <div class="card-body">
                            <h5 class="card-title mb-3">Legend</h5>
                            <div class="mb-2">
                                <span class="maintained legend-item">Maintained</span>
                            </div>
                            <div class="mb-2">
                                <span class="maintained inherited legend-item">Maintained (inherited)</span>
                            </div>
                            <div class="mb-2">
                                <span class="odd-fixes legend-item">Odd Fixes</span>
                            </div>
                            <div class="mb-2">
                                <span class="unmaintained legend-item">Unmaintained</span>
                            </div>
                        </div>
                    </div>
                </div>
            </div>
        </div>

        <!-- Single reusable tooltip -->
        <div id="global-tooltip" class="tooltip"></div>

        <script>
            // Performance-optimized tree viewer
            class TreeViewer {
                                constructor() {
                    this.data = null;
                    this.renderedNodes = new Map();
                    this.statusCountCache = new Map();
                    this.tooltip = document.getElementById('global-tooltip');
                    this.container = document.getElementById('tree-container');
                    this.searchInput = document.getElementById('search-input');
                    this.searchResults = document.getElementById('search-results');
                    this.expandedNodes = new Set(['']); // Root is always expanded
                    this.searchMode = false;
                    this.searchMatches = new Set();
                    this.searchTimeout = null;

                    this.init();
                }

                init() {
                    // Single event delegation listener
                    this.container.addEventListener('click', this.handleClick.bind(this));
                    this.container.addEventListener('mouseenter', this.handleMouseEnter.bind(this), true);
                    this.container.addEventListener('mouseleave', this.handleMouseLeave.bind(this), true);

                    // Search functionality
                    this.searchInput.addEventListener('input', this.handleSearch.bind(this));

                    // Hide tooltip on scroll
                    document.getElementById('tree-view').addEventListener('scroll', () => {
                        this.hideTooltip();
                    });

                    // Load data
                    this.loadData();
                }

                async loadData() {
                    try {
                        const response = await fetch(new URL('maintainers_data.json', window.location.href).href);
                        const jsonData = await response.json();

                        // Extract metadata and data
                        if (jsonData.generated_at && jsonData.data) {
                            // New format with metadata
                            this.generatedAt = jsonData.generated_at;
                            this.data = jsonData.data;
                        } else {
                            // Legacy format without metadata
                            this.data = jsonData;
                            this.generatedAt = null;
                        }

                        // Update the last update timestamp
                        this.updateLastUpdateDisplay();
                        this.render();
                    } catch (error) {
                        console.error('Error loading data:', error);
                        this.container.innerHTML = '<div class="alert alert-danger">Error loading data. Please try again later.</div>';
                    }
                }

                updateLastUpdateDisplay() {
                    const lastUpdateElement = document.getElementById('last-update');
                    if (this.generatedAt) {
                        const date = new Date(this.generatedAt);
                        lastUpdateElement.textContent = date.toLocaleString();
                    } else {
                        lastUpdateElement.textContent = 'Unknown';
                    }
                }

                // Memoized status count calculation
                getStatusSummary(contents, path) {
                    if (this.statusCountCache.has(path)) {
                        return this.statusCountCache.get(path);
                    }

                    const counts = { maintained: 0, 'odd fixes': 0, unmaintained: 0 };

                    const countFiles = (items) => {
                        for (const [_, item] of Object.entries(items)) {
                            if (item.type === 'file') {
                                counts[item.maintainer_info.status]++;
                            } else if (item.type === 'directory' && item.contents) {
                                countFiles(item.contents);
                            }
                        }
                    };

                    countFiles(contents);
                    const total = counts.maintained + counts['odd fixes'] + counts.unmaintained;

                                         let summary = '';
                     if (total > 0) {
                         summary = `
                             <span class="status-summary">
                                 <span class="status-count maintained">${counts.maintained}</span>
                                 <span class="status-count odd-fixes">${counts['odd fixes']}</span>
                                 <span class="status-count unmaintained">${counts.unmaintained}</span>
                             </span>
                         `;
                     }

                    this.statusCountCache.set(path, summary);
                    return summary;
                }

                                handleClick(e) {
                    const item = e.target.closest('.item');
                    if (!item) return;

                    const path = item.dataset.path;
                    const info = this.getNodeInfo(path);

                    // Only handle clicks for directories
                    if (!info || info.type !== 'directory') return;

                    e.stopPropagation();
                    this.hideTooltip();

                    const toggle = item.querySelector('.toggle');

                    if (this.expandedNodes.has(path)) {
                        this.expandedNodes.delete(path);
                        toggle.textContent = '▶';
                    } else {
                        this.expandedNodes.add(path);
                        toggle.textContent = '▼';
                    }

                    this.render();
                }

                handleMouseEnter(e) {
                    const item = e.target.closest('.item');
                    if (!item) return;

                    const path = item.dataset.path;
                    const info = this.getNodeInfo(path);
                    if (!info) return;

                    this.showTooltip(e, info.maintainer_info);
                }

                handleMouseLeave(e) {
                    const item = e.target.closest('.item');
                    if (!item) return;

                    this.hideTooltip();
                }

                showTooltip(e, maintainerInfo) {
                    const rect = e.target.closest('.item').getBoundingClientRect();

                    let left = rect.right + 15;
                    let top = rect.top;

                    if (left + 350 > window.innerWidth - 20) {
                        left = rect.left - 350 - 15;
                    }

                    if (top + 200 > window.innerHeight - 20) {
                        top = window.innerHeight - 220;
                    }

                                         this.tooltip.innerHTML = `
                         <strong>Status:</strong> ${maintainerInfo.status}${maintainerInfo.inherited ? ' (inherited from ' + maintainerInfo.inherited_from + ')' : ''}<br>
                         <strong>Maintainers:</strong> ${maintainerInfo.maintainers.join(', ') || 'None'}${maintainerInfo.inherited ? ' (inherited)' : ''}<br>
                         <strong>Collaborators:</strong> ${maintainerInfo.collaborators.join(', ') || 'None'}${maintainerInfo.inherited ? ' (inherited)' : ''}<br>
                         <strong>Labels:</strong> ${maintainerInfo.labels.join(', ') || 'None'}${maintainerInfo.inherited ? ' (inherited)' : ''}
                     `;

                    this.tooltip.style.left = left + 'px';
                    this.tooltip.style.top = top + 'px';
                    this.tooltip.style.display = 'block';

                    requestAnimationFrame(() => {
                        this.tooltip.classList.add('visible');
                    });
                }

                hideTooltip() {
                    this.tooltip.classList.remove('visible');
                    setTimeout(() => {
                        if (!this.tooltip.classList.contains('visible')) {
                            this.tooltip.style.display = 'none';
                        }
                    }, 100);
                }

                                handleSearch(e) {
                    const query = e.target.value.toLowerCase().trim();

                    // Clear existing timeout
                    if (this.searchTimeout) {
                        clearTimeout(this.searchTimeout);
                    }

                    // Show loading indicator for searches longer than 1 character
                    if (query.length > 1) {
                        this.searchResults.textContent = 'Searching...';
                    }

                    // Debounce search - only execute after user stops typing for 300ms
                    this.searchTimeout = setTimeout(() => {
                        this.performSearch(query);
                    }, 300);
                }

                                performSearch(query) {
                    if (!query || query.length < 2) {
                        this.searchMode = false;
                        this.searchMatches.clear();
                        this.searchResults.textContent = '';
                        this.render();
                        return;
                    }

                    this.searchMode = true;
                    this.searchMatches.clear();

                    const MAX_RESULTS = 1000; // Limit results to prevent browser freeze
                    let resultCount = 0;

                    const searchRecursive = (data, path = '') => {
                        if (resultCount >= MAX_RESULTS) return; // Stop if we hit the limit

                        for (const [name, info] of Object.entries(data)) {
                            if (resultCount >= MAX_RESULTS) return;

                            const currentPath = path ? `${path}/${name}` : name;

                            if (name.toLowerCase().includes(query)) {
                                this.searchMatches.add(currentPath);
                                resultCount++;

                                // Ensure parent directories are expanded
                                let parentPath = '';
                                for (const segment of currentPath.split('/').slice(0, -1)) {
                                    parentPath = parentPath ? `${parentPath}/${segment}` : segment;
                                    this.expandedNodes.add(parentPath);
                                }
                            }

                            if (info.type === 'directory' && info.contents) {
                                searchRecursive(info.contents, currentPath);
                            }
                        }
                    };

                    try {
                        searchRecursive(this.data);
                        const matchText = this.searchMatches.size >= MAX_RESULTS
                            ? `${MAX_RESULTS}+ matches found (showing first ${MAX_RESULTS})`
                            : `${this.searchMatches.size} matches found`;
                        this.searchResults.textContent = matchText;
                        this.render();
                    } catch (error) {
                        console.error('Search error:', error);
                        this.searchResults.textContent = 'Search error occurred';
                    }
                }

                getNodeInfo(path) {
                    const parts = path.split('/').filter(p => p);
                    let current = this.data;

                    for (const part of parts) {
                        if (!current[part]) return null;
                        current = current[part];
                        if (current.type === 'directory') {
                            current = current.contents;
                        }
                    }

                    // Go back one level to get the actual node
                    if (parts.length > 0) {
                        current = this.data;
                        for (const part of parts.slice(0, -1)) {
                            current = current[part].contents;
                        }
                        return current[parts[parts.length - 1]];
                    }

                    return null;
                }

                render() {
                    const fragment = document.createDocumentFragment();
                    this.renderLevel(this.data, fragment, '', 0);

                    this.container.innerHTML = '';
                    this.container.appendChild(fragment);
                }

                renderLevel(data, parent, currentPath, level) {
                    const entries = Object.entries(data).sort((a, b) => {
                        const aIsDir = a[1].type === 'directory';
                        const bIsDir = b[1].type === 'directory';

                        if (aIsDir !== bIsDir) {
                            return bIsDir - aIsDir;
                        }
                        return a[0].localeCompare(b[0], undefined, {sensitivity: 'base'});
                    });

                    for (const [name, info] of entries) {
                                                 const path = currentPath ? `${currentPath}/${name}` : name;

                        // Skip non-matching items in search mode
                        if (this.searchMode && !this.searchMatches.has(path) && !this.hasMatchingChildren(info, path)) {
                            continue;
                        }

                        const item = this.createItemElement(name, info, path, level);
                        parent.appendChild(item);

                        // Render children if expanded
                        if (info.type === 'directory' && this.expandedNodes.has(path)) {
                            this.renderLevel(info.contents, parent, path, level + 1);
                        }
                    }
                }

                hasMatchingChildren(info, path) {
                    if (info.type !== 'directory') return false;

                    for (const match of this.searchMatches) {
                        if (match.startsWith(path + '/')) {
                            return true;
                        }
                    }
                    return false;
                }

                createItemElement(name, info, path, level) {
                    const item = document.createElement('div');
                    item.className = `item ${info.type}`;
                    item.dataset.path = path;
                    item.style.marginLeft = (level * 20) + 'px';

                    const prefix = info.type === 'directory' ? '📁 ' : '📄 ';
                    const statusClass = info.maintainer_info.status === 'maintained'
                        ? 'maintained' + (info.maintainer_info.inherited ? ' inherited' : '')
                        : info.maintainer_info.status === 'odd fixes'
                            ? 'odd-fixes'
                            : 'unmaintained';

                    let statusSummary = '';
                    if (info.type === 'directory') {
                        statusSummary = this.getStatusSummary(info.contents, path);
                    }

                    const isExpanded = this.expandedNodes.has(path);
                    const toggleSymbol = info.type === 'directory'
                        ? (isExpanded ? '▼' : '▶')
                        : '';

                    // Highlight search matches
                    const isMatch = this.searchMatches.has(path);
                    const nameClass = 'item-name';
                    const matchClass = isMatch ? 'search-match' : '';

                                        item.innerHTML = `
                        <div class="item-wrapper ${matchClass}">
                            <div class="toggle" style="visibility: ${info.type === 'directory' ? 'visible' : 'hidden'}">${toggleSymbol}</div>
                            <div class="item-content">
                                <span class="${statusClass} ${nameClass}">${prefix}${name}</span>
                                ${statusSummary}
                            </div>
                        </div>
                    `;

                    return item;
                }
            }

            // Initialize when DOM is ready
            document.addEventListener('DOMContentLoaded', () => {
                new TreeViewer();
            });
        </script>
    </body>
    </html>
    '''

    # Write to index.html instead of maintainers_visualization.html
    with open('index.html', 'w') as f:
        f.write(html)

def main():
    # Get the repository structure
    structure = get_repo_structure()

    # Initialize maintainers
    maintainers = Maintainers()

    # Process the structure and add maintainer information
    processed_structure = process_structure(structure, maintainers)

    # Generate the HTML visualization
    generate_html(processed_structure)
    print("Generated index.html")

if __name__ == '__main__':
    main()