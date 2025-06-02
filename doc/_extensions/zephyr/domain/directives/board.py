import json

from docutils import nodes
from docutils.parsers.rst import directives, roles
from sphinx import addnodes
from sphinx.util import logging
from sphinx.util.docutils import SphinxDirective
from sphinx.util.nodes import NodeMatcher
from sphinx.util.template import SphinxRenderer

from zephyr.domain.constants import TEMPLATES_DIR
from zephyr.domain.nodes import BoardNode
from zephyr.domain.utils import BINDING_TYPE_TO_DOCUTILS_NODE, parse_text_with_acronyms

logger = logging.getLogger(__name__)


class BoardDirective(SphinxDirective):
    has_content = False
    required_arguments = 1
    optional_arguments = 0

    def run(self):
        # board_name is passed as the directive argument
        board_name = self.arguments[0]

        boards = self.env.domaindata["zephyr"]["boards"]
        vendors = self.env.domaindata["zephyr"]["vendors"]

        if board_name not in boards:
            logger.warning(
                f"Board {board_name} does not seem to be a valid board name.",
                location=(self.env.docname, self.lineno),
            )
            return []
        elif "docname" in boards[board_name]:
            logger.warning(
                f"Board {board_name} is already documented in {boards[board_name]['docname']}.",
                location=(self.env.docname, self.lineno),
            )
            return []
        else:
            self.env.domaindata["zephyr"]["has_board"][self.env.docname] = True
            board = boards[board_name]
            # flag board in the domain data as now having a documentation page so that it can be
            # cross-referenced etc.
            board["docname"] = self.env.docname

            board_node = BoardNode(id=board_name)
            board_node["full_name"] = board["full_name"]
            board_node["vendor"] = vendors.get(board["vendor"], board["vendor"])
            board_node["revision_default"] = board["revision_default"]
            board_node["supported_features"] = board["supported_features"]
            board_node["archs"] = board["archs"]
            board_node["socs"] = board["socs"]
            board_node["image"] = board["image"]
            board_node["supported_runners"] = board["supported_runners"]
            board_node["flash_runner"] = board["flash_runner"]
            board_node["debug_runner"] = board["debug_runner"]
            return [board_node]


class BoardCatalogDirective(SphinxDirective):
    has_content = False
    required_arguments = 0
    optional_arguments = 0

    def run(self):
        if self.env.app.builder.format == "html":
            self.env.domaindata["zephyr"]["has_board_catalog"][self.env.docname] = True

            domain_data = self.env.domaindata["zephyr"]
            renderer = SphinxRenderer([TEMPLATES_DIR])
            rendered = renderer.render(
                "board-catalog.html",
                {
                    "boards": domain_data["boards"],
                    "shields": domain_data["shields"],
                    "vendors": domain_data["vendors"],
                    "socs": domain_data["socs"],
                    "hw_features_present": self.env.app.config.zephyr_generate_hw_features,
                },
            )
            return [nodes.raw("", rendered, format="html")]
        else:
            return [nodes.paragraph(text="Board catalog is only available in HTML.")]


