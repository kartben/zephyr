import re

from docutils import nodes

from zephyr.domain.constants import BINDINGS_TXT_PATH

ACRONYM_PATTERN = re.compile(r'([a-zA-Z0-9-]+)\s*\((.*?)\)')
ACRONYM_PATTERN_UPPERCASE_ONLY = re.compile(r'(\b[A-Z0-9-]+)\s*\((.*?)\)')
BINDING_TYPE_TO_DOCUTILS_NODE = {}


def parse_text_with_acronyms(text, uppercase_only=False):
    """Parse text that may contain acronyms into a list of nodes."""
    result = nodes.inline()
    last_end = 0

    pattern = ACRONYM_PATTERN_UPPERCASE_ONLY if uppercase_only else ACRONYM_PATTERN
    for match in pattern.finditer(text):
        # Add any text before the acronym
        if match.start() > last_end:
            result += nodes.Text(text[last_end : match.start()])

        # Add the acronym
        abbr, explanation = match.groups()
        result += nodes.abbreviation(abbr, abbr, explanation=explanation)
        last_end = match.end()

    # Add any remaining text
    if last_end < len(text):
        result += nodes.Text(text[last_end:])

    return result


with open(BINDINGS_TXT_PATH) as f:
    for line in f:
        line = line.strip()
        if not line or line.startswith('#'):
            continue

        key, value = line.split('\t', 1)
        BINDING_TYPE_TO_DOCUTILS_NODE[key] = parse_text_with_acronyms(value)
