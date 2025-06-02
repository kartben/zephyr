from docutils import nodes
from sphinx import addnodes


def create_code_sample_list(code_samples):
    """
    Creates a bullet list (`nodes.bullet_list`) of code samples from a list of code samples.

    The list is alphabetically sorted (case-insensitive) by the code sample name.
    """

    ul = nodes.bullet_list(classes=["code-sample-list"])

    for code_sample in sorted(code_samples, key=lambda x: x["name"].casefold()):
        li = nodes.list_item()

        sample_xref = addnodes.pending_xref(
            "",
            refdomain="zephyr",
            reftype="code-sample",
            reftarget=code_sample["id"],
            refwarn=True,
        )
        sample_xref += nodes.Text(code_sample["name"])
        li += nodes.inline("", "", sample_xref, classes=["code-sample-name"])

        li += nodes.inline(
            text=code_sample["description"].astext(),
            classes=["code-sample-description"],
        )

        ul += li

    return ul
