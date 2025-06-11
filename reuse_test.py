import os
from reuse.project import Project


def list_copyrights(folder):
    project = Project(folder)
    all_files = project.all_files()

    for file_path in sorted(all_files):
        try:
            infos = project.reuse_info_of(file_path)
            print(f"File: {file_path}")
            found = False
            for info in infos:
                if info.copyright_lines:
                    for c in info.copyright_lines:
                        print(f"  Copyright: {c}")
                    found = True
            if not found:
                print("  No copyright found.")
            print()
        except Exception as e:
            print(f"Error processing {file_path}: {e}")


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 2:
        print("Usage: python reuse_test.py <folder>")
    else:
        list_copyrights(sys.argv[1])
