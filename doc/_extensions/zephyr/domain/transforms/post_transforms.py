from typing import Any

from anytree import search
from docutils import nodes
from sphinx import addnodes
from sphinx.transforms.post_transforms import SphinxPostTransform
from sphinx.util import logging
from sphinx.util.nodes import NodeMatcher, make_refnode

from zephyr.domain.nodes import CodeSampleListingNode, RelatedCodeSamplesNode
from zephyr.domain.utils import create_code_sample_list

logger = logging.getLogger(__name__)


class CodeSampleCategoriesTocPatching(SphinxPostTransform):
    default_priority = 5  # needs to run *before* ReferencesResolver

    def output_sample_categories_list_items(self, tree, container: nodes.Node):
        list_item = nodes.list_item()
        compact_paragraph = addnodes.compact_paragraph()
        # find docname for tree.category["id"]
        docname = self.env.domaindata["zephyr"]["code-samples-categories"][tree.category["id"]][
            "docname"
        ]
        reference = nodes.reference(
            "",
            "",
            *[nodes.Text(tree.category["name"])],
            internal=True,
            refuri=docname,
            anchorname="",
            classes=["category-link"],
        )
        compact_paragraph += reference
        list_item += compact_paragraph

        sorted_children = sorted(tree.children, key=lambda x: x.category["name"])

        # add bullet list for children (if there are any, i.e. there are subcategories or at least
        # one code sample in the category)
        if sorted_children or any(
            code_sample.get("category") == tree.category["id"]
            for code_sample in self.env.domaindata["zephyr"]["code-samples"].values()
        ):
            bullet_list = nodes.bullet_list()
            for child in sorted_children:
                self.output_sample_categories_list_items(child, bullet_list)

            for code_sample in sorted(
                [
                    code_sample
                    for code_sample in self.env.domaindata["zephyr"]["code-samples"].values()
                    if code_sample.get("category") == tree.category["id"]
                ],
                key=lambda x: x["name"].casefold(),
            ):
                li = nodes.list_item()
                sample_xref = nodes.reference(
                    "",
                    "",
                    *[nodes.Text(code_sample["name"])],
                    internal=True,
                    refuri=code_sample["docname"],
                    anchorname="",
                    classes=["code-sample-link"],
                )
                sample_xref["reftitle"] = code_sample["description"].astext()
                compact_paragraph = addnodes.compact_paragraph()
                compact_paragraph += sample_xref
                li += compact_paragraph
                bullet_list += li

            list_item += bullet_list

        container += list_item

    def run(self, **kwargs: Any) -> None:
        if not self.document.get("needs_toc_patch"):
            return

        code_samples_categories_tree = self.env.domaindata["zephyr"]["code-samples-categories-tree"]

        category = search.find(
            code_samples_categories_tree,
            lambda node: hasattr(node, "category") and node.category["docname"] == self.env.docname,
        )

        bullet_list = nodes.bullet_list()
        self.output_sample_categories_list_items(category, bullet_list)

        self.env.tocs[self.env.docname] = bullet_list


class ProcessCodeSampleListingNode(SphinxPostTransform):
    default_priority = 5  # needs to run *before* ReferencesResolver

    def output_sample_categories_sections(self, tree, container: nodes.Node, show_titles=False):
        if show_titles:
            section = nodes.section(ids=[tree.category["id"]])

            link = make_refnode(
                self.env.app.builder,
                self.env.docname,
                tree.category["docname"],
                targetid=None,
                child=nodes.Text(tree.category["name"]),
            )
            title = nodes.title("", "", link)
            section += title
            container += section
        else:
            section = container

        # list samples from this category
        list_node = create_code_sample_list(
            [
                code_sample
                for code_sample in self.env.domaindata["zephyr"]["code-samples"].values()
                if code_sample.get("category") == tree.category["id"]
            ]
        )
        section += list_node

        sorted_children = sorted(tree.children, key=lambda x: x.name)
        for child in sorted_children:
            self.output_sample_categories_sections(child, section, show_titles=True)

    def run(self, **kwargs: Any) -> None:
        matcher = NodeMatcher(CodeSampleListingNode)

        for node in self.document.traverse(matcher):
            self.env.domaindata["zephyr"]["has_code_sample_listing"][self.env.docname] = True

            code_samples_categories = self.env.domaindata["zephyr"]["code-samples-categories"]
            code_samples_categories_tree = self.env.domaindata["zephyr"][
                "code-samples-categories-tree"
            ]

            container = nodes.container()
            container["classes"].append("code-sample-listing")

            if self.env.app.builder.format == "html" and node["live-search"]:
                search_input = nodes.raw(
                    "",
                    '''
                    <div class="cs-search-bar">
                      <input type="text" class="cs-search-input"
                             placeholder="Filter code samples..." onkeyup="filterSamples(this)">
                      <i class="fa fa-search"></i>
                    </div>
                    ''',
                    format="html",
                )
                container += search_input

            for category in node["categories"]:
                if category not in code_samples_categories:
                    logger.error(
                        f"Category {category} not found in code samples categories",
                        location=(self.env.docname, node.line),
                    )
                    continue

                category_node = search.find(
                    code_samples_categories_tree,
                    lambda n, cat=category: hasattr(n, "category") and n.category["id"] == cat,
                )
                self.output_sample_categories_sections(category_node, container)

            node.replace_self(container)


class ProcessRelatedCodeSamplesNode(SphinxPostTransform):
    default_priority = 5  # before ReferencesResolver

    def run(self, **kwargs: Any) -> None:
        matcher = NodeMatcher(RelatedCodeSamplesNode)
        for node in self.document.traverse(matcher):
            node_id = node["id"]

            code_samples = self.env.domaindata["zephyr"]["code-samples"].values()
            code_samples = [
                code_sample
                for code_sample in code_samples
                if node_id in code_sample["relevant-api"]
            ]

            if len(code_samples) > 0:
                admonition = nodes.admonition()
                admonition += nodes.title(text="Related code samples")
                admonition["classes"].append("related-code-samples")
                admonition["classes"].append("dropdown")
                admonition["classes"].append("toggle-shown")

                sample_list = create_code_sample_list(code_samples)
                admonition += sample_list

                node.replace_self(admonition)
            else:
                node.replace_self([])
