import json

from docutils import nodes
from sphinx import addnodes
from sphinx.transforms import SphinxTransform
from sphinx.util.nodes import NodeMatcher

from zephyr.gh_utils import gh_link_get_url
from zephyr.domain.nodes import CodeSampleNode, CodeSampleCategoryNode


class ConvertCodeSampleNode(SphinxTransform):
    default_priority = 100

    def apply(self):
        matcher = NodeMatcher(CodeSampleNode)
        for node in self.document.traverse(matcher):
            self.convert_node(node)

    def convert_node(self, node):
        """
        Transforms a `CodeSampleNode` into a `nodes.section` named after the code sample name.

        Moves all sibling nodes that are after the `CodeSampleNode` in the document under this new
        section.

        Adds a "See Also" section at the end with links to all relevant APIs as per the samples's
        `relevant-api` attribute.
        """
        parent = node.parent
        siblings_to_move = []
        if parent is not None:
            index = parent.index(node)
            siblings_to_move = parent.children[index + 1 :]

            # Create a new section
            new_section = nodes.section(ids=[node["id"]])
            new_section += nodes.title(text=node["name"])

            gh_link = gh_link_get_url(self.app, self.env.docname)
            gh_link_button = nodes.raw(
                "",
                f"""
                <a href="{gh_link}/.." class="btn btn-info fa fa-github"
                    target="_blank" style="text-align: center;">
                    Browse source code on GitHub
                </a>
                """,
                format="html",
            )
            new_section += nodes.paragraph("", "", gh_link_button)

            # Move the sibling nodes under the new section
            new_section.extend(siblings_to_move)

            # Replace the custom node with the new section
            node.replace_self(new_section)

            # Remove the moved siblings from their original parent
            for sibling in siblings_to_move:
                parent.remove(sibling)

            # Add a "See Also" section at the end with links to relevant APIs
            if node["relevant-api"]:
                see_also_section = nodes.section(ids=["see-also"])
                see_also_section += nodes.title(text="See also")

                for api in node["relevant-api"]:
                    desc_node = addnodes.desc()
                    desc_node["domain"] = "c"
                    desc_node["objtype"] = "group"

                    title_signode = addnodes.desc_signature()
                    api_xref = addnodes.pending_xref(
                        "",
                        refdomain="c",
                        reftype="group",
                        reftarget=api,
                        refwarn=True,
                    )
                    api_xref += nodes.Text(api)
                    title_signode += api_xref
                    desc_node += title_signode
                    see_also_section += desc_node

                new_section += see_also_section

            # Set sample description as the meta description of the document for improved SEO
            meta_description = nodes.meta()
            meta_description["name"] = "description"
            meta_description["content"] = node.children[0].astext()
            # Accessing node.document after node.replace_self(new_section) might be problematic.
            # It might be better to add the meta_description to new_section or the document directly.
            # For now, assuming node.document is still valid or refers to the document root.
            if hasattr(node, 'document') and node.document:
                node.document += meta_description
            elif (
                hasattr(new_section, 'document') and new_section.document
            ):  # Fallback to new_section's document if available
                new_section.document += meta_description
            else:  # Fallback to self.document if others are not available
                self.document += meta_description

            # Similarly, add a node with JSON-LD markup (only renders in HTML output) describing
            # the code sample.
            json_ld_data = {
                "@context": "http://schema.org",
                "@type": "SoftwareSourceCode",
                "name": node['name'],
                "description": node.children[0].astext(),
                "codeSampleType": "full",
                "codeRepository": gh_link_get_url(self.app, self.env.docname),
            }
            json_ld = nodes.raw(
                "",
                f'''<script type="application/ld+json">
                {json.dumps(json_ld_data)}
                </script>''',
                format="html",
            )
            if hasattr(node, 'document') and node.document:
                node.document += json_ld
            elif hasattr(new_section, 'document') and new_section.document:
                new_section.document += json_ld
            else:
                self.document += json_ld


class ConvertCodeSampleCategoryNode(SphinxTransform):
    default_priority = 100

    def apply(self):
        matcher = NodeMatcher(CodeSampleCategoryNode)
        for node in self.document.traverse(matcher):
            self.convert_node(node)

    def convert_node(self, node):
        # move all the siblings of the category node underneath the section it contains
        parent = node.parent
        siblings_to_move = []
        if parent is not None:
            index = parent.index(node)
            siblings_to_move = parent.children[index + 1 :]

            node.children[0].extend(siblings_to_move)
            for sibling in siblings_to_move:
                parent.remove(sibling)

        # note document as needing toc patching
        self.document["needs_toc_patch"] = True

        # finally, replace the category node with the section it contains
        node.replace_self(node.children[0])
