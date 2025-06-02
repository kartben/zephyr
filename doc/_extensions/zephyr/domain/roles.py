from docutils import nodes
from sphinx.roles import XRefRole

# Define roles for the Zephyr domain
CODE_SAMPLE_ROLE = XRefRole(innernodeclass=nodes.inline, warn_dangling=True)
CODE_SAMPLE_CATEGORY_ROLE = XRefRole(innernodeclass=nodes.inline, warn_dangling=True)
BOARD_ROLE = XRefRole(innernodeclass=nodes.inline, warn_dangling=True)

# Dictionary of roles to be used by the ZephyrDomain class
ZEPHYR_ROLES = {
    "code-sample": CODE_SAMPLE_ROLE,
    "code-sample-category": CODE_SAMPLE_CATEGORY_ROLE,
    "board": BOARD_ROLE,
}
