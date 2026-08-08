# zephyr-series-prs

Splits one long local commit series into per-subsystem draft PRs against
upstream, then keeps telling you what still needs doing.

Single file, stdlib only, needs `python3` and `git`. A GitHub token is only
needed for `open` and `status`.

## Setup

```sh
cd ~/zephyr                     # your clone, with the series checked out
git remote add upstream https://github.com/zephyrproject-rtos/zephyr.git
chmod +x zephyr-series-prs.py
```

`origin` is assumed to be your fork and `upstream` the Zephyr repo. Both are
overridable (`--remote`, `--upstream-remote`, `--upstream`, `--fork`). If there
is no `upstream` remote the tool says so and falls back to `origin/main`.

Token: `--token`, `$GH_TOKEN`, `$GITHUB_TOKEN`, or whatever `gh auth token`
returns, in that order.

## Use

```sh
./zephyr-series-prs.py plan          # how the series gets cut up
./zephyr-series-prs.py plan -v       # ... plus the full PR title and body of each
./zephyr-series-prs.py verify        # do all batches apply on their own?
./zephyr-series-prs.py diff net      # the exact diff that batch's PR will show
./zephyr-series-prs.py diff net --stat
./zephyr-series-prs.py push --yes    # build + push batch branches to your fork
./zephyr-series-prs.py open --yes    # open a draft PR per batch
./zephyr-series-prs.py update --yes  # push edited descriptions to open PRs

./zephyr-series-prs.py status        # what needs attention (run this any time)

# one batch at a time (repeatable; name with or without the branch prefix)
./zephyr-series-prs.py push --only input --yes
./zephyr-series-prs.py open --only input --yes
```

`push`, `open` and `update` are dry runs without `--yes`. Nothing is ever pushed
to upstream: branches go to your fork, PRs are opened from there.

## Web UI

```sh
./zephyr-series-prs.py serve      # http://127.0.0.1:8765, opens a browser
```

A dashboard over the same code: every batch with its maintainers, PR number,
state, CI and next action; filter by branch, maintainer or area; and run
verify / push / open / update / refresh from the toolbar, with the output
streamed into a log pane.

Click a row to expand it, with three tabs:

- **commits** the commits in the batch
- **diff** the exact patch the PR will show, syntax-coloured, fetched on
  demand and cached until the plan changes
- **body** the rendered PR description

and buttons to **push this** / **open this PR** / **update this text** for that
one batch, so you can land them one at a time rather than all at once.

To act on several, tick the checkboxes (the header checkbox selects everything
currently matching the filter) and use the toolbar buttons: they apply to the
selection, or to every batch when nothing is selected. The header shows how
many are selected and the confirmation names the scope.

Mutating actions are dry runs until you tick **apply changes**, and then still
ask for confirmation. The server binds to localhost only and every API call
carries a per-run nonce, so a random page in another browser tab cannot drive
it. `--host`, `--port` and `--no-browser` are available.

No build step, no dependencies: the page is one embedded HTML file served from
the script itself.

`status` is the one to keep coming back to:

```
BRANCH                                CMTS  PR      STATE            CI        NEXT ACTION
doxygen/drivers-sensor                  20  #1234   open             passing
doxygen/net-coap                         3  #1240   draft (behind)   passing   push (base moved)
doxygen/sys                             34  -       pushed           -         open PR
doxygen/usb                              8  -       not pushed       -         push
doxygen/bluetooth                       34  #1251   merged           -
doxygen/old-thing                        -  #1249   orphaned         -         close PR and delete branch
```

## Where the PR titles and descriptions live

In an editable template file. Scaffold it with:

```sh
./zephyr-series-prs.py templates      # writes .git/zephyr-series-prs-templates.md
```

Edit that file, then `./zephyr-series-prs.py plan -v` to preview the rendered
text, and `open --yes` (or `update --yes` for PRs already open) to apply it. If
the file does not exist the built-in defaults are used, so you can ignore all of
this until you want to change the wording. `--templates FILE` points elsewhere.

The format is one section per batch:

```markdown
## default
title: {scope}: improve Doxygen coverage

Part of an ongoing sweep to improve Doxygen coverage of the public API headers.

Documentation only, no functional change.

## doxygen/drivers-sensor
title: drivers: sensor: document custom attributes and streaming

Body for this one batch only.
```

Three sections ship by default: `default`, `area-assorted` (leftovers within one
area, e.g. `doxygen/drivers-assorted-1`) and `assorted` (leftovers pooled across
unrelated areas, which have no single area to name). A section named after a
branch overrides everything for that batch.

