# Copyright (c) 2023 Intel Corporation
# SPDX-License-Identifier: Apache-2.0

import os
from pathlib import Path
from typing import Any

import doxmlparser
from docutils import nodes
from doxmlparser.compound import DoxCompoundKind
from sphinx.util.docutils import SphinxDirective

from zephyr.doxyrunner import doxygen_outputs


def get_group(innergroup, all_groups):
    try:
        return [
            group
            for group in all_groups
            if group.get_compounddef()[0].get_id() == innergroup.get_refid()
        ][0]
    except IndexError as e:
        raise Exception(f"Unexpected group {innergroup.get_refid()}") from e


def parse_xml_dir(xml_dir: Path):
    groups = []
    root = doxmlparser.index.parse(xml_dir / "index.xml", True)
    for compound in root.get_compound():
        if compound.get_kind() == DoxCompoundKind.GROUP:
            file_name = xml_dir / f"{compound.get_refid()}.xml"
            groups.append(doxmlparser.compound.parse(file_name, True))

    return groups


class ApiOverview(SphinxDirective):
    """
    This is a Zephyr directive to generate a table containing an overview
    of all APIs. This table will show the API name, version and since which
    version it is present - all information extracted from Doxygen XML output.

    It is exclusively used by the doc/develop/api/overview.rst page.

    The Doxygen output it reads is located through zephyr.doxyrunner's
    doxyrunner_projects; api_overview_base_url points at the repository used for the
    "since" release links.
    """

    def run(self):
        outputs = doxygen_outputs(self.config)
        if not outputs:
            return []
        if len(outputs) != 1:
            raise self.error("api-overview-table requires exactly one Doxygen project")

        self.doxygen_output = next(iter(outputs.values()))

        self.env.note_dependency(str(self.doxygen_output.xml / "index.xml"))
        groups = parse_xml_dir(self.doxygen_output.xml)

        inners = [group.get_compounddef()[0].get_innergroup() for group in groups]
        inner_ids = [i.get_refid() for inner in inners for i in inner]
        toplevel = [
            group for group in groups if group.get_compounddef()[0].get_id() not in inner_ids
        ]

        return [self.generate_table(toplevel, groups)]

    def generate_table(self, toplevel, groups):
        table = nodes.table()
        tgroup = nodes.tgroup()

        thead = nodes.thead()
        thead_row = nodes.row()
        for header_name in ["API", "Version", "Available in Zephyr Since"]:
            colspec = nodes.colspec()
            tgroup += colspec

            entry = nodes.entry()
            entry += nodes.Text(header_name)
            thead_row += entry
        thead += thead_row
        tgroup += thead

        rows = []
        tbody = nodes.tbody()
        for t in toplevel:
            self.visit_group(t, groups, rows)
        tbody.extend(rows)
        tgroup += tbody

        table += tgroup

        return table

    def visit_group(self, group, all_groups, rows, indent=0):
        version = since = ""
        github_uri = self.config.api_overview_base_url + "/releases/tag/"
        cdef = group.get_compounddef()[0]

        ssects = [s for p in cdef.get_detaileddescription().get_para() for s in p.get_simplesect()]
        for sect in ssects:
            if sect.get_kind() == "since":
                since = sect.get_para()[0].get_valueOf_()
            elif sect.get_kind() == "version":
                version = sect.get_para()[0].get_valueOf_()

        if since:
            # Some @since values already carry a patch component (e.g. "1.14.0");
            # normalize to a "major.minor.0" release tag to avoid "v1.14.0.0".
            release = ".".join(since.strip().split(".")[:2]) + ".0"
            since_url = nodes.inline()
            reference = nodes.reference(text=f"v{release}", refuri=f"{github_uri}v{release}")
            reference.attributes["internal"] = True
            since_url += reference
        else:
            since_url = nodes.Text("")

        abs_url = self.doxygen_output.html / f"{cdef.get_id()}.html"
        doc_dir = os.path.dirname(self.get_source_info()[0])
        doc_dest = os.path.join(
            self.env.app.outdir,
            os.path.relpath(doc_dir, self.env.app.srcdir),
        )
        url = os.path.relpath(abs_url, doc_dest)

        title = cdef.get_title()

        row_node = nodes.row()

        # Next entry will contain the spacer and the link with API name
        entry = nodes.entry()
        span = nodes.Text("".join(["\U000000a0"] * indent))
        entry += span

        # API name with link
        inline = nodes.inline()
        reference = nodes.reference(text=title, refuri=str(url))
        reference.attributes["internal"] = True
        inline += reference
        entry += inline
        row_node += entry

        version_node = nodes.Text(version)
        # Finally, add version and since
        for cell in [version_node, since_url]:
            entry = nodes.entry()
            entry += cell
            row_node += entry
        rows.append(row_node)

        for innergroup in cdef.get_innergroup():
            self.visit_group(get_group(innergroup, all_groups), all_groups, rows, indent + 6)


def setup(app) -> dict[str, Any]:
    app.add_config_value("api_overview_base_url", "", "env")

    app.add_directive("api-overview-table", ApiOverview)

    return {
        "version": "0.1",
        "parallel_read_safe": True,
        "parallel_write_safe": True,
    }
