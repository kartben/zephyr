/**
 * Copyright (c) 2025, The Linux Foundation.
 * SPDX-License-Identifier: Apache-2.0
 */

(function () {
        function initHardwareFeatures() {
                const container = document.querySelector('.container[id$="-hw-features"]');
                if (!container) {
                        console.error('Container not found');
                        return;
                }

                const sections = Array.from(document.querySelectorAll('section[id$="-hw-features"]'));
                if (sections.length < 1) {
                        console.error('No sections found');
                        return;
                }

                console.log('Found sections:', sections.length, sections);

                const select = createSelect(sections);
                container.parentNode.insertBefore(select, container);

                // Initial setup - show first section
                showSection(0);
                select.value = 0;
        }

        function createSelect(sections) {
                const select = document.createElement('select');
                select.id = 'hw-features-select';
                select.style.margin = '20px 0';
                select.style.padding = '5px';
                select.style.width = '100%';
                select.style.maxWidth = '400px';

                sections.forEach((section, index) => {
                        let optionText = `Target ${index + 1}`;
                        const targetCode = section.querySelector('code');
                        if (targetCode) {
                                optionText = targetCode.textContent.trim();
                        }

                        const option = document.createElement('option');
                        option.value = index;
                        option.textContent = optionText;
                        select.appendChild(option);
                });

                select.addEventListener('change', function () {
                        showSection(this.value);
                });

                return select;
        }

        function showSection(index) {
                const sections = Array.from(document.querySelectorAll('section[id$="-hw-features"]'));

                // Hide all sections
                sections.forEach(section => {
                        section.style.display = 'none';
                });

                // Hide all tables and their responsive containers
                const allTables = document.querySelectorAll('table.hardware-features');
                allTables.forEach(table => {
                        table.style.display = 'none';
                        const responsiveContainer = table.closest('.wy-table-responsive');
                        if (responsiveContainer) {
                                responsiveContainer.style.display = 'none';
                        }
                });

                // Show the selected section
                const selectedSection = sections[parseInt(index)];
                if (selectedSection) {
                        // selectedSection.style.display = 'block';

                        // Find and show the corresponding table
                        const sectionId = selectedSection.id;
                        const table = document.querySelector(`table[id="${sectionId}"]`);
                        if (table) {
                                table.style.display = 'table';
                                const responsiveContainer = table.closest('.wy-table-responsive');
                                if (responsiveContainer) {
                                        responsiveContainer.style.display = 'block';
                                }
                        }
                }
        }

        // Run when DOM is loaded
        if (document.readyState === 'loading') {
                document.addEventListener('DOMContentLoaded', initHardwareFeatures);
        } else {
                initHardwareFeatures();
        }
})();