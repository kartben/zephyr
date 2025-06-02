from os import path
from typing import Any

from docutils import nodes
from sphinx.application import Sphinx
from sphinx.environment import BuildEnvironment
from sphinx.util import logging

# This sys.path manipulation is needed because gen_boards_catalog is not (yet) a proper python package
# and this Sphinx extension is not (yet) installed in the environment.
# TODO: Make gen_boards_catalog a proper python package and install it in the environment.
# TODO: Install this Sphinx extension in the environment.
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).parents[4] / "scripts/dts/python-devicetree/src"))
sys.path.insert(0, str(Path(__file__).parents[4] / "scripts/west_commands"))
sys.path.insert(0, str(Path(__file__).parents[3] / "_scripts"))

from gen_boards_catalog import get_catalog


logger = logging.getLogger(__name__)


def compute_sample_categories_hierarchy(app: Sphinx, env: BuildEnvironment) -> None:
    domain = env.get_domain("zephyr")
    code_samples = domain.data["code-samples"]

    category_tree = env.domaindata["zephyr"]["code-samples-categories-tree"]
    resolver = env.domaindata["zephyr"]["code-samples-categories-resolver"]
    for code_sample in code_samples.values():
        try:
            # Try to get the node at the specified path
            node = resolver.get(category_tree, "/" + path.dirname(code_sample["docname"]))
        except (
            Exception
        ) as e:  # We expect a anytree.ChildResolverError but it's not directly importable
            # starting with e.node and up, find the first node that has a category
            node = e.node
            while not hasattr(node, "category"):
                node = node.parent
            code_sample["category"] = node.category["id"]


def install_static_assets_as_needed(
    app: Sphinx, pagename: str, templatename: str, context: dict[str, Any], doctree: nodes.Node
) -> None:
    if app.env.domaindata["zephyr"]["has_code_sample_listing"].get(pagename, False):
        app.add_css_file("css/codesample-livesearch.css")
        app.add_js_file("js/codesample-livesearch.js")

    if app.env.domaindata["zephyr"]["has_board_catalog"].get(pagename, False):
        app.add_css_file("css/board-catalog.css")
        app.add_js_file("js/board-catalog.js")

    if app.env.domaindata["zephyr"]["has_board"].get(pagename, False):
        app.add_css_file("css/board.css")
        app.add_js_file("js/board.js")


def load_board_catalog_into_domain(app: Sphinx) -> None:
    board_catalog = get_catalog(
        generate_hw_features=(
            app.builder.format == "html" and app.config.zephyr_generate_hw_features
        ),
        hw_features_vendor_filter=app.config.zephyr_hw_features_vendor_filter,
    )
    app.env.domaindata["zephyr"]["boards"] = board_catalog["boards"]
    app.env.domaindata["zephyr"]["shields"] = board_catalog["shields"]
    app.env.domaindata["zephyr"]["vendors"] = board_catalog["vendors"]
    app.env.domaindata["zephyr"]["socs"] = board_catalog["socs"]
    app.env.domaindata["zephyr"]["runners"] = board_catalog["runners"]
