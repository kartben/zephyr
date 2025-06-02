from docutils import nodes
from docutils.parsers.rst import directives
from docutils.statemachine import StringList
from sphinx.util import logging
from sphinx.util.docutils import SphinxDirective, switch_source_input
from sphinx.util.parsing import nested_parse_to_nodes

from zephyr.domain.nodes import (
    CodeSampleCategoryNode,
    CodeSampleListingNode,
    CodeSampleNode,
)

logger = logging.getLogger(__name__)


class CodeSampleDirective(SphinxDirective):
    """
    A directive for creating a code sample node in the Zephyr documentation.
    """

    required_arguments = 1  # ID
    optional_arguments = 0
    option_spec = {"name": directives.unchanged, "relevant-api": directives.unchanged}
    has_content = True

    def run(self):
        code_sample_id = self.arguments[0]
        env = self.state.document.settings.env
        code_samples = env.domaindata["zephyr"]["code-samples"]

        if code_sample_id in code_samples:
            logger.warning(
                f"Code sample {code_sample_id} already exists. "
                f"Other instance in {code_samples[code_sample_id]['docname']}",
                location=(env.docname, self.lineno),
            )

        name = self.options.get("name", code_sample_id)
        relevant_api_list = self.options.get("relevant-api", "").split()

        # Create a node for description and populate it with parsed content
        description_node = nodes.container(ids=[f"{code_sample_id}-description"])
        self.state.nested_parse(self.content, self.content_offset, description_node)

        code_sample = {
            "id": code_sample_id,
            "name": name,
            "description": description_node,
            "relevant-api": relevant_api_list,
            "docname": env.docname,
        }

        domain = env.get_domain("zephyr")
        domain.add_code_sample(code_sample)

        # Create an instance of the custom node
        code_sample_node = CodeSampleNode()
        code_sample_node["id"] = code_sample_id
        code_sample_node["name"] = name
        code_sample_node["relevant-api"] = relevant_api_list
        code_sample_node += description_node

        return [code_sample_node]


class CodeSampleCategoryDirective(SphinxDirective):
    required_arguments = 1  # Category ID
    optional_arguments = 0
    option_spec = {
        "name": directives.unchanged,
        "show-listing": directives.flag,
        "live-search": directives.flag,
        "glob": directives.unchanged,
    }
    has_content = True  # Category description
    final_argument_whitespace = True

    def run(self):
        env = self.state.document.settings.env
        category_id = self.arguments[0]  # Renamed id to category_id
        name = self.options.get("name", category_id)

        category_node = CodeSampleCategoryNode()
        category_node["id"] = category_id
        category_node["name"] = name
        category_node["docname"] = env.docname

        description_node = nodes.container(ids=[f"{category_id}-description"])
        self.state.nested_parse(self.content, self.content_offset, description_node)
        category_node["description"] = description_node

        code_sample_category = {
            "docname": env.docname,
            "id": category_id,
            "name": name,
        }

        domain = env.get_domain("zephyr")
        domain.add_code_sample_category(code_sample_category)

        lines = [
            name,
            "#" * len(name),
            "",
            ".. toctree::",
            "   :titlesonly:",
            "   :glob:",
            "   :hidden:",
            "   :maxdepth: 1",
            "",
            f"   {self.options['glob']}" if "glob" in self.options else "   */*",
            "",
        ]
        stringlist = StringList(lines, source=env.docname)

        with switch_source_input(self.state, stringlist):
            parsed_section = nested_parse_to_nodes(self.state, stringlist)[0]

        category_node += parsed_section
        parsed_section += description_node

        if "show-listing" in self.options:
            listing_node = CodeSampleListingNode()
            listing_node["categories"] = [category_id]
            listing_node["live-search"] = "live-search" in self.options
            parsed_section += listing_node

        return [category_node]


class CodeSampleListingDirective(SphinxDirective):
    has_content = False
    required_arguments = 0
    optional_arguments = 0
    option_spec = {
        "categories": directives.unchanged_required,
        "live-search": directives.flag,
    }

    def run(self):
        code_sample_listing_node = CodeSampleListingNode()
        code_sample_listing_node["categories"] = self.options.get("categories").split()
        code_sample_listing_node["live-search"] = "live-search" in self.options

        return [code_sample_listing_node]
