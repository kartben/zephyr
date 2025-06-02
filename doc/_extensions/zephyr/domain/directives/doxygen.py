from docutils import nodes

from zephyr.doxybridge import DoxygenGroupDirective
from zephyr.domain.nodes import RelatedCodeSamplesNode


class CustomDoxygenGroupDirective(DoxygenGroupDirective):
    """Monkey patch for Breathe's DoxygenGroupDirective."""

    def run(self) -> list[nodes.Node]:
        nodes_list = super().run()

        if self.config.zephyr_breathe_insert_related_samples:
            return [*nodes_list, RelatedCodeSamplesNode(id=self.arguments[0])]
        else:
            return nodes_list
