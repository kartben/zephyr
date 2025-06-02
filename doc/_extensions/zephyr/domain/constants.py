from pathlib import Path

# Path to the root of the Zephyr project
ZEPHYR_BASE = Path(__file__).resolve().parents[4]

# Path to the domain's templates and static resources directories
TEMPLATES_DIR = Path(__file__).resolve().parent / "templates"
RESOURCES_DIR = Path(__file__).resolve().parent / "static"

# Path to binding-types.txt, depends on ZEPHYR_BASE
BINDINGS_TXT_PATH = ZEPHYR_BASE / "dts" / "bindings" / "binding-types.txt"