Placeholders: `{scope}` `{area}` `{areas}` `{branch}` `{count}` `{commits}`
`{base}` `{total}` `{upstream}` `{maintainers}`. The shipped templates keep the
body short and deliberately do not list the commits, since GitHub already shows
them; `{commits}` is there if you want them anyway. Avoid `{maintainers}` in a
body: it pings people on every edit, and `plan`/`status` show them already.

`update` re-renders every open PR and PATCHes only the ones whose text actually
changed, so it is cheap to run after editing templates or reworking commits.

## How batches are chosen

By default (`--group-by maintainers`), by who owns the files in
`MAINTAINERS.yml`, so each PR lands on one reviewer's plate. This reuses the
repo's own `scripts/get_maintainer.py`, so the file-to-area mapping is exactly
the one CI and the assignment bot use.

Each commit's files map to MAINTAINERS areas, and commits are grouped by the
resulting **maintainer set**. That has two useful effects:

- sibling areas with a shared maintainer collapse into one PR (all of
  networking in a single PR for its maintainers, rather than one per protocol)
- a commit spanning two maintainers' areas gets its own batch instead of
  quietly dragging an extra reviewer onto an unrelated PR

Areas with no maintainer but with collaborators group by collaborator set,
since those are the people who actually review them. Areas with neither are
pooled into an `unowned` batch, because no routing decision depends on them.

`plan`, `status` and the web UI print the handles per batch so you can see the
routing. The shipped PR bodies deliberately do not mention them, so editing a
description does not ping anyone.

**Commits touching the same file are always kept in the same batch.** They are
unioned into a component before grouping, and the component takes the union of
its owners. Without this, a commit that edits a block created by an earlier
commit can land in a different maintainer's batch and then fail to apply on its
own. `verify` proves the result: it builds every branch and reports any that
does not apply.

On a 414-commit series this yields 96 batches, every one of which applies
cleanly on top of the base, with no file appearing in two batches.

With `--group-by scope` the old behaviour is available: group by commit subject
scope (`drivers: sensor:`, `net: coap:`), splitting oversized areas one level
deeper. That mode uses:

- `--max-commits N` (default 40) split an area bigger than this one level deeper
- `--split-threshold N` (default 3) sub-areas smaller than this fold into
  `<area>-assorted`
- `--min-commits N` (default 3) areas smaller than this are pooled into shared
  `assorted` batches; use `--min-commits 1` for one PR per area

`--max-commits` applies in both modes. Branch names are `<prefix>/<batch-key>`
(`--branch-prefix`, default `doxygen`).

If `MAINTAINERS.yml` or PyYAML is missing the tool says so and falls back to
scope grouping.

## Reworking commits later

Just rework the source branch and re-run. Batch membership is recomputed every
time, so amending, reordering, splitting, dropping or re-scoping commits is
fine.

Each batch is fingerprinted with `git patch-id --stable`, which ignores commit
SHAs, author dates and line offsets. So:

- **rebased your series onto newer main** -> `stale base`, action `push (base
  moved)`. Not reported as a content change.
- **actually edited a commit** -> `drifted`, action `push (content changed)`.
  Only the affected batch is rebuilt.
- **re-scoped or dropped commits so a batch vanished** -> its branch shows up
  as `orphaned` so you can close the PR instead of leaving it dangling.

`push` only rebuilds batches that changed or whose base moved (`--force` for
all of them), and uses `--force-with-lease`. Branch names are deterministic, so
the same subsystem always lands on the same branch and therefore the same PR.

Merged and closed batches are left alone unless you pass `--include-closed`.

## This tool's own commit

`--exclude-paths` drops commits that only touch the given path prefixes, and it
defaults to `scripts/series-prs`. So when this tool is committed on the working
branch alongside the series, it is skipped rather than turned into a PR
proposing itself to Zephyr. The option is repeatable, so pass it again for any
other local-only commits you keep on the branch.

## State

Kept in `.git/zephyr-series-prs.json` (`--state` to move it). It records the PR
number, last pushed head, base SHA and fingerprint per batch. Deleting it is
safe: `open` re-adopts existing PRs by matching the branch name, and `push`
will just rebuild everything once.

## Implementation note

Branches are built with `git merge-tree` + `git commit-tree`, so batches are
assembled straight in the object database. Your working tree, index and HEAD
are never touched, and there is no checkout, which is why building 45 branches
off a 400-commit series takes a few seconds. On git older than 2.38 it falls
back to a throwaway worktree.
