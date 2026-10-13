#!/usr/bin/env python3
# SPDX-FileCopyrightText: Copyright The Zephyr Project Contributors
# SPDX-License-Identifier: Apache-2.0

"""
Carve a few-class detection dataset out of COCO for ESP-Detection.

Takes COCO 2017 in the Ultralytics YOLO layout (images/ and labels/, with
train2017 and val2017 split directories), keeps the images holding at least one
of the requested classes, renumbers those classes from 0 and writes:

    <output>/images/{train,val}/   links to the kept images
    <output>/labels/{train,val}/   the kept labels, renumbered
    <output>/calib/                validation images to calibrate quantization
    <output>/dataset.yaml          the dataset description for ESP-Detection

Example, for a person and phone detector (COCO classes 0 and 67):

    python coco_subset.py --coco datasets/coco --output datasets/person_phone \\
        --classes 0:person 67:phone
"""

import argparse
import os
import random
import shutil
from pathlib import Path

SPLITS = {"train": "train2017", "val": "val2017"}


def parse_classes(specs):
    classes = {}
    for spec in specs:
        coco_id, _, name = spec.partition(":")
        if not coco_id.isdigit() or not name:
            raise argparse.ArgumentTypeError(f"expected <coco id>:<name>, got {spec}")
        classes[int(coco_id)] = (len(classes), name)
    return classes


def filter_labels(label_file, classes):
    kept = []
    for line in label_file.read_text().splitlines():
        fields = line.split()
        if fields and int(fields[0]) in classes:
            kept.append(" ".join([str(classes[int(fields[0])][0])] + fields[1:]))
    return kept


def link(src, dst):
    if not dst.exists():
        os.symlink(src.resolve(), dst)


def build_split(coco, output, split, classes, max_images):
    src_images = coco / "images" / SPLITS[split]
    src_labels = coco / "labels" / SPLITS[split]
    dst_images = output / "images" / split
    dst_labels = output / "labels" / split
    dst_images.mkdir(parents=True, exist_ok=True)
    dst_labels.mkdir(parents=True, exist_ok=True)

    label_files = sorted(src_labels.glob("*.txt"))
    random.Random(0).shuffle(label_files)
    kept = []
    for label_file in label_files:
        if max_images and len(kept) == max_images:
            break
        lines = filter_labels(label_file, classes)
        if not lines:
            continue
        image = src_images / (label_file.stem + ".jpg")
        (dst_labels / label_file.name).write_text("\n".join(lines) + "\n")
        link(image, dst_images / image.name)
        kept.append(image)

    print(f"{split}: {len(kept)} images")
    return kept


def main():
    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
        allow_abbrev=False,
    )
    parser.add_argument("--coco", type=Path, required=True, help="COCO dataset in YOLO layout")
    parser.add_argument("--output", type=Path, required=True, help="dataset to create")
    parser.add_argument(
        "--classes", nargs="+", required=True, help="classes to keep, as <coco id>:<name>"
    )
    parser.add_argument(
        "--max-train", type=int, default=0, help="limit on training images, 0 for all"
    )
    parser.add_argument(
        "--calib", type=int, default=128, help="validation images kept for calibration"
    )
    args = parser.parse_args()

    classes = parse_classes(args.classes)

    build_split(args.coco, args.output, "train", classes, args.max_train)
    val = build_split(args.coco, args.output, "val", classes, 0)

    calib = args.output / "calib"
    calib.mkdir(parents=True, exist_ok=True)
    for image in val[: args.calib]:
        shutil.copy(image, calib / image.name)

    names = "\n".join(f"  {index}: {name}" for index, name in classes.values())
    (args.output / "dataset.yaml").write_text(
        f"path: {args.output.resolve()}\ntrain: images/train\nval: images/val\n\nnames:\n{names}\n"
    )
    print(f"Wrote {args.output / 'dataset.yaml'}")


if __name__ == "__main__":
    main()
