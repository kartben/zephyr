import os
import re

def find_defgroups_without_since(directory):
    # Pattern to match @defgroup and @since tags
    defgroup_pattern = re.compile(r"(/\*\*[^*]*\*+(?:[^/*][^*]*\*+)*/)")
    since_pattern = re.compile(r"@since")

    files_without_since = []

    # Walk through the directory and process each .h file
    for root, _, files in os.walk(directory):
        for file in files:
            if file.endswith(".h"):
                file_path = os.path.join(root, file)
                with open(file_path, 'r', encoding='utf-8') as f:
                    content = f.read()

                    # Find all comment blocks with @defgroup
                    for match in defgroup_pattern.findall(content):
                        if '@defgroup' in match and not since_pattern.search(match):
                            files_without_since.append(file_path)
                            break

    # Print results
    if files_without_since:
        print("Files with @defgroup but missing @since:")
        for file in files_without_since:
            print(file)
    else:
        print("All @defgroup tags have an associated @since tag.")

# Specify the path to the include/zephyr directory
include_dir = "include/zephyr"
find_defgroups_without_since(include_dir)
