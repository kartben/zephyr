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
    status = 'maintained'
    all_maintainers = set()
    all_collaborators = set()
    all_labels = set()

    for area in areas:
        if area.status == 'unmaintained':
            status = 'unmaintained'
        elif area.status == 'odd fixes' and status != 'unmaintained':
            status = 'odd fixes'
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
    # Write data to JSON file
    with open('maintainers_data.json', 'w') as f:
        json.dump(data, f)

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
            .maintainer-info {
                display: none;
                position: fixed;
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
            }
            .maintainer-info.visible {
                opacity: 1;
            }
            .item {
                position: relative;
                cursor: pointer;
                padding: 2px 0;
            }
            .item-wrapper {
                display: flex;
                align-items: center;
                min-width: 0;  /* Enable flex item shrinking */
            }
            .toggle {
                flex: 0 0 20px;  /* Fixed width, don't grow or shrink */
                display: inline-flex;
                align-items: center;
                justify-content: center;
                cursor: pointer;
                user-select: none;
                color: #4a5568;
            }
            .item-content {
                flex: 1;  /* Take remaining space */
                min-width: 0;  /* Enable text truncation */
                display: flex;
                align-items: center;
            }
            .item-name {
                overflow: hidden;
                text-overflow: ellipsis;
                white-space: nowrap;
                min-width: 0;  /* Enable text truncation */
                flex: 1;
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
            .maintainer-info strong {
                color: #4a5568;
                font-weight: 600;
            }
            .maintainer-info br {
                margin-bottom: 4px;
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
                    <div id="tree-view" class="tree-view"></div>
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
        <script>
            // Add last update time
            document.getElementById('last-update').textContent = new Date().toLocaleString();

            // Global variables for tooltip management
            let currentTooltip = null;

            // Function to hide all tooltips
            function hideAllTooltips() {
                const tooltips = document.querySelectorAll('.maintainer-info');
                tooltips.forEach(tooltip => {
                    tooltip.classList.remove('visible');
                    setTimeout(() => {
                        if (!tooltip.classList.contains('visible')) {
                            tooltip.style.display = 'none';
                        }
                    }, 100);
                });
                currentTooltip = null;
            }

            // Load data from JSON file with proper path handling
            fetch(new URL('maintainers_data.json', window.location.href).href)
                .then(response => response.json())
                .then(data => {
                    function createTreeView(data, level = 0) {
                        const container = document.createElement('div');
                        container.style.marginLeft = level * 20 + 'px';

                        // Sort entries: directories first, then files, both alphabetically
                        const entries = Object.entries(data).sort((a, b) => {
                            const aIsDir = a[1].type === 'directory';
                            const bIsDir = b[1].type === 'directory';

                            if (aIsDir !== bIsDir) {
                                return bIsDir - aIsDir;
                            }
                            return a[0].localeCompare(b[0], undefined, {sensitivity: 'base'});
                        });

                        for (const [name, info] of entries) {
                            const item = document.createElement('div');
                            item.className = 'item';

                            const prefix = info.type === 'directory' ? '📁 ' : '📄 ';
                            const statusClass = info.maintainer_info.status === 'maintained'
                                ? 'maintained' + (info.maintainer_info.inherited ? ' inherited' : '')
                                : info.maintainer_info.status === 'odd fixes'
                                    ? 'odd-fixes'
                                    : 'unmaintained';

                            const tooltipContent = `
                                <strong>Status:</strong> ${info.maintainer_info.status}${info.maintainer_info.inherited ? ' (inherited from ' + info.maintainer_info.inherited_from + ')' : ''}<br>
                                <strong>Maintainers:</strong> ${info.maintainer_info.maintainers.join(', ') || 'None'}${info.maintainer_info.inherited ? ' (inherited)' : ''}<br>
                                <strong>Collaborators:</strong> ${info.maintainer_info.collaborators.join(', ') || 'None'}${info.maintainer_info.inherited ? ' (inherited)' : ''}<br>
                                <strong>Labels:</strong> ${info.maintainer_info.labels.join(', ') || 'None'}${info.maintainer_info.inherited ? ' (inherited)' : ''}
                            `;

                            item.innerHTML = `
                                <div class="item-wrapper">
                                    ${info.type === 'directory' ? '<div class="toggle">▶</div>' : '<div class="toggle" style="visibility:hidden">▶</div>'}
                                    <div class="item-content">
                                        <span class="${statusClass} item-name">${prefix}${name}</span>
                                    </div>
                                    <div class="maintainer-info">${tooltipContent}</div>
                                </div>
                            `;

                            // Add mouse event listeners for tooltip positioning
                            item.addEventListener('mouseenter', (e) => {
                                e.stopPropagation();
                                hideAllTooltips();

                                const tooltip = item.querySelector('.maintainer-info');
                                currentTooltip = tooltip;

                                // Position tooltip next to cursor
                                const rect = item.getBoundingClientRect();

                                // Default position is to the right of the item
                                let left = rect.right + 15;
                                let top = rect.top;

                                // If tooltip would go off right edge, show it to the left instead
                                if (left + 350 > window.innerWidth - 20) {
                                    left = rect.left - 350 - 15;
                                }

                                // If tooltip would go off bottom edge, adjust upward
                                if (top + 200 > window.innerHeight - 20) {
                                    top = window.innerHeight - 220;
                                }

                                tooltip.style.left = left + 'px';
                                tooltip.style.top = top + 'px';
                                tooltip.style.display = 'block';
                                // Delay adding visible class for smooth fade in
                                requestAnimationFrame(() => tooltip.classList.add('visible'));
                            });

                            item.addEventListener('mouseleave', (e) => {
                                const tooltip = item.querySelector('.maintainer-info');
                                if (tooltip === currentTooltip) {
                                    tooltip.classList.remove('visible');
                                    setTimeout(() => {
                                        if (!tooltip.classList.contains('visible')) {
                                            tooltip.style.display = 'none';
                                        }
                                    }, 100);
                                    currentTooltip = null;
                                }
                            });

                            if (info.type === 'directory') {
                                const contents = document.createElement('div');
                                contents.style.display = 'none';
                                contents.appendChild(createTreeView(info.contents, level + 1));

                                const toggle = item.querySelector('.toggle');
                                toggle.addEventListener('click', (e) => {
                                    e.stopPropagation();
                                    hideAllTooltips();
                                    const isHidden = contents.style.display === 'none';
                                    contents.style.display = isHidden ? 'block' : 'none';
                                    toggle.textContent = isHidden ? '▼' : '▶';
                                });

                                item.appendChild(contents);
                            }

                            container.appendChild(item);
                        }

                        return container;
                    }

                    document.getElementById('tree-view').appendChild(createTreeView(data));
                })
                .catch(error => {
                    console.error('Error loading data:', error);
                    document.getElementById('tree-view').innerHTML = '<div class="alert alert-danger">Error loading data. Please try again later.</div>';
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