class BoardSupportedHardwareDirective(SphinxDirective):
    """A directive for showing the supported hardware features of a board."""

    has_content = False
    required_arguments = 0
    optional_arguments = 0

    def run(self):
        env = self.env
        docname = env.docname

        matcher = NodeMatcher(BoardNode)
        board_nodes = list(self.state.document.traverse(matcher))
        if not board_nodes:
            logger.warning(
                "board-supported-hw directive must be used in a board documentation page.",
                location=(docname, self.lineno),
            )
            return []

        board_node = board_nodes[0]
        supported_features = board_node["supported_features"]
        result_nodes = []

        paragraph = nodes.paragraph()
        paragraph += nodes.Text("The ")
        paragraph += nodes.literal(text=board_node["id"])
        paragraph += nodes.Text(" board supports the hardware features listed below.")
        result_nodes.append(paragraph)

        if not env.app.config.zephyr_generate_hw_features:
            note = nodes.admonition()
            note += nodes.title(text="Note")
            note["classes"].append("warning")
            note += nodes.paragraph(
                text="The list of supported hardware features was not generated. Run a full "
                "documentation build for the required metadata to be available."
            )
            result_nodes.append(note)
            return result_nodes

        html_contents = '''<div class="legend admonition">
  <dl class="supported-hardware field-list">
    <dt>
      <span class="location-chip onchip">on-chip</span> /
      <span class="location-chip onboard">on-board</span>
    </dt>
    <dd>
      Feature integrated in the SoC / present on the board.
    </dd>
    <dt>
      <span class="count okay-count">2</span> /
      <span class="count disabled-count">2</span>
    </dt>
    <dd>
      Number of instances that are enabled / disabled. <br/>
      Click on the label to see the first instance of this feature in the board/SoC DTS files.
    </dd>
    <dt>
      <code class="docutils literal notranslate"><span class="pre">vnd,foo</span></code>
    </dt>
    <dd>
      Compatible string for the Devicetree binding matching the feature. <br/>
      Click on the link to view the binding documentation.
    </dd>
  </dl>
</div>'''
        result_nodes.append(nodes.raw("", html_contents, format="html"))

        tables_container = nodes.container(ids=[f"{board_node['id']}-hw-features"])
        result_nodes.append(tables_container)

        board_json = json.dumps(
            {
                "board_name": board_node["id"],
                "revision_default": board_node["revision_default"],
                "targets": list(supported_features.keys()),
            }
        )
        result_nodes.append(
            nodes.raw(
                "",
                f"""<script>board_data = {board_json}</script>""",
                format="html",
            )
        )

        for target, features in sorted(supported_features.items()):
            if not features:
                continue

            target_heading = nodes.section(ids=[f"{board_node['id']}-{target}-hw-features-section"])
            heading = nodes.title()
            heading += nodes.literal(text=target)
            heading += nodes.Text(" target")
            target_heading += heading
            tables_container += target_heading

            table = nodes.table(
                classes=["colwidths-given", "hardware-features"],
                ids=[f"{board_node['id']}-{target}-hw-features-table"],
            )
            tgroup = nodes.tgroup(cols=4)

            tgroup += nodes.colspec(colwidth=15, classes=["type"])
            tgroup += nodes.colspec(colwidth=12, classes=["location"])
            tgroup += nodes.colspec(colwidth=53, classes=["description"])
            tgroup += nodes.colspec(colwidth=20, classes=["compatible"])

            thead = nodes.thead()
            row = nodes.row()
            headers = ["Type", "Location", "Description", "Compatible"]
            for header in headers:
                entry = nodes.entry(classes=[header.lower()])
                entry += nodes.paragraph(text=header)
                row += entry
            thead += row
            tgroup += thead

            tbody = nodes.tbody()

            def feature_sort_key(feature):
                # Put "CPU" first. Later updates might also give priority to features
                # like "sensor"s, for example.
                if feature == "cpu":
                    return (0, feature)
                return (1, feature)

            sorted_features_keys = sorted(features.keys(), key=feature_sort_key)

            for feature_key in sorted_features_keys:
                items = list(features[feature_key].items())
                num_items = len(items)

                for i, (key, value) in enumerate(items):
                    row = nodes.row()

                    # TYPE column
                    if i == 0:
                        type_entry = nodes.entry(morerows=num_items - 1, classes=["type"])
                        type_entry += nodes.paragraph(
                            "",
                            "",
                            BINDING_TYPE_TO_DOCUTILS_NODE.get(
                                feature_key, nodes.Text(feature_key)
                            ).deepcopy(),
                        )
                        row += type_entry

                    # LOCATION column
                    location_entry = nodes.entry(classes=["location"])
                    location_para = nodes.paragraph()

                    if "board" in value["locations"]:
                        location_chip = nodes.inline(
                            classes=["location-chip", "onboard"],
                            text="on-board",
                        )
                        location_para += location_chip
                    elif "soc" in value["locations"]:
                        location_chip = nodes.inline(
                            classes=["location-chip", "onchip"],
                            text="on-chip",
                        )
                        location_para += location_chip

                    location_entry += location_para
                    row += location_entry

                    # DESCRIPTION column
                    desc_entry = nodes.entry(classes=["description"])
                    desc_para = nodes.paragraph(classes=["status"])
                    if value["title"]:
                        desc_para += parse_text_with_acronyms(value["title"], uppercase_only=True)
                    else:
                        desc_para += nodes.Text(value["description"])

                    # Add count indicators for okay and not-okay instances
                    okay_nodes = value.get("okay_nodes", [])
                    disabled_nodes = value.get("disabled_nodes", [])

                    role_fn, _ = roles.role(
                        "zephyr_file", self.state_machine.language, self.lineno, self.state.reporter
                    )

                    def create_count_indicator(nodes_list, class_type, role_function=role_fn):
                        if not nodes_list:
                            return None

                        count = len(nodes_list)

                        if role_function is None:
                            return nodes.inline(
                                classes=["count", f"{class_type}-count"], text=str(count)
                            )

                        # Create a reference to the first node in the list
                        first_node_item = nodes_list[0]  # Renamed first_node to first_node_item
                        file_ref = (
                            f"{count} <{first_node_item['filename']}#L{first_node_item['lineno']}>"
                        )

                        role_nodes, _ = role_function(
                            "zephyr_file", file_ref, file_ref, self.lineno, self.state.inliner
                        )

                        count_node = role_nodes[0]
                        count_node["classes"] = ["count", f"{class_type}-count"]

                        return count_node

                    desc_para += create_count_indicator(okay_nodes, "okay")
                    desc_para += create_count_indicator(disabled_nodes, "disabled")

                    desc_entry += desc_para
                    row += desc_entry

                    # COMPATIBLE column
                    compatible_entry = nodes.entry(classes=["compatible"])
                    xref = addnodes.pending_xref(
                        "",
                        refdomain="std",
                        reftype="dtcompatible",
                        reftarget=key,
                        refexplicit=False,
                        refwarn=(not value.get("custom_binding", False)),
                    )
                    xref += nodes.literal(text=key)
                    compatible_entry += nodes.paragraph("", "", xref)
                    row += compatible_entry

                    tbody += row

                    # Declare the dts and binding files as dependencies of the board doc page,
                    # ensuring that the page is rerendered if the files change.
                    for dep_node in okay_nodes + disabled_nodes:  # Renamed node to dep_node
                        env.note_dependency(dep_node["dts_path"])
                        env.note_dependency(dep_node["binding_path"])

            tgroup += tbody
            table += tgroup
            tables_container += table

        return result_nodes


