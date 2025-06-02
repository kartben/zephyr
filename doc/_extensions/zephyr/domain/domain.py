from collections.abc import Iterator
from os import path
from typing import Any

from anytree import ChildResolverError, Node, PreOrderIter, Resolver, search
from docutils import nodes
from sphinx.domains import Domain, ObjType
from sphinx.util.nodes import make_refnode

from zephyr.domain.directives import (
    BoardCatalogDirective,
    BoardDirective,
    BoardSupportedHardwareDirective,
    BoardSupportedRunnersDirective,
    CodeSampleCategoryDirective,
    CodeSampleDirective,
    CodeSampleListingDirective,
)
from zephyr.domain.roles import ZEPHYR_ROLES


class ZephyrDomain(Domain):
    """Zephyr domain"""

    name = "zephyr"
    label = "Zephyr"

    roles = ZEPHYR_ROLES

    directives = {
        "code-sample": CodeSampleDirective,
        "code-sample-listing": CodeSampleListingDirective,
        "code-sample-category": CodeSampleCategoryDirective,
        "board-catalog": BoardCatalogDirective,
        "board": BoardDirective,
        "board-supported-hw": BoardSupportedHardwareDirective,
        "board-supported-runners": BoardSupportedRunnersDirective,
    }

    object_types: dict[str, ObjType] = {
        "code-sample": ObjType("code sample", "code-sample"),
        "code-sample-category": ObjType("code sample category", "code-sample-category"),
        "board": ObjType("board", "board"),
    }

    initial_data: dict[str, Any] = {
        "code-samples": {},  # id -> code sample data
        "code-samples-categories": {},  # id -> code sample category data
        "code-samples-categories-tree": Node("samples"),
        # keep track of documents containing special directives
        "has_code_sample_listing": {},  # docname -> bool
        "has_board_catalog": {},  # docname -> bool
        "has_board": {},  # docname -> bool
    }

    def clear_doc(self, docname: str) -> None:
        self.data["code-samples"] = {
            sample_id: sample_data
            for sample_id, sample_data in self.data["code-samples"].items()
            if sample_data["docname"] != docname
        }

        self.data["code-samples-categories"] = {
            category_id: category_data
            for category_id, category_data in self.data["code-samples-categories"].items()
            if category_data["docname"] != docname
        }

        # TODO clean up the anytree as well

        self.data["has_code_sample_listing"].pop(docname, None)
        self.data["has_board_catalog"].pop(docname, None)
        self.data["has_board"].pop(docname, None)

    def merge_domaindata(self, docnames: list[str], otherdata: dict) -> None:
        self.data["code-samples"].update(otherdata["code-samples"])
        self.data["code-samples-categories"].update(otherdata["code-samples-categories"])

        # self.data["boards"] contains all the boards right from builder-inited time, but it still
        # potentially needs merging since a board's docname property is set by BoardDirective to
        # indicate the board is documented in a specific document.
        if "boards" in self.data and "boards" in otherdata:
            for board_name, board in otherdata["boards"].items():
                if board_name in self.data["boards"] and "docname" in board:
                    self.data["boards"][board_name]["docname"] = board["docname"]
        elif "boards" in otherdata:  # if self.data["boards"] doesn't exist yet
            self.data["boards"] = otherdata["boards"]

        # merge category trees by adding all the categories found in the "other" tree that to
        # self tree
        other_tree = otherdata["code-samples-categories-tree"]
        categories = [n for n in PreOrderIter(other_tree) if hasattr(n, "category")]
        for category in categories:
            category_path_parts = [
                n.name for n in category.path if n.name != 'samples'
            ]  # Exclude root 'samples' node if present
            category_path = f"/{'/'.join(category_path_parts)}"
            if not category_path_parts:  # Handle root category case if necessary
                category_path = "/"

            self.add_category_to_tree(
                category_path,
                category.category["id"],
                category.category["name"],
                category.category["docname"],
            )

        for docname in docnames:
            self.data["has_code_sample_listing"][docname] = otherdata[
                "has_code_sample_listing"
            ].get(docname, False)
            self.data["has_board_catalog"][docname] = otherdata["has_board_catalog"].get(
                docname, False
            )
            self.data["has_board"][docname] = otherdata["has_board"].get(docname, False)

    def get_objects(self):
        for _, code_sample in self.data["code-samples"].items():
            yield (
                code_sample["id"],
                code_sample["name"],
                "code-sample",
                code_sample["docname"],
                code_sample["id"],
                1,
            )

        for _, code_sample_category in self.data["code-samples-categories"].items():
            yield (
                code_sample_category["id"],
                code_sample_category["name"],
                "code-sample-category",
                code_sample_category["docname"],
                code_sample_category["id"],
                1,
            )

        if "boards" in self.data:
            for _, board in self.data["boards"].items():
                # only boards that do have a documentation page are to be considered as valid objects
                if "docname" in board:
                    yield (
                        board["name"],
                        board["full_name"],
                        "board",
                        board["docname"],
                        board["name"],
                        1,
                    )

    # used by Sphinx Immaterial theme
    def get_object_synopses(self) -> Iterator[tuple[tuple[str, str], str]]:
        for _, code_sample in self.data["code-samples"].items():
            if hasattr(code_sample["description"], "astext"):
                yield (
                    (code_sample["docname"], code_sample["id"]),
                    code_sample["description"].astext(),
                )
            # else provide a default or log a warning if description is not as expected

    def resolve_xref(self, env, fromdocname, builder, type, target, node, contnode):
        elem = None
        if type == "code-sample":
            elem = self.data["code-samples"].get(target)
        elif type == "code-sample-category":
            elem = self.data["code-samples-categories"].get(target)
        elif type == "board" and "boards" in self.data:
            elem = self.data["boards"].get(target)
        else:
            return None  # Explicitly return None if type is not handled

        if elem and "docname" in elem:
            title = elem["name"] if type != "board" else elem["full_name"]
            description = None
            if type == "code-sample" and hasattr(elem["description"], "astext"):
                description = elem["description"].astext()

            if not node.get("refexplicit"):
                contnode = [nodes.Text(title)]

            return make_refnode(
                builder,
                fromdocname,
                elem["docname"],
                elem["id"]
                if type != "board"
                else elem["name"],  # targetid for board should be its name (id)
                contnode,
                title,  # Use title as reftitle for consistency
            )
        return None  # Explicitly return None if element or docname is missing

    def add_code_sample(self, code_sample):
        self.data["code-samples"][code_sample["id"]] = code_sample

    def add_code_sample_category(self, code_sample_category):
        self.data["code-samples-categories"][code_sample_category["id"]] = code_sample_category
        # category_doc_path is derived from the directory of the .rst file defining the category
        category_doc_path = path.dirname(code_sample_category["docname"])
        # Removed: if not category_doc_path: category_doc_path = "/"
        # add_category_to_tree will handle normalization including the empty path case.

        self.add_category_to_tree(
            category_doc_path,
            code_sample_category["id"],
            code_sample_category["name"],
            code_sample_category["docname"],
        )

    def add_category_to_tree(
        self, category_path_from_docname: str, category_id: str, category_name: str, docname: str
    ) -> Node:
        resolver = Resolver("name")
        tree_root = self.data["code-samples-categories-tree"]  # This is Node("samples")

        # Normalize category_path_from_docname
        # "" (from dirname of root file e.g. "index.rst") -> "/"
        # "foo" -> "/foo"
        # "samples/foo" -> "/samples/foo"
        if not category_path_from_docname:
            normalized_category_path = "/"
        elif not category_path_from_docname.startswith("/"):
            normalized_category_path = "/" + category_path_from_docname
        else:
            normalized_category_path = category_path_from_docname

        # Determine path components relative to the tree_root's children
        all_path_parts = [part for part in normalized_category_path.strip("/").split("/") if part]

        path_components_for_creation = []
        if not all_path_parts:
            # Path was "/" or "/samples" (if root is "samples" and path was "samples")
            # Category applies to the tree_root itself.
            path_components_for_creation = []
        elif all_path_parts[0] == tree_root.name:
            # Path starts with root name, e.g., "/samples/foo/bar"
            # Components to create are relative to root: ["foo", "bar"]
            path_components_for_creation = all_path_parts[1:]
        else:
            # Path does not start with root name, e.g., "/subsys/cat"
            # Components are created directly under root: ["subsys", "cat"]
            path_components_for_creation = all_path_parts

        current_parent_node = tree_root
        final_node_for_category = tree_root  # Default if path_components_for_creation is empty

        for component_name in path_components_for_creation:
            try:
                child_node = resolver.get(current_parent_node, component_name)
            except ChildResolverError:
                child_node = Node(component_name, parent=current_parent_node)
            current_parent_node = child_node
            final_node_for_category = child_node

        # Attach category data to the target node
        if (
            hasattr(final_node_for_category, "category")
            and final_node_for_category.category["id"] != category_id
        ):
            raise ValueError(
                f"Can't add code sample category '{category_id}' from doc '{docname}'. "
                f"Node '{final_node_for_category.name}' already has category "
                f"'{final_node_for_category.category['id']}' from doc '{final_node_for_category.category['docname']}'. "
                "You may only have one category per path or need to reconcile definitions."
            )

        final_node_for_category.category = {
            "id": category_id,
            "name": category_name,
            "docname": docname,
        }
        return tree_root  # Return the main tree root
