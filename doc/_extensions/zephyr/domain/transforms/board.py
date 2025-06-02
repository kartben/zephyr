from docutils import nodes
from sphinx.transforms import SphinxTransform
from sphinx.util.nodes import NodeMatcher

from zephyr.gh_utils import gh_link_get_url
from zephyr.domain.nodes import BoardNode


class ConvertBoardNode(SphinxTransform):
    default_priority = 100

    def apply(self):
        matcher = NodeMatcher(BoardNode)
        for node in self.document.traverse(matcher):
            self.convert_node(node)

    def convert_node(self, node):
        parent = node.parent
        siblings_to_move = []
        if parent is not None:
            index = parent.index(node)
            siblings_to_move = parent.children[index + 1 :]

            new_section = nodes.section(ids=[node["id"]])
            new_section += nodes.title(text=node["full_name"])

            # create a sidebar with all the board details
            sidebar = nodes.sidebar(classes=["board-overview"])
            new_section += sidebar
            sidebar += nodes.title(text="Board Overview")

            if node["image"] is not None:
                figure = nodes.figure()
                # set a scale of 100% to indicate we want a link to the full-size image
                figure += nodes.image(uri=f"/{node['image']}", scale=100)
                figure += nodes.caption(text=node["full_name"])
                sidebar += figure

            field_list = nodes.field_list()
            sidebar += field_list

            details = [
                ("Name", nodes.literal(text=node["id"])),
                ("Vendor", node["vendor"]),
                ("Architecture", ", ".join(node["archs"])),
                ("SoC", ", ".join(node["socs"])),
            ]

            for property_name, value in details:
                field = nodes.field()
                field_name = nodes.field_name(text=property_name)
                field_body = nodes.field_body()
                if isinstance(value, nodes.Node):
                    field_body += value
                else:
                    field_body += nodes.paragraph(text=value)
                field += field_name
                field += field_body
                field_list += field

            gh_link = gh_link_get_url(self.app, self.env.docname)
            gh_link_button = nodes.raw(
                "",
                f"""
                <div id="board-github-link">
                    <a href="{gh_link}/../.." class="btn btn-info fa fa-github"
                        target="_blank">
                        Browse board sources
                    </a>
                </div>
                """,
                format="html",
            )
            sidebar += gh_link_button

            # Move the sibling nodes under the new section
            new_section.extend(siblings_to_move)

            # Replace the custom node with the new section
            node.replace_self(new_section)

            # Remove the moved siblings from their original parent
            for sibling in siblings_to_move:
                parent.remove(sibling)