class BoardSupportedRunnersDirective(SphinxDirective):
    """A directive for showing the supported runners of a board."""

    has_content = False
    required_arguments = 0
    optional_arguments = 0

    def run(self):
        env = self.env
        docname = env.docname

        matcher = NodeMatcher(BoardNode)
        board_nodes = list(self.state.document.traverse(matcher))
        if not board_nodes:
            logger.warning(
                "board-supported-runners directive must be used in a board documentation page.",
                location=(docname, self.lineno),
            )
            return []

        if not env.app.config.zephyr_generate_hw_features:
            note = nodes.admonition()
            note += nodes.title(text="Note")
            note["classes"].append("warning")
            note += nodes.paragraph(
                text="The list of supported runners was not generated. Run a full documentation "
                "build for the required metadata to be available."
            )
            return [note]

        board_node = board_nodes[0]
        runners = board_node["supported_runners"]
        flash_runner = board_node["flash_runner"]
        debug_runner = board_node["debug_runner"]

        result_nodes = []

        paragraph = nodes.paragraph()
        paragraph += nodes.Text("The ")
        paragraph += nodes.literal(text=board_node["id"])
        paragraph += nodes.Text(
            " board supports the runners and associated west commands listed below."
        )
        result_nodes.append(paragraph)

        env_runners = env.domaindata["zephyr"]["runners"]
        commands = ["flash", "debug"]
        for runner_name in env_runners:  # Renamed runner to runner_name
            if runner_name in board_node["supported_runners"]:
                for cmd in env_runners[runner_name].get("commands", []):
                    if cmd not in commands:
                        commands.append(cmd)

        # create the table
        table = nodes.table(classes=["colwidths-given", "runners-table"])
        tgroup = nodes.tgroup(cols=len(commands) + 1)  # +1 for the Runner column

        # Add colspec for Runner column
        tgroup += nodes.colspec(colwidth=15, classes=["type"])
        # Add colspecs for command columns
        for _ in commands:
            tgroup += nodes.colspec(colwidth=15, classes=["type"])

        thead = nodes.thead()
        row = nodes.row()
        entry = nodes.entry()
        row += entry
        headers = [*commands]
        for header in headers:
            entry = nodes.entry(classes=[header.lower()])
            entry += addnodes.literal_strong(text=header, classes=["command"])
            row += entry
        thead += row
        tgroup += thead

        tbody = nodes.tbody()

        # add a row for each runner
        for runner_name_sorted in sorted(runners):  # Renamed runner to runner_name_sorted
            row = nodes.row()
            # First column - Runner name
            entry = nodes.entry()

            xref = addnodes.pending_xref(
                "",
                refdomain="std",
                reftype="ref",
                reftarget=f"runner_{runner_name_sorted}",
                refexplicit=True,
                refwarn=False,
            )
            xref += nodes.Text(runner_name_sorted)
            entry += addnodes.literal_strong("", "", xref)
            row += entry

            # Add columns for each command
            for command in commands:
                entry = nodes.entry()
                if command in env_runners[runner_name_sorted].get("commands", []):
                    entry += nodes.Text("✅")
                    if (command == "flash" and runner_name_sorted == flash_runner) or (
                        command == "debug" and runner_name_sorted == debug_runner
                    ):
                        entry += nodes.Text(" (default)")
                row += entry
            tbody += row

        tgroup += tbody
        table += tgroup

        result_nodes.append(table)

        return result_nodes
