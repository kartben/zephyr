"""
Zephyr Extension
################

Copyright (c) 2023-2025 The Linux Foundation
SPDX-License-Identifier: Apache-2.0

This extension adds a new ``zephyr`` domain for handling the documentation of various entities
specific to the Zephyr RTOS project (ex. code samples).

Directives
----------

- ``zephyr:code-sample::`` - Defines a code sample.
- ``zephyr:code-sample-category::`` - Defines a category for grouping code samples.
- ``zephyr:code-sample-listing::`` - Shows a listing of code samples found in a given category.
- ``zephyr:board-catalog::`` - Shows a listing of boards supported by Zephyr.
- ``zephyr:board::`` - Flags a document as being the documentation page for a board.
- ``zephyr:board-supported-hw::`` - Shows a table of supported hardware features for all the targets
  of the board documented in the current page.
- ``zephyr:board-supported-runners::`` - Shows a table of supported runners for the board documented
  in the current page.

Roles
-----

- ``:zephyr:code-sample:`` - References a code sample.
- ``:zephyr:code-sample-category:`` - References a code sample category.
- ``:zephyr:board:`` - References a board.

"""

# Minimal imports for the setup function
from zephyr.domain.constants import RESOURCES_DIR
from zephyr.domain.transforms import (
    ConvertBoardNode,
    ConvertCodeSampleCategoryNode,
    ConvertCodeSampleNode,
    CodeSampleCategoriesTocPatching,
    ProcessCodeSampleListingNode,
    ProcessRelatedCodeSamplesNode
)
from zephyr.domain.directives import CustomDoxygenGroupDirective
from zephyr.domain.domain import ZephyrDomain
from zephyr.domain import events

__version__ = "0.2.0"

def setup(app):  # Sphinx type hint removed for minimality
    app.add_config_value("zephyr_breathe_insert_related_samples", False, "env")
    app.add_config_value("zephyr_generate_hw_features", False, "env")
    app.add_config_value("zephyr_hw_features_vendor_filter", [], "env", types=[list[str]])

    app.add_domain(ZephyrDomain)

    app.add_transform(ConvertCodeSampleNode)
    app.add_transform(ConvertCodeSampleCategoryNode)
    app.add_transform(ConvertBoardNode)

    app.add_post_transform(ProcessCodeSampleListingNode)
    app.add_post_transform(CodeSampleCategoriesTocPatching)
    app.add_post_transform(ProcessRelatedCodeSamplesNode)

    app.connect(
        "builder-inited",
        (lambda app_lambda: app_lambda.config.html_static_path.append(RESOURCES_DIR.as_posix())),
    )
    app.connect("builder-inited", events.load_board_catalog_into_domain)

    app.connect("html-page-context", events.install_static_assets_as_needed)
    app.connect("env-updated", events.compute_sample_categories_hierarchy)

    # monkey-patching of the DoxygenGroupDirective
    app.add_directive("doxygengroup", CustomDoxygenGroupDirective, override=True)

    return {
        "version": __version__,
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
