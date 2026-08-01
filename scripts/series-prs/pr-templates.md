# PR title and body templates for zephyr-series-prs.
#
# One section per batch, introduced by a line starting with "## ". The section
# named "default" is used for any batch without its own section; add a section
# named after a branch (for example "## doxygen/drivers-sensor") to override
# the text for that one batch.
#
# The first line of a section is "title: ...". Everything after the following
# blank line is the PR body, in Markdown.
#
# Placeholders:
#   {scope}    full scope of the batch, e.g. "drivers: sensor"
#   {area}     top-level area only, e.g. "drivers"
#   {branch}   batch branch name
#   {count}    number of commits in the batch
#   {commits}  bullet list of the commit subjects (GitHub already shows the
#              commits, so the shipped templates do not use this)
#   {base}     upstream base branch
#   {total}    total number of batches in the series
#   {upstream} upstream repo, e.g. zephyrproject-rtos/zephyr
#   {areas}    MAINTAINERS.yml areas the batch touches
#   {maintainers} GitHub handles that own those areas. Careful: putting these
#              in a body pings them on every edit; they are shown by `plan`
#              and `status` anyway.
#
# Lines outside a section, and lines starting with "#" before the first
# section, are comments and are ignored.

## default
title: {scope}: improve Doxygen coverage

Part of an ongoing sweep to improve Doxygen coverage of the public API headers.

Documentation only: adds missing `@file` blocks and group membership, documents
symbols whose meaning is unambiguous, and hides constructs that are effectively
internal. No functional change.

## area-assorted
title: {area}: improve Doxygen coverage (assorted headers)

Part of an ongoing sweep to improve Doxygen coverage of the public API headers.
This batch collects the {area} sub-areas that only had a commit or two each.

Documentation only: adds missing `@file` blocks and group membership, documents
symbols whose meaning is unambiguous, and hides constructs that are effectively
internal. No functional change.

## assorted
title: include: improve Doxygen coverage in assorted subsystems

Part of an ongoing sweep to improve Doxygen coverage of the public API headers.
This batch collects the subsystems that only had a commit or two each, across
{scope}.

Documentation only: adds missing `@file` blocks and group membership, documents
symbols whose meaning is unambiguous, and hides constructs that are effectively
internal. No functional change.
