#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
"""
zephyr-series-prs: split one long local commit series into per-subsystem draft PRs.

Takes a local branch holding many commits, groups them into batches by who owns
the touched files in MAINTAINERS.yml (so each PR lands on the right reviewer),
builds one branch per batch directly on top of upstream main, pushes those
branches to your fork, and opens a draft PR per batch against upstream. It then
keeps reporting the state of every batch so you can see what still needs
opening, what drifted and needs a re-push, and what needs a rebase.

Commits that touch the same file are always kept in one batch, so every PR
applies on top of the base on its own. Use --group-by scope to group by commit
subject prefix instead.

Nothing is written anywhere until you pass --yes. Every mutating command is a
dry run by default, and the tool never pushes to the upstream repository: it
pushes branches to your fork and opens PRs from there.

Quick start
-----------
    # See how the series would be cut up, and the state of anything already open
    ./zephyr-series-prs.py status

    # Build + push the batch branches to your fork
    ./zephyr-series-prs.py push --yes

    # Open the draft PRs (edit the text first with: templates, then plan -v)
    ./zephyr-series-prs.py open --yes

    # Later, after reworking commits on the source branch, re-run:
    ./zephyr-series-prs.py status
    ./zephyr-series-prs.py push --yes     # only batches that actually changed

Resilience to reworked commits
------------------------------
Batch membership is recomputed from the source branch on every run, so amending,
reordering, splitting or dropping commits is fine. Each batch is fingerprinted
with `git patch-id --stable`, which ignores commit SHAs, author dates and
context line numbers. That means a plain rebase of your source branch does NOT
count as a change, while an actual content edit does. Branch names are derived
deterministically from the batch key, so the same subsystem always maps to the
same branch and therefore the same PR.

PR titles and bodies come from a template file you can edit; run the
`templates` command to write out the defaults, `plan -v` to preview the
rendered text, and `update` to push edits to PRs that are already open.

Requires: python3 and git. A GitHub token is only needed for the commands that
talk to GitHub (`open`, `update`, `status`); it is read from --token,
$GH_TOKEN, $GITHUB_TOKEN, or `gh auth token`.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from dataclasses import dataclass, field
from typing import Iterable

DEFAULT_UPSTREAM = "zephyrproject-rtos/zephyr"
DEFAULT_BASE = "main"
DEFAULT_BRANCH_PREFIX = "doxygen"
STATE_BASENAME = "zephyr-series-prs.json"
TOOL_DIR = "scripts/series-prs"  # this tool lives here when committed in-tree
SPLITS_BASENAME = "zephyr-series-prs-splits.json"

# Subjects are "area: subarea: summary". Anything before the last colon-separated
# scope token is treated as scope. A token is only scope-like if it is short and
# has no sentence punctuation, which keeps "Bluetooth: document FOO: bar" sane.
SCOPE_TOKEN_RE = re.compile(r"^[A-Za-z0-9_+./-]+$")


# --------------------------------------------------------------------------- #
# small helpers
# --------------------------------------------------------------------------- #


class Fatal(Exception):
    pass


def git(*args: str, cwd: str | None = None, check: bool = True) -> str:
    proc = subprocess.run(
        ["git", *args], cwd=cwd, capture_output=True, text=True
    )
    if check and proc.returncode != 0:
        raise Fatal(f"git {' '.join(args)} failed:\n{proc.stderr.strip()}")
    return proc.stdout.strip()


def _text_hash(title: str, body: str) -> str:
    """Fingerprint of the rendered PR text, so we only PATCH when it changed."""
    return hashlib.sha256(f"{title}\n{body}".encode()).hexdigest()[:16]


def slug(text: str) -> str:
    text = text.strip().lower()
    text = re.sub(r"[^a-z0-9]+", "-", text)
    return text.strip("-")


def c(text: str, colour: str) -> str:
    if not sys.stdout.isatty() or os.environ.get("NO_COLOR"):
        return text
    codes = {
        "red": "31", "green": "32", "yellow": "33", "blue": "34",
        "magenta": "35", "cyan": "36", "grey": "90", "bold": "1",
    }
    return f"\033[{codes[colour]}m{text}\033[0m"


# --------------------------------------------------------------------------- #
# model
# --------------------------------------------------------------------------- #


@dataclass
class Commit:
    sha: str
    subject: str
    scope: list[str]
    patch_id: str = ""

    @property
    def summary(self) -> str:
        """Subject with the scope prefix stripped."""
        prefix_len = sum(len(t) + 2 for t in self.scope)
        return self.subject[prefix_len:] if prefix_len else self.subject


@dataclass
class Batch:
    key: str
    scopes: list[str]
    commits: list[Commit] = field(default_factory=list)
    areas: list[str] = field(default_factory=list)        # MAINTAINERS.yml areas
    maintainers: list[str] = field(default_factory=list)  # GitHub handles

    @property
    def branch(self) -> str:
        return self.key  # prefix is applied by Planner

    def fingerprint(self) -> str:
        return " ".join(cm.patch_id for cm in self.commits)

    def common_scope(self) -> str:
        """Longest scope prefix shared by every commit, for the PR title.

        Titles have to read like Zephyr subjects ("net: coap: ..."), so they
        come from the commits themselves rather than from the MAINTAINERS area
        name, which is prose ("Networking") and would look wrong as a prefix.
        """
        scopes = [cm.scope for cm in self.commits if cm.scope]
        if not scopes:
            return "include"
        common: list[str] = []
        for tokens in zip(*scopes):
            if len(set(tokens)) != 1:
                break
            common.append(tokens[0])
        if common:
            return ": ".join(common)
        # No shared prefix (a maintainer owning areas that use different
        # subject prefixes). Fall back to the one most of the commits use.
        tally: dict[str, int] = {}
        for sc in scopes:
            tally[sc[0]] = tally.get(sc[0], 0) + 1
        return max(tally, key=lambda k: (tally[k], -len(k)))


# state values reported per batch
ST_NEW = "needs-open"
ST_OPEN = "open"
ST_MERGED = "merged"
ST_CLOSED = "closed"
ST_CONFLICT = "conflicts"


# --------------------------------------------------------------------------- #
# batching
# --------------------------------------------------------------------------- #


def parse_scope(subject: str) -> list[str]:
    """Split a Zephyr subject into its scope tokens.

    "drivers: sensor: document foo"  -> ["drivers", "sensor"]
    "Bluetooth: Mesh: hide bar"      -> ["Bluetooth", "Mesh"]
    "sys: document baz"              -> ["sys"]
    """
    parts = subject.split(": ")
    scope: list[str] = []
    for token in parts[:-1]:
        token = token.strip()
        if not token or len(token) > 24 or not SCOPE_TOKEN_RE.match(token):
            break
        scope.append(token)
    return scope


def load_commits(repo: str, base: str, head: str) -> list[Commit]:
    rng = f"{base}..{head}"
    raw = git("log", "--reverse", "--format=%H%x1f%s", rng, cwd=repo)
    commits: list[Commit] = []
    for line in raw.splitlines():
        if not line.strip():
            continue
        sha, subject = line.split("\x1f", 1)
        commits.append(Commit(sha=sha, subject=subject, scope=parse_scope(subject)))
    if not commits:
        raise Fatal(f"no commits in range {rng}")

    # patch-ids in one batch call: stable across rebases, sensitive to real edits
    diff = subprocess.run(
        ["git", "log", "--reverse", "--format=%H", "-p", "--no-color", rng],
        cwd=repo, capture_output=True, text=True, check=True,
    )
    pid = subprocess.run(
        ["git", "patch-id", "--stable"],
        cwd=repo, input=diff.stdout, capture_output=True, text=True, check=True,
    )
    mapping = {}
    for line in pid.stdout.splitlines():
        bits = line.split()
        if len(bits) == 2:
            mapping[bits[1]] = bits[0]
    for cm in commits:
        # a commit with an empty diff has no patch-id; fall back to its subject
        cm.patch_id = mapping.get(cm.sha, "empty:" + slug(cm.subject))
    return commits


# --------------------------------------------------------------------------- #
# MAINTAINERS.yml
# --------------------------------------------------------------------------- #


def load_maintainers(repo: str):
    """Return zephyr's own Maintainers object, or None if unavailable.

    Reuses scripts/get_maintainer.py so the file -> area mapping is exactly the
    one CI and the assignment bot use, glob semantics and all. That module
    imports `tabulate` purely for its CLI output, so a stub is enough when it
    is not installed.
    """
    script = os.path.join(repo, "scripts", "get_maintainer.py")
    yml = os.path.join(repo, "MAINTAINERS.yml")
    if not (os.path.exists(script) and os.path.exists(yml)):
        return None
    try:
        import importlib.util
        import types
        sys.modules.setdefault(
            "tabulate", types.SimpleNamespace(tabulate=lambda *a, **k: "")
        )
        spec = importlib.util.spec_from_file_location("_zsp_gm", script)
        mod = importlib.util.module_from_spec(spec)
        sys.modules["_zsp_gm"] = mod
        spec.loader.exec_module(mod)
        return mod.Maintainers(yml)
    except Exception as exc:  # missing pyyaml, or upstream refactor
        print(c(f"note: cannot use MAINTAINERS.yml ({exc}); "
                f"falling back to --group-by scope", "yellow"))
        return None


def commit_files(repo: str, base: str, head: str) -> dict[str, list[str]]:
    """Map commit sha -> changed files, in one pass over the range."""
    out = git("log", "--reverse", "--format=%x01%H", "--name-only",
              "--no-renames", f"{base}..{head}", cwd=repo)
    files: dict[str, list[str]] = {}
    sha = None
    for line in out.splitlines():
        if line.startswith("\x01"):
            sha = line[1:].strip()
            files[sha] = []
        elif line.strip() and sha:
            files[sha].append(line.strip())
    return files


@dataclass
class Ownership:
    areas: tuple[str, ...] = ()
    maintainers: tuple[str, ...] = ()
    collaborators: tuple[str, ...] = ()

    @property
    def owners(self) -> tuple[str, ...]:
        """Who would end up reviewing: maintainers, else collaborators."""
        return self.maintainers or self.collaborators

    @property
    def signature(self) -> tuple:
        """What decides who reviews.

        Maintainers first, since they are the assignees. Several areas have no
        maintainer but do have collaborators who review them, so those come
        next. Areas with neither are grouped by area and later pooled, because
        no routing decision depends on them.
        """
        if self.maintainers:
            return ("m",) + self.maintainers
        if self.collaborators:
            return ("c",) + self.collaborators
        return ("unowned",)


def compute_ownership(repo: str, commits: list[Commit], base: str,
                      head: str, maint) -> dict[str, Ownership]:
    files = commit_files(repo, base, head)
    cache: dict[str, tuple[tuple[str, ...], ...]] = {}
    owners: dict[str, Ownership] = {}
    for cm in commits:
        areas: set[str] = set()
        people: set[str] = set()
        collab: set[str] = set()
        for path in files.get(cm.sha, []):
            if path not in cache:
                hits = maint.path2areas(path)
                cache[path] = (
                    tuple(a.name for a in hits),
                    tuple(m for a in hits for m in a.maintainers),
                    tuple(x for a in hits for x in a.collaborators),
                )
            a, p, co = cache[path]
            areas.update(a)
            people.update(p)
            collab.update(co)
        owners[cm.sha] = Ownership(tuple(sorted(areas)), tuple(sorted(people)),
                                   tuple(sorted(collab)))
    return owners


ASSORTED = "assorted"  # suffix for coalesced leftovers; never a real scope


def _pack(groups: list[list[Commit]], size: int) -> list[list[Commit]]:
    """Greedily fill chunks of at most `size`, never splitting a group.

    Commits that touch the same files almost always share a scope, so keeping a
    scope group intact is what stops one batch from depending on another. A
    single group larger than `size` becomes its own oversized chunk: a big PR
    beats a PR that cannot apply on its own.
    """
    chunks: list[list[Commit]] = []
    cur: list[Commit] = []
    for group in groups:
        if cur and len(cur) + len(group) > size:
            chunks.append(cur)
            cur = []
        cur.extend(group)
    if cur:
        chunks.append(cur)
    return chunks


def make_batches_by_maintainer(
    commits: list[Commit],
    owners: dict[str, Ownership],
    files: dict[str, list[str]],
    max_commits: int,
    prefix: str,
) -> list[Batch]:
    """One batch per maintainer set, so each PR lands on one reviewer's plate.

    Commits are keyed by the set of people MAINTAINERS.yml puts on the files
    they touch. That merges sibling areas with a shared maintainer into a
    single PR (all of networking rather than one PR per protocol), and keeps a
    commit that spans two maintainers' areas in its own batch rather than
    silently dragging an extra reviewer onto an unrelated PR.

    Batches are named after the area they mostly touch, since a branch called
    after a GitHub handle would be unreadable, with a numeric suffix if two
    different maintainer sets share a dominant area name.
    """
    order = {cm.sha: i for i, cm in enumerate(commits)}

    # Two commits that touch the same file must ship together, otherwise the
    # second one cannot apply without the first. Union them into components
    # before grouping, and give the component the union of its owners: a
    # commit editing one file can otherwise land in a different maintainer's
    # batch than the commit that created the block it edits.
    parent: dict[str, str] = {cm.sha: cm.sha for cm in commits}

    def find(x: str) -> str:
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x

    def union(a: str, b: str) -> None:
        ra, rb = find(a), find(b)
        if ra != rb:
            parent[max(ra, rb, key=lambda s: order[s])] = min(
                ra, rb, key=lambda s: order[s]
            )

    seen_file: dict[str, str] = {}
    for cm in commits:
        for path in files.get(cm.sha, []):
            if path in seen_file:
                union(seen_file[path], cm.sha)
            else:
                seen_file[path] = cm.sha

    components: dict[str, list[Commit]] = {}
    for cm in commits:
        components.setdefault(find(cm.sha), []).append(cm)

    def merged_signature(items: list[Commit]) -> tuple:
        maint = sorted({m for cm in items for m in owners[cm.sha].maintainers})
        if maint:
            return ("m",) + tuple(maint)
        collab = sorted({x for cm in items for x in owners[cm.sha].collaborators})
        if collab:
            return ("c",) + tuple(collab)
        return ("unowned",)

    groups: dict[tuple, list[Commit]] = {}
    for members in components.values():
        members.sort(key=lambda cm: order[cm.sha])
        groups.setdefault(merged_signature(members), []).extend(members)

    batches: list[Batch] = []
    used: set[str] = set()

    def name_for(items: list[Commit]) -> tuple[str, list[str]]:
        """Slug from the most-touched area, plus the areas covered."""
        tally: dict[str, int] = {}
        for cm in items:
            for area in owners[cm.sha].areas:
                tally[area] = tally.get(area, 0) + 1
        if not tally:
            # nothing in MAINTAINERS.yml covers these files
            scope = items[0].scope[0] if items[0].scope else "misc"
            return slug(scope), [scope]
        areas = sorted(tally, key=lambda a: (-tally[a], a.lower()))
        return slug(areas[0]), areas

    # Areas with neither a maintainer nor a collaborator route to nobody, so
    # keeping them as separate one-commit PRs buys nothing. Pool them, but
    # keep each area's commits together so the batches still apply on their own.
    unowned = groups.pop(("unowned",), [])
    if unowned:
        by_area: dict[tuple, list[Commit]] = {}
        for cm in sorted(unowned, key=lambda cm: order[cm.sha]):
            by_area.setdefault(owners[cm.sha].areas, []).append(cm)
        for idx, chunk in enumerate(_pack(list(by_area.values()), max_commits), 1):
            groups[("unowned", idx)] = chunk

    for sig in sorted(groups, key=lambda s: order[groups[s][0].sha]):
        items = sorted(groups[sig], key=lambda cm: order[cm.sha])
        base_name, areas = name_for(items)
        if sig[0] == "unowned":
            base_name = "unowned" if len(groups) == 1 or sig[1] == 1 else f"unowned-{sig[1]}"
        comps: dict[str, list[Commit]] = {}
        for cm in items:
            comps.setdefault(find(cm.sha), []).append(cm)
        chunks = _pack(sorted(comps.values(), key=lambda g: order[g[0].sha]),
                       max_commits)
        for idx, chunk in enumerate(chunks, 1):
            name = base_name if len(chunks) == 1 else f"{base_name}-{idx}"
            n = 2
            while f"{prefix}/{name}" in used:
                name = f"{base_name}-{n}" if len(chunks) == 1 else f"{base_name}-{idx}-{n}"
                n += 1
            used.add(f"{prefix}/{name}")
            people = sorted({p for cm in chunk for p in owners[cm.sha].owners})
            batches.append(Batch(key=f"{prefix}/{name}", scopes=areas,
                                 commits=chunk, areas=areas, maintainers=people))

    batches.sort(key=lambda b: order[b.commits[0].sha])
    return batches


def make_batches(
    commits: list[Commit],
    max_commits: int,
    split_threshold: int,
    min_commits: int,
    prefix: str,
) -> list[Batch]:
    """Group commits by scope into reviewable, independently mergeable batches.

    Areas with more than `max_commits` commits are re-split on their second
    scope token; sub-groups smaller than `split_threshold` are folded into an
    "<area>-assorted" batch so we do not emit a swarm of one-commit PRs. Areas
    with fewer than `min_commits` commits are pooled across areas into shared
    "assorted" batches. Any bucket still over `max_commits` is cut into
    numbered chunks.
    """
    by_area: dict[str, list[Commit]] = {}
    for cm in commits:
        area = cm.scope[0] if cm.scope else "misc"
        by_area.setdefault(area, []).append(cm)

    batches: list[Batch] = []
    order = {cm.sha: i for i, cm in enumerate(commits)}

    def add(key: str, scopes: Iterable[str], items: list[Commit]) -> None:
        batches.append(Batch(key=f"{prefix}/{key}", scopes=sorted(set(scopes)),
                             commits=sorted(items, key=lambda cm: order[cm.sha])))

    pooled_groups: list[list[Commit]] = []
    for area in sorted(by_area, key=lambda a: a.lower()):
        items = sorted(by_area[area], key=lambda cm: order[cm.sha])

        if len(items) < min_commits:
            pooled_groups.append(items)
            continue

        if len(items) <= max_commits:
            add(slug(area), [area], items)
            continue

        by_sub: dict[str, list[Commit]] = {}
        for cm in items:
            sub = cm.scope[1] if len(cm.scope) > 1 else ""
            by_sub.setdefault(sub, []).append(cm)

        leftovers: list[list[Commit]] = []
        for sub in sorted(by_sub, key=lambda s: s.lower()):
            group = by_sub[sub]
            if sub and len(group) >= split_threshold:
                add(f"{slug(area)}-{slug(sub)}", [f"{area}: {sub}"], group)
            else:
                leftovers.append(sorted(group, key=lambda cm: order[cm.sha]))

        if leftovers:
            leftovers.sort(key=lambda g: order[g[0].sha])
            chunks = _pack(leftovers, max_commits)
            for idx, chunk in enumerate(chunks, 1):
                suffix = ASSORTED if len(chunks) == 1 else f"{ASSORTED}-{idx}"
                add(f"{slug(area)}-{suffix}", [area], chunk)

    if pooled_groups:
        pooled_groups.sort(key=lambda g: order[g[0].sha])
        chunks = _pack(pooled_groups, max_commits)
        for idx, chunk in enumerate(chunks, 1):
            key = ASSORTED if len(chunks) == 1 else f"{ASSORTED}-{idx}"
            scopes = {cm.scope[0] if cm.scope else "misc" for cm in chunk}
            add(key, scopes, chunk)

    # A real "<area>: assorted:" scope would collide with a leftover bucket.
    # Nothing in Zephyr uses it today, but fail loudly rather than silently
    # pushing two batches onto one branch.
    seen: dict[str, Batch] = {}
    for b in batches:
        if b.key in seen:
            raise Fatal(
                f"branch name collision on '{b.key}'. Rename the scope or pass "
                f"a different --branch-prefix."
            )
        seen[b.key] = b

    # keep overall ordering close to the original series for readability
    batches.sort(key=lambda b: order[b.commits[0].sha])
    return batches


# --------------------------------------------------------------------------- #
# PR text
# --------------------------------------------------------------------------- #


TEMPLATES_BASENAME = "zephyr-series-prs-templates.md"

# Shipped defaults. `templates` writes these to a file you can edit; if that
# file exists it wins. Placeholders are documented in the header below.
DEFAULT_TEMPLATES = """\
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
"""


def templates_path(repo: str, override: str | None) -> str:
    """--templates, else the per-clone copy, else the one committed in-tree."""
    if override:
        return override
    git_dir = git("rev-parse", "--git-dir", cwd=repo)
    if not os.path.isabs(git_dir):
        git_dir = os.path.join(repo, git_dir)
    local = os.path.join(git_dir, TEMPLATES_BASENAME)
    if os.path.exists(local):
        return local
    in_tree = os.path.join(repo, TOOL_DIR, "pr-templates.md")
    return in_tree if os.path.exists(in_tree) else local


def parse_templates(text: str) -> dict[str, tuple[str, str]]:
    """Parse the template file into {section: (title, body)}."""
    sections: dict[str, tuple[str, str]] = {}
    name: str | None = None
    title = ""
    body: list[str] = []

    def flush() -> None:
        if name:
            sections[name] = (title, "\n".join(body).strip("\n"))

    for line in text.splitlines():
        if line.startswith("## "):
            flush()
            name, title, body = line[3:].strip(), "", []
            continue
        if name is None:
            continue  # header comments
        if not title:
            if line.lower().startswith("title:"):
                title = line.split(":", 1)[1].strip()
            continue
        body.append(line)
    flush()
    if "default" not in sections:
        raise Fatal("template file has no '## default' section")
    return sections


def load_templates(repo: str, override: str | None) -> dict[str, tuple[str, str]]:
    path = templates_path(repo, override)
    if os.path.exists(path):
        with open(path, encoding="utf-8") as fh:
            return parse_templates(fh.read())
    return parse_templates(DEFAULT_TEMPLATES)


def template_key(batch: Batch) -> str:
    """Which template section applies: a batch-specific one, or a shared kind.

    Leftovers pooled across unrelated areas ("assorted-2") have no meaningful
    area, so they get their own section; leftovers within one area
    ("drivers-assorted-1") keep that area in the title.
    """
    tail = batch.key.split("/")[-1]
    if tail == ASSORTED or re.fullmatch(rf"{ASSORTED}-\d+", tail):
        return "assorted"
    if tail.endswith(f"-{ASSORTED}") or re.search(rf"-{ASSORTED}-\d+$", tail):
        return "area-assorted"
    return "default"


def render(batch: Batch, templates: dict[str, tuple[str, str]],
           base: str, total: int, upstream: str) -> tuple[str, str]:
    """Render (title, body) for a batch from the templates."""
    key = template_key(batch)
    title_tpl, body_tpl = templates.get(
        batch.branch, templates.get(key, templates["default"])
    )
    scope = batch.common_scope() if batch.areas else (
        batch.scopes[0] if batch.scopes else "misc")
    fields = {
        # For a cross-area batch "scope" is the list of areas it covers, which
        # is what its template wants to name; otherwise it is the single scope.
        "scope": ", ".join(batch.scopes) if key == "assorted" else scope,
        "area": scope.split(":")[0].strip(),
        "areas": ", ".join(batch.areas) if batch.areas else "n/a",
        "maintainers": ", ".join(batch.maintainers) if batch.maintainers
                       else "no maintainer listed",
        "branch": batch.branch,
        "count": len(batch.commits),
        "commits": "\n".join(f"- {cm.subject}" for cm in batch.commits),
        "base": base,
        "total": total,
        "upstream": upstream,
    }
    try:
        return title_tpl.format(**fields), body_tpl.format(**fields)
    except KeyError as exc:
        raise Fatal(f"unknown placeholder {exc} in template for {batch.branch}") from exc


def cmd_templates(args) -> int:
    """Write the default templates out so they can be edited."""
    path = templates_path(args.repo, args.templates)
    if os.path.exists(path) and not args.force:
        print(f"{path} already exists (use --force to overwrite)")
        return 1
    with open(path, "w", encoding="utf-8") as fh:
        fh.write(DEFAULT_TEMPLATES)
    print(f"wrote {path}\n\nEdit it and re-run `open` (or `update` for PRs that "
          f"are already open).")
    return 0


# --------------------------------------------------------------------------- #
# GitHub
# --------------------------------------------------------------------------- #


class GitHub:
    def __init__(self, token: str | None):
        self.token = token

    @staticmethod
    def discover_token(explicit: str | None) -> str | None:
        if explicit:
            return explicit
        for env in ("GH_TOKEN", "GITHUB_TOKEN"):
            if os.environ.get(env):
                return os.environ[env]
        if shutil.which("gh"):
            proc = subprocess.run(["gh", "auth", "token"],
                                  capture_output=True, text=True)
            if proc.returncode == 0 and proc.stdout.strip():
                return proc.stdout.strip()
        return None

    def request(self, method: str, path: str, payload: dict | None = None) -> dict | list:
        if not self.token:
            raise Fatal(
                "no GitHub token found. Pass --token, set $GH_TOKEN, or run "
                "`gh auth login`."
            )
        url = path if path.startswith("http") else f"https://api.github.com{path}"
        data = json.dumps(payload).encode() if payload is not None else None
        req = urllib.request.Request(url, data=data, method=method)
        req.add_header("Authorization", f"Bearer {self.token}")
        req.add_header("Accept", "application/vnd.github+json")
        req.add_header("X-GitHub-Api-Version", "2022-11-28")
        if data:
            req.add_header("Content-Type", "application/json")
        try:
            with urllib.request.urlopen(req) as resp:
                body = resp.read().decode()
                return json.loads(body) if body else {}
        except urllib.error.HTTPError as exc:
            detail = exc.read().decode()
            raise Fatal(f"GitHub {method} {url} -> {exc.code}: {detail}") from exc

    def find_pr(self, upstream: str, fork_owner: str, branch: str) -> dict | None:
        query = urllib.parse.urlencode(
            {"head": f"{fork_owner}:{branch}", "state": "all", "per_page": 100}
        )
        prs = self.request("GET", f"/repos/{upstream}/pulls?{query}")
        if not prs:
            return None
        # newest first
        return sorted(prs, key=lambda p: p["number"])[-1]

    def pr_detail(self, upstream: str, number: int) -> dict:
        return self.request("GET", f"/repos/{upstream}/pulls/{number}")

    def check_summary(self, fork: str, sha: str) -> str:
        try:
            runs = self.request("GET", f"/repos/{fork}/commits/{sha}/check-runs")
        except Fatal:
            return "?"
        items = runs.get("check_runs", []) if isinstance(runs, dict) else []
        if not items:
            return "-"
        concl = [r.get("conclusion") for r in items]
        if any(x is None for x in concl):
            return "running"
        if any(x in ("failure", "timed_out", "cancelled") for x in concl):
            return c("failing", "red")
        if all(x in ("success", "neutral", "skipped") for x in concl):
            return c("passing", "green")
        return "mixed"

    def update_pr(self, upstream: str, number: int, title: str, body: str) -> dict:
        return self.request("PATCH", f"/repos/{upstream}/pulls/{number}",
                            {"title": title, "body": body})

    def create_pr(self, upstream: str, fork_owner: str, branch: str, base: str,
                  title: str, body: str) -> dict:
        return self.request(
            "POST", f"/repos/{upstream}/pulls",
            {
                "title": title,
                "body": body,
                "head": f"{fork_owner}:{branch}",
                "base": base,
                "draft": True,
                "maintainer_can_modify": True,
            },
        )


# --------------------------------------------------------------------------- #
# state
# --------------------------------------------------------------------------- #


def state_path(repo: str, override: str | None) -> str:
    if override:
        return override
    git_dir = git("rev-parse", "--git-dir", cwd=repo)
    if not os.path.isabs(git_dir):
        git_dir = os.path.join(repo, git_dir)
    return os.path.join(git_dir, STATE_BASENAME)


def load_state(path: str) -> dict:
    if os.path.exists(path):
        with open(path, encoding="utf-8") as fh:
            return json.load(fh)
    return {"batches": {}}


def save_state(path: str, state: dict) -> None:
    with open(path, "w", encoding="utf-8") as fh:
        json.dump(state, fh, indent=1, sort_keys=True)
        fh.write("\n")


# --------------------------------------------------------------------------- #
# branch building
# --------------------------------------------------------------------------- #


def _supports_merge_tree(repo: str) -> bool:
    """git >= 2.38 can do a 3-way merge entirely in memory."""
    head = git("rev-parse", "HEAD", cwd=repo)
    proc = subprocess.run(
        ["git", "merge-tree", "--write-tree", f"--merge-base={head}", head, head],
        cwd=repo, capture_output=True, text=True,
    )
    return proc.returncode == 0


def _replay_commit(repo: str, tree: str, parent: str, orig: str) -> str:
    """Create a commit with `tree` on `parent`, keeping the original authorship."""
    meta = git("show", "-s", "--format=%an%x1f%ae%x1f%aI%x1f%B", orig, cwd=repo)
    name, email, date, message = meta.split("\x1f", 3)
    env = dict(
        os.environ,
        GIT_AUTHOR_NAME=name, GIT_AUTHOR_EMAIL=email, GIT_AUTHOR_DATE=date,
    )
    proc = subprocess.run(
        ["git", "commit-tree", tree, "-p", parent],
        cwd=repo, input=message, capture_output=True, text=True, env=env,
    )
    if proc.returncode != 0:
        raise Fatal(f"commit-tree failed: {proc.stderr.strip()}")
    return proc.stdout.strip()


def build_branch(repo: str, batch: Batch, base_ref: str,
                 write_ref: bool = True) -> tuple[bool, str]:
    """Rebuild `batch.branch` from scratch on top of base_ref.

    Returns (ok, head_sha_or_error). Replays each commit with `git merge-tree`,
    which merges in memory and writes straight to the object database, so the
    caller's working tree, index and HEAD are never touched and no checkout
    happens. Falls back to a throwaway worktree on git < 2.38.

    With write_ref=False no branch ref is created; the head commit is returned
    for read-only use such as rendering a diff.
    """
    if not _supports_merge_tree(repo):
        return _build_branch_worktree(repo, batch, base_ref)

    head = git("rev-parse", base_ref, cwd=repo)
    for cm in batch.commits:
        parent = git("rev-parse", f"{cm.sha}^", cwd=repo)
        proc = subprocess.run(
            ["git", "merge-tree", "--write-tree", f"--merge-base={parent}",
             head, cm.sha],
            cwd=repo, capture_output=True, text=True,
        )
        if proc.returncode != 0:
            lines = proc.stdout.splitlines()
            files = sorted({
                ln.split("\t")[-1] for ln in lines[1:]
                if "\t" in ln and not ln.startswith("CONFLICT")
            })
            where = ", ".join(files[:3]) or "unknown file"
            if len(files) > 3:
                where += f" (+{len(files) - 3} more)"
            return False, (
                f"{cm.sha[:9]} \"{cm.subject}\" does not apply on {base_ref}: "
                f"conflict in {where}"
            )
        tree = proc.stdout.strip().splitlines()[0]
        head = _replay_commit(repo, tree, head, cm.sha)

    if write_ref:
        git("branch", "-f", batch.branch, head, cwd=repo)
    return True, head


def batch_diff(repo: str, batch: Batch, base_ref: str,
               stat: bool = False) -> tuple[bool, str]:
    """The diff the PR would show: base..batch, without creating any ref."""
    ok, head = build_branch(repo, batch, base_ref, write_ref=False)
    if not ok:
        return False, head
    base_sha = git("rev-parse", base_ref, cwd=repo)
    args = ["diff", "--no-color"] + (["--stat"] if stat else []) + [base_sha, head]
    return True, git(*args, cwd=repo)


def _build_branch_worktree(repo: str, batch: Batch, base_ref: str) -> tuple[bool, str]:
    """Portable fallback for git older than 2.38."""
    tmp = tempfile.mkdtemp(prefix="zsp-wt-")
    worktree = os.path.join(tmp, "wt")
    try:
        git("worktree", "add", "--detach", worktree, base_ref, cwd=repo)
        git("checkout", "-B", batch.branch, base_ref, cwd=worktree)
        for cm in batch.commits:
            proc = subprocess.run(
                ["git", "cherry-pick", "--allow-empty", cm.sha],
                cwd=worktree, capture_output=True, text=True,
            )
            if proc.returncode != 0:
                subprocess.run(["git", "cherry-pick", "--abort"],
                               cwd=worktree, capture_output=True, text=True)
                return False, (
                    f"{cm.sha[:9]} \"{cm.subject}\" does not apply on {base_ref}"
                )
        return True, git("rev-parse", "HEAD", cwd=worktree)
    finally:
        subprocess.run(["git", "worktree", "remove", "--force", worktree],
                       cwd=repo, capture_output=True, text=True)
        shutil.rmtree(tmp, ignore_errors=True)


# --------------------------------------------------------------------------- #
# commands
# --------------------------------------------------------------------------- #


def splits_path(repo: str, override: str | None) -> str:
    """--splits, else the per-clone copy, else the one committed in-tree."""
    if override:
        return override
    git_dir = git("rev-parse", "--git-dir", cwd=repo)
    if not os.path.isabs(git_dir):
        git_dir = os.path.join(repo, git_dir)
    local = os.path.join(git_dir, SPLITS_BASENAME)
    if os.path.exists(local):
        return local
    return os.path.join(repo, TOOL_DIR, "splits.json")


def apply_splits(batches: list[Batch], repo: str, override: str | None,
                 files: dict[str, list[str]], owners: dict[str, Ownership],
                 prefix: str) -> list[Batch]:
    """Cut named batches into smaller ones along commit scope.

    Grouping by maintainer merges everything one person owns into a single PR.
    That is usually what you want, but not always: a maintainer owning three
    unrelated subsystems is better served by three focused reviews. The splits
    file names a batch and the sub-batches to cut it into:

        {"doxygen/rtio": [{"name": "llext", "scopes": ["llext"]},
                          {"name": "rtio",  "scopes": ["rtio"]}]}

    A commit joins the first sub-batch whose scope it starts with; anything
    left over stays in the original batch. Splitting is refused when it would
    put commits touching the same file into different PRs, since neither could
    then apply on its own.
    """
    path = splits_path(repo, override)
    if not os.path.exists(path):
        return batches
    try:
        with open(path, encoding="utf-8") as fh:
            spec = json.load(fh)
    except json.JSONDecodeError as exc:
        raise Fatal(f"cannot parse {path}: {exc}") from exc

    by_key = {b.key: b for b in batches}
    unknown = [k for k in spec if k not in by_key]
    if unknown:
        print(c(f"note: splits file mentions unknown batch(es): "
                f"{', '.join(sorted(unknown))}", "yellow"))

    out: list[Batch] = []
    for b in batches:
        rules = spec.get(b.key)
        if not rules:
            out.append(b)
            continue

        buckets: list[tuple[str, list[str], list[Commit]]] = [
            (r["name"], list(r.get("scopes", [])), []) for r in rules
        ]
        leftover: list[Commit] = []
        for cm in b.commits:
            scope = ": ".join(cm.scope)
            for _name, scopes, items in buckets:
                if any(scope == s or scope.startswith(s + ":") for s in scopes):
                    items.append(cm)
                    break
            else:
                leftover.append(cm)

        pieces: list[Batch] = []
        for name, _scopes, items in buckets:
            if not items:
                continue
            pieces.append(Batch(
                key=f"{prefix}/{name}",
                scopes=sorted({a for cm in items for a in owners[cm.sha].areas}),
                commits=items,
                areas=sorted({a for cm in items for a in owners[cm.sha].areas}),
                maintainers=sorted({m for cm in items for m in owners[cm.sha].owners}),
            ))
        if leftover:
            pieces.append(Batch(key=b.key, scopes=b.scopes, commits=leftover,
                                areas=b.areas, maintainers=b.maintainers))

        seen: dict[str, str] = {}
        for piece in pieces:
            for cm in piece.commits:
                for f in files.get(cm.sha, []):
                    if f in seen and seen[f] != piece.key:
                        raise Fatal(
                            f"cannot split {b.key}: {f} is touched by both "
                            f"{seen[f]} and {piece.key}, so neither would apply "
                            f"on its own"
                        )
                    seen[f] = piece.key
        out.extend(pieces)

    # A split name can collide with a batch that already exists elsewhere in
    # the plan; two batches on one branch would silently overwrite each other.
    seen_keys: dict[str, int] = {}
    for b in out:
        seen_keys[b.key] = seen_keys.get(b.key, 0) + 1
    dupes = sorted(k for k, n in seen_keys.items() if n > 1)
    if dupes:
        raise Fatal(
            f"split produced duplicate branch name(s): {', '.join(dupes)}. "
            f"Rename the sub-batch in the splits file."
        )
    return out


def selected(batches: list[Batch], only: list[str] | None) -> list[Batch]:
    """Narrow the batches an action applies to.

    Grouping always runs over the whole series (batch membership and the total
    count have to stay stable), so selection is applied afterwards. Names may
    be given with or without the branch prefix.
    """
    if not only:
        return batches
    wanted = set(only)
    picked = [b for b in batches
              if b.branch in wanted or b.branch.split("/", 1)[-1] in wanted]
    missing = wanted - {b.branch for b in picked} - {
        b.branch.split("/", 1)[-1] for b in picked}
    if missing:
        raise Fatal(f"no batch named: {', '.join(sorted(missing))}")
    return picked


def resolve_fork(repo: str, remote: str, explicit: str | None) -> str:
    if explicit:
        return explicit
    url = git("remote", "get-url", remote, cwd=repo)
    m = re.search(r"[:/]([^/:]+)/([^/]+?)(?:\.git)?$", url)
    if not m:
        raise Fatal(f"cannot infer fork repo from remote '{remote}' url {url}")
    return f"{m.group(1)}/{m.group(2)}"


def fetch_base(repo: str, args) -> str:
    """Make sure we have an up-to-date upstream base, return the ref to use."""
    remotes = git("remote", cwd=repo).split()
    if args.upstream_remote in remotes:
        git("fetch", args.upstream_remote, args.base, cwd=repo)
        return f"{args.upstream_remote}/{args.base}"
    # fall back to the fork's copy of the base branch
    print(c(f"note: no '{args.upstream_remote}' remote; using "
            f"{args.remote}/{args.base} as the base. Add the upstream remote "
            f"with:\n  git remote add {args.upstream_remote} "
            f"https://github.com/{args.upstream}.git", "yellow"))
    git("fetch", args.remote, args.base, cwd=repo)
    return f"{args.remote}/{args.base}"


def compute(args) -> tuple[list[Batch], str, str]:
    repo = args.repo
    base_ref = fetch_base(repo, args)
    source = args.source or git("rev-parse", "--abbrev-ref", "HEAD", cwd=repo)
    commits = load_commits(repo, base_ref, source)

    # Commits that only touch excluded paths are not part of the series to
    # upstream. By default that is this tool's own directory, so committing it
    # on the working branch never turns into a PR proposing it to Zephyr.
    excludes = tuple(args.exclude_paths or ())
    if excludes:
        files_map = commit_files(repo, base_ref, source)
        kept = [
            cm for cm in commits
            if not files_map.get(cm.sha)
            or not all(f.startswith(excludes) for f in files_map[cm.sha])
        ]
        dropped = len(commits) - len(kept)
        if dropped:
            print(c(f"note: skipping {dropped} commit(s) that only touch "
                    f"{', '.join(excludes)}", "grey"))
        commits = kept
        if not commits:
            raise Fatal("every commit in the range was excluded")

    maint = load_maintainers(repo) if args.group_by == "maintainers" else None
    if args.group_by == "maintainers" and maint is None:
        args.group_by = "scope"
    if maint is not None:
        owners = compute_ownership(repo, commits, base_ref, source, maint)
        compute.owners = owners
        files_map = commit_files(repo, base_ref, source)
        batches = make_batches_by_maintainer(
            commits, owners, files_map, args.max_commits, args.branch_prefix)
        batches = apply_splits(batches, repo, args.splits, files_map, owners,
                               args.branch_prefix)
    else:
        compute.owners = {}
        batches = make_batches(commits, args.max_commits, args.split_threshold,
                               args.min_commits, args.branch_prefix)
    return batches, base_ref, source


def cmd_plan(args) -> int:
    batches, base_ref, source = compute(args)
    total = sum(len(b.commits) for b in batches)
    print(f"{c('Source', 'bold')}  {source}  ({total} commits over {base_ref})")
    print(f"{c('Batches', 'bold')} {len(batches)}\n")
    templates = load_templates(args.repo, args.templates)
    show_owners = any(b.areas for b in batches)
    for b in batches:
        n = len(b.commits)
        title, _ = render(b, templates, args.base, len(batches), args.upstream)
        line = (f"  {c(b.branch, 'cyan'):<46} {n:>3} "
                f"commit{'s' if n != 1 else ' '}  ")
        if show_owners:
            who = ", ".join(b.maintainers) or c("no owner in MAINTAINERS.yml",
                                                "yellow")
            print(f"{line} {who}")
            print(f"  {'':<46} {'':>3}          {c(title, 'grey')}")
        else:
            print(f"{line} {title}")
    if args.verbose:
        print()
        for b in batches:
            title, body = render(b, templates, args.base, len(batches),
                                 args.upstream)
            print(c(f"{b.branch}  ->  {title}", "cyan"))
            for cm in b.commits:
                print(f"    {cm.sha[:9]}  {cm.subject}")
            print("    " + "\n    ".join(body.splitlines()) + "\n")
    return 0


def cmd_verify(args) -> int:
    """Build every batch branch locally and report conflicts. Pushes nothing."""
    repo = args.repo
    batches, base_ref, _ = compute(args)
    print(f"building {len(batches)} branches on {base_ref} "
          f"(local only, nothing is pushed)\n")
    bad: list[tuple[Batch, str]] = []
    for b in batches:
        ok, info = build_branch(repo, b, base_ref)
        mark = c("ok", "green") if ok else c("CONFLICT", "red")
        print(f"  {mark:<20} {b.branch:<44} {len(b.commits):>3}")
        if not ok:
            bad.append((b, info))
        if not args.keep:
            git("branch", "-D", b.branch, cwd=repo, check=False)
    if bad:
        print(c(f"\n{len(bad)} batch(es) do not apply cleanly on their own:", "red"))
        for b, info in bad:
            print(f"  {b.branch}: {info}")
        print(c("\nThose commits depend on another batch. Either merge the two "
                "batches (raise --min-commits / lower --max-commits so they land "
                "together) or push them as a stack.", "yellow"))
        return 1
    print(c("\nall batches apply cleanly on top of the base", "green"))
    return 0


def cmd_diff(args) -> int:
    """Print the diff a batch's PR would show."""
    batches, base_ref, _ = compute(args)
    wanted = args.batch
    if not wanted:
        print("pass a branch name, for example:")
        for b in batches[:8]:
            print(f"  {sys.argv[0]} diff {b.branch}")
        return 1
    matches = [b for b in batches
               if b.branch == wanted or b.branch.endswith("/" + wanted)]
    if not matches:
        raise Fatal(f"no batch named {wanted!r}. Run `plan` to list them.")
    for b in matches:
        ok, out = batch_diff(args.repo, b, base_ref, stat=args.stat)
        if not ok:
            print(c(f"{b.branch}: {out}", "red"))
            return 1
        print(out)
    return 0


def cmd_push(args) -> int:
    repo = args.repo
    all_batches, base_ref, _ = compute(args)
    batches = selected(all_batches, args.only)
    path = state_path(repo, args.state)
    state = load_state(path)
    fork = resolve_fork(repo, args.remote, args.fork)
    base_sha = git("rev-parse", base_ref, cwd=repo)

    todo: list[tuple[Batch, str]] = []
    for b in batches:
        rec = state["batches"].get(b.branch, {})
        if rec.get("state") in (ST_MERGED, ST_CLOSED) and not args.include_closed:
            continue
        pushed = bool(rec.get("head_sha"))
        changed = rec.get("fingerprint") != b.fingerprint()
        rebased = rec.get("base_sha") != base_sha
        if pushed and not changed and not rebased and not args.force:
            continue
        reason = ("new" if not pushed
                  else "content changed" if changed else "base moved")
        todo.append((b, reason))

    if not todo:
        print(c("everything already pushed and up to date", "green"))
        return 0

    print(f"{len(todo)} branch(es) to build and push to {c(fork, 'cyan')}:\n")
    for b, reason in todo:
        n = len(b.commits)
        print(f"  {b.branch:<46} {n:>3} commit{'s' if n != 1 else ' '}   ({reason})")
    if not args.yes:
        print(c("\ndry run: re-run with --yes to build and push", "yellow"))
        return 0

    print()
    failures = 0
    for b, _ in todo:
        ok, info = build_branch(repo, b, base_ref)
        if not ok:
            print(f"  {c('CONFLICT', 'red')} {b.branch}: {info}")
            state["batches"].setdefault(b.branch, {}).update(
                {"state": ST_CONFLICT, "note": info}
            )
            failures += 1
            continue
        git("push", "--force-with-lease", args.remote,
            f"{b.branch}:{b.branch}", cwd=repo)
        print(f"  {c('pushed', 'green')}   {b.branch} -> {info[:9]}")
        rec = state["batches"].setdefault(b.branch, {})
        rec.update({
            "fingerprint": b.fingerprint(),
            "base_sha": base_sha,
            "head_sha": info,
            "commits": [cm.subject for cm in b.commits],
            "note": "",
        })
        rec.setdefault("state", ST_NEW)
    save_state(path, state)
    if failures:
        print(c(f"\n{failures} batch(es) hit conflicts. Those commits depend on "
                "changes in another batch; merge the two batches (or push them "
                "as a stack) and re-run.", "yellow"))
    return 1 if failures else 0


def cmd_open(args) -> int:
    repo = args.repo
    all_batches, _, _ = compute(args)
    batches = selected(all_batches, args.only)
    path = state_path(repo, args.state)
    state = load_state(path)
    fork = resolve_fork(repo, args.remote, args.fork)
    fork_owner = fork.split("/")[0]
    token = GitHub.discover_token(args.token)
    if not token:
        raise Fatal("no GitHub token found. Pass --token, set $GH_TOKEN, or "
                    "run `gh auth login`.")
    gh = GitHub(token)

    todo, unpushed = [], []
    for b in batches:
        rec = state["batches"].get(b.branch, {})
        if rec.get("pr"):
            continue
        if not rec.get("head_sha"):
            unpushed.append(b)
            continue
        # A PR may already exist from an earlier run or from the web UI; adopt
        # it rather than trying to open a duplicate.
        existing = gh.find_pr(args.upstream, fork_owner, b.branch)
        if existing:
            rec["pr"] = existing["number"]
            rec["url"] = existing["html_url"]
            continue
        todo.append(b)

    if unpushed:
        print(c(f"{len(unpushed)} batch(es) not pushed yet; run `push --yes` "
                f"first:", "yellow"))
        for b in unpushed:
            print(f"  {b.branch}")
        print()

    if not todo:
        save_state(path, state)
        print(c("no new PRs to open", "green"))
        return 0

    templates = load_templates(repo, args.templates)
    print(f"{len(todo)} draft PR(s) to open against "
          f"{c(args.upstream, 'cyan')} (base {args.base}):\n")
    for b in todo:
        title, _ = render(b, templates, args.base, len(all_batches), args.upstream)
        print(f"  {b.branch:<46} {title}")
    if not args.yes:
        print(c("\ndry run: re-run with --yes to open them", "yellow"))
        return 0

    print()
    for b in todo:
        title, body = render(b, templates, args.base, len(all_batches),
                             args.upstream)
        pr = gh.create_pr(args.upstream, fork_owner, b.branch, args.base,
                          title, body)
        rec = state["batches"].setdefault(b.branch, {})
        rec.update({"pr": pr["number"], "url": pr["html_url"], "state": ST_OPEN,
                    "text": _text_hash(title, body)})
        print(f"  {c('opened', 'green')} #{pr['number']:<6} {b.branch}")
        print(f"          {pr['html_url']}")
    save_state(path, state)
    return 0


def derive_status(batch: Batch, rec: dict, base_sha: str,
                  detail: dict | None) -> tuple[str, str, str]:
    """Work out (state, colour, next action) for one batch.

    `detail` is the GitHub PR object, or None when we have no token or no PR.
    Precedence: what GitHub says about a finished PR wins; otherwise local
    drift (content changed, base moved) decides, because that is what the user
    has to act on next.
    """
    pushed = bool(rec.get("head_sha"))
    changed = pushed and rec.get("fingerprint") != batch.fingerprint()
    base_moved = pushed and rec.get("base_sha") != base_sha

    if detail and detail.get("merged"):
        return "merged", "magenta", ""
    if detail and detail.get("state") == "closed":
        return "closed", "grey", "reopen or drop the batch"
    if rec.get("state") == ST_CONFLICT:
        return "conflicts", "red", "merge with the batch it depends on"
    if not pushed:
        return "not pushed", "yellow", "push"
    if changed:
        return "drifted", "yellow", "push (content changed)"
    if base_moved:
        return "stale base", "yellow", "push (base moved)"
    if not rec.get("pr"):
        return "pushed", "cyan", "open PR"

    state = "draft" if (detail or {}).get("draft") else "open"
    mergeable = (detail or {}).get("mergeable_state")
    if mergeable in ("dirty", "behind"):
        return f"{state} ({mergeable})", "yellow", "push (base moved)"
    return state, "green", ""


def cmd_update(args) -> int:
    """Re-render titles/bodies and PATCH any open PR whose text changed.

    Use after editing the templates, or when reworking commits changed the
    commit list that the body lists.
    """
    repo = args.repo
    all_batches, _, _ = compute(args)
    batches = selected(all_batches, args.only)
    path = state_path(repo, args.state)
    state = load_state(path)
    templates = load_templates(repo, args.templates)

    token = GitHub.discover_token(args.token)
    if not token:
        raise Fatal("no GitHub token found. Pass --token, set $GH_TOKEN, or "
                    "run `gh auth login`.")
    gh = GitHub(token)

    todo = []
    for b in batches:
        rec = state["batches"].get(b.branch, {})
        if not rec.get("pr") or rec.get("state") in (ST_MERGED, ST_CLOSED):
            continue
        title, body = render(b, templates, args.base, len(all_batches),
                             args.upstream)
        if rec.get("text") == _text_hash(title, body) and not args.force:
            continue
        todo.append((b, rec, title, body))

    if not todo:
        print(c("all PR descriptions are up to date", "green"))
        return 0

    print(f"{len(todo)} PR description(s) to update:\n")
    for b, rec, title, _ in todo:
        print(f"  #{rec['pr']:<6} {b.branch:<40} {title}")
    if not args.yes:
        print(c("\ndry run: re-run with --yes to update them", "yellow"))
        return 0

    print()
    for b, rec, title, body in todo:
        gh.update_pr(args.upstream, int(rec["pr"]), title, body)
        rec["text"] = _text_hash(title, body)
        print(f"  {c('updated', 'green')} #{rec['pr']} {b.branch}")
    save_state(path, state)
    return 0


def cmd_status(args) -> int:
    repo = args.repo
    batches, base_ref, source = compute(args)
    path = state_path(repo, args.state)
    state = load_state(path)
    fork = resolve_fork(repo, args.remote, args.fork)
    base_sha = git("rev-parse", base_ref, cwd=repo)

    token = GitHub.discover_token(args.token)
    gh = GitHub(token) if token else None
    if not gh:
        print(c("no GitHub token: showing local state only "
                "(set $GH_TOKEN or run `gh auth login` for PR and CI state)\n",
                "yellow"))

    total = sum(len(b.commits) for b in batches)
    print(f"{c('Source', 'bold')} {source} ({total} commits)   "
          f"{c('Base', 'bold')} {base_ref} @ {base_sha[:9]}   "
          f"{c('Fork', 'bold')} {fork}\n")
    print(c(f"{'BRANCH':<40} {'CMTS':>4}  {'PR':<7} {'STATE':<16} "
            f"{'CI':<9} NEXT ACTION", "bold"))

    counts: dict[str, int] = {}
    actions = 0
    for b in batches:
        rec = state["batches"].setdefault(b.branch, {})
        detail = None
        ci = "-"
        if gh and rec.get("pr"):
            try:
                detail = gh.pr_detail(args.upstream, int(rec["pr"]))
                head_sha = (detail.get("head") or {}).get("sha", "")
                if head_sha and not detail.get("merged"):
                    ci = gh.check_summary(fork, head_sha)
            except Fatal as exc:
                print(c(f"  warning: {exc}", "yellow"), file=sys.stderr)

        label, colour, action = derive_status(b, rec, base_sha, detail)
        if detail:
            rec["state"] = (
                ST_MERGED if detail.get("merged")
                else ST_CLOSED if detail.get("state") == "closed"
                else ST_OPEN
            )
        counts[label.split(" (")[0]] = counts.get(label.split(" (")[0], 0) + 1
        actions += bool(action)

        pr_txt = f"#{rec['pr']}" if rec.get("pr") else "-"
        print(f"{b.branch:<40} {len(b.commits):>4}  {pr_txt:<7} "
              f"{c(f'{label:<16}', colour)} {ci:<9} {action}")

    # Branches we pushed once that no longer correspond to any batch. Happens
    # when commits get re-scoped, squashed or dropped on the source branch;
    # without this they would silently keep an open PR nobody looks at.
    known = {b.branch for b in batches}
    orphans = [
        (br, rec) for br, rec in sorted(state["batches"].items())
        if br not in known and rec.get("head_sha")
        and rec.get("state") not in (ST_MERGED, ST_CLOSED)
    ]
    for br, rec in orphans:
        pr_txt = f"#{rec['pr']}" if rec.get("pr") else "-"
        action = "close PR and delete branch" if rec.get("pr") else "delete branch"
        label = c(f"{'orphaned':<16}", "red")
        print(f"{br:<40} {'-':>4}  {pr_txt:<7} {label} {'-':<9} {action}")
        counts["orphaned"] = counts.get("orphaned", 0) + 1
        actions += 1

    save_state(path, state)
    print("\n" + "   ".join(f"{k}: {v}" for k, v in sorted(counts.items())))
    if orphans:
        print(c(f"{len(orphans)} branch(es) no longer match any batch: their "
                "commits were re-scoped, squashed or dropped.", "yellow"))
    if actions:
        print(c(f"{actions} batch(es) need attention. "
                f"Run `push --yes`, then `open --yes`.", "yellow"))
    else:
        print(c("nothing to do", "green"))
    return 0


def cmd_all(args) -> int:
    rc = cmd_push(args)
    if rc == 0 and args.yes:
        rc = cmd_open(args)
    if args.yes:
        cmd_status(args)
    return rc


# --------------------------------------------------------------------------- #
# web UI
# --------------------------------------------------------------------------- #

PAGE = r"""<!doctype html>
<html lang="en"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>zephyr-series-prs</title>
<style>
:root{--bg:#fff;--fg:#1c1f24;--dim:#6a737d;--line:#e3e6ea;--card:#f7f8fa;
--accent:#0969da;--ok:#1a7f37;--warn:#9a6700;--bad:#cf222e;--mag:#8250df}
@media(prefers-color-scheme:dark){:root{--bg:#0d1117;--fg:#e6edf3;--dim:#8b949e;
--line:#30363d;--card:#161b22;--accent:#58a6ff;--ok:#3fb950;--warn:#d29922;
--bad:#f85149;--mag:#bc8cff}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);
font:14px/1.5 ui-sans-serif,system-ui,-apple-system,"Segoe UI",sans-serif}
header{padding:14px 20px;border-bottom:1px solid var(--line);display:flex;
gap:16px;align-items:center;flex-wrap:wrap;position:sticky;top:0;background:var(--bg);z-index:5}
h1{font-size:15px;margin:0;font-weight:600}
.meta{color:var(--dim);font-size:12px;display:flex;gap:14px;flex-wrap:wrap}
.grow{flex:1}
button{font:inherit;padding:5px 11px;border:1px solid var(--line);border-radius:6px;
background:var(--card);color:var(--fg);cursor:pointer}
button:hover{border-color:var(--accent)}
button.primary{background:var(--accent);border-color:var(--accent);color:#fff}
button:disabled{opacity:.5;cursor:default}
label.apply{display:flex;align-items:center;gap:6px;font-size:12px;color:var(--dim);
border:1px solid var(--line);border-radius:6px;padding:4px 9px}
main{padding:16px 20px;display:grid;grid-template-columns:1fr;gap:14px}
input[type=search]{font:inherit;padding:5px 9px;border:1px solid var(--line);
border-radius:6px;background:var(--card);color:var(--fg);min-width:200px}
table{width:100%;border-collapse:collapse;font-size:13px}
th{text-align:left;font-weight:600;color:var(--dim);font-size:11px;
text-transform:uppercase;letter-spacing:.04em;padding:6px 8px;border-bottom:1px solid var(--line)}
td{padding:6px 8px;border-bottom:1px solid var(--line);vertical-align:top}
tr.b:hover{background:var(--card)}
tr.b{cursor:pointer}
.br{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;color:var(--accent)}
.who{color:var(--dim)}
.pill{display:inline-block;padding:1px 7px;border-radius:20px;font-size:11px;
border:1px solid var(--line)}
.s-merged{color:var(--mag)}.s-open{color:var(--ok)}.s-draft{color:var(--dim)}
.s-drifted,.s-stale{color:var(--warn)}.s-notpushed{color:var(--warn)}
.s-conflicts,.s-orphaned{color:var(--bad)}
.act{color:var(--warn);font-size:12px}
.det{background:var(--card)}
.det td{padding:10px 14px}
.cm{font-family:ui-monospace,SFMono-Regular,Menlo,monospace;font-size:12px;
color:var(--dim);margin:1px 0}
pre.body{white-space:pre-wrap;background:var(--bg);border:1px solid var(--line);
border-radius:6px;padding:10px;font-size:12px;margin:8px 0 0;max-height:280px;overflow:auto}
td.sel{width:26px;padding-right:0}
input[type=checkbox]{cursor:pointer}
.rowbtns{display:flex;gap:6px;margin:8px 0 0}
.rowbtns button{font-size:12px;padding:3px 10px}
.tabs{display:flex;gap:6px;margin:8px 0 6px}
.tabs button{padding:3px 10px;font-size:12px}
.tabs button.sel{background:var(--accent);border-color:var(--accent);color:#fff}
pre.diff{white-space:pre;overflow:auto;max-height:460px;background:var(--bg);
border:1px solid var(--line);border-radius:6px;padding:10px;font-size:12px;
font-family:ui-monospace,SFMono-Regular,Menlo,monospace;margin:0;tab-size:8}
pre.diff .a{color:var(--ok)}
pre.diff .d{color:var(--bad)}
pre.diff .h{color:var(--mag);font-weight:600}
pre.diff .k{color:var(--dim)}
#log{background:var(--card);border:1px solid var(--line);border-radius:8px;
padding:10px 12px;font-family:ui-monospace,Menlo,monospace;font-size:12px;
white-space:pre-wrap;max-height:320px;overflow:auto;display:none}
#log.on{display:block}
.spin{display:inline-block;width:11px;height:11px;border:2px solid var(--line);
border-top-color:var(--accent);border-radius:50%;animation:r .7s linear infinite;
vertical-align:-1px}
@keyframes r{to{transform:rotate(360deg)}}
.warnbar{background:#fff8c5;color:#7a5d00;border:1px solid #d4a72c55;padding:8px 12px;
border-radius:6px;font-size:12px}
@media(prefers-color-scheme:dark){.warnbar{background:#2d2a12;color:#e3b341;}}
</style></head><body>
<header>
  <h1>zephyr-series-prs</h1>
  <div class="meta" id="meta"></div>
  <div class="grow"></div>
  <span id="selinfo" class="meta"></span>
  <input type="search" id="q" placeholder="filter branch / maintainer / area">
  <label class="apply"><input type="checkbox" id="apply"> apply changes</label>
  <button onclick="run('verify')">verify</button>
  <button onclick="run('push')" title="selected batches, or all if none selected">push</button>
  <button onclick="run('open')" title="selected batches, or all if none selected">open PRs</button>
  <button onclick="run('update')" title="selected batches, or all if none selected">update text</button>
  <button class="primary" onclick="run('status')">refresh PR state</button>
</header>
<main>
  <div id="bar"></div>
  <div id="log"></div>
  <table><thead><tr>
    <th><input type="checkbox" id="all" title="select all shown"></th>
    <th>branch</th><th>commits</th><th>maintainers</th><th>PR</th>
    <th>state</th><th>CI</th><th>next action</th>
  </tr></thead><tbody id="rows"></tbody></table>
</main>
<script>
const NONCE = "__NONCE__";
let DATA = {batches: []}, open_ = new Set(), busy = false;
let tabs_ = {}, diffs_ = {}, sel_ = new Set();

function pick(branch, on){
  on ? sel_.add(branch) : sel_.delete(branch);
  draw();
}
function one(action, branch){ run(action, [branch]); }

function colour(text){
  return text.split("\n").map(l=>{
    const e = esc(l);
    if(l.startsWith("diff --git") || l.startsWith("index ")
       || l.startsWith("--- ") || l.startsWith("+++ ")) return `<span class="k">${e}</span>`;
    if(l.startsWith("@@")) return `<span class="h">${e}</span>`;
    if(l.startsWith("+")) return `<span class="a">${e}</span>`;
    if(l.startsWith("-")) return `<span class="d">${e}</span>`;
    return e;
  }).join("\n");
}

async function tab(branch, name){
  tabs_[branch] = name;
  draw();
  if(name === "diff" && diffs_[branch] === undefined){
    try{
      const r = await api("/api/diff", {branch});
      diffs_[branch] = r.diff || "(empty diff)";
    }catch(e){ diffs_[branch] = "error: " + e.message; }
    draw();
  }
}

async function api(path, body){
  const r = await fetch(path, {method: body ? "POST" : "GET",
    headers: {"content-type":"application/json","x-nonce":NONCE},
    body: body ? JSON.stringify(body) : undefined});
  if(!r.ok) throw new Error(await r.text());
  return r.json();
}
function cls(s){return "s-" + (s||"").split(" ")[0].replace(/[^a-z]/gi,"").toLowerCase();}
const ENT = {"&":"&amp;","<":"&lt;",">":"&gt;"};
function esc(s){
  return (s ?? "").toString().replace(/[&<>]/g, c => ENT[c]);
}

function draw(){
  const q = document.getElementById("q").value.toLowerCase();
  const rows = [];
  let shown = 0;
  for(const b of DATA.batches){
    const hay = [b.branch, (b.maintainers||[]).join(" "),
                 (b.areas||[]).join(" "), b.title].join(" ").toLowerCase();
    if(q && !hay.includes(q)) continue;
    shown++;
    rows.push(`<tr class="b" onclick="tog('${b.branch}')">
      <td class="sel" onclick="event.stopPropagation()">
        <input type="checkbox" ${sel_.has(b.branch)?"checked":""}
               onchange="pick('${b.branch}', this.checked)"></td>
      <td class="br">${esc(b.branch)}</td>
      <td>${b.count}</td>
      <td class="who">${esc((b.maintainers||[]).join(", ")) || "<i>no owner</i>"}</td>
      <td>${b.pr ? `<a href="${esc(b.url||"#")}" target="_blank"
            rel="noopener">#${b.pr}</a>` : "-"}</td>
      <td class="${cls(b.state)}">${esc(b.state)}</td>
      <td>${esc(b.ci||"-")}</td>
      <td class="act">${esc(b.action||"")}</td></tr>`);
    if(open_.has(b.branch)){
      const tab = tabs_[b.branch] || "commits";
      let pane;
      if(tab === "diff"){
        const d = diffs_[b.branch];
        pane = d === undefined
          ? `<div class="k">loading diff <span class="spin"></span></div>`
          : `<pre class="diff">${colour(d)}</pre>`;
      }else if(tab === "body"){
        pane = `<pre class="body">${esc(b.body)}</pre>`;
      }else{
        pane = b.commits.map(c=>`<div class="cm">${esc(c)}</div>`).join("");
      }
      rows.push(`<tr class="det"><td colspan="8">
        <b>${esc(b.title)}</b>
        <div style="color:var(--dim);font-size:12px;margin:4px 0 0">
          areas: ${esc((b.areas||[]).join(", ")) || "n/a"}</div>
        <div class="tabs" onclick="event.stopPropagation()">
          ${["commits","diff","body"].map(t=>
            `<button class="${t===tab?"sel":""}" onclick="tab('${b.branch}','${t}')">${t}</button>`
          ).join("")}
        </div>
        ${pane}
        <div class="rowbtns" onclick="event.stopPropagation()">
          <button onclick="one('push','${b.branch}')">push this</button>
          <button onclick="one('open','${b.branch}')">open this PR</button>
          <button onclick="one('update','${b.branch}')">update this text</button>
        </div></td></tr>`);
    }
  }
  document.getElementById("rows").innerHTML = rows.join("") ||
    `<tr><td colspan="8" style="color:var(--dim);padding:20px">no batches match</td></tr>`;
  document.getElementById("selinfo").textContent =
    sel_.size ? `${sel_.size} selected` : "";
  const d = DATA;
  document.getElementById("meta").innerHTML =
    `<span>${esc(d.source||"")} &rarr; ${esc(d.base||"")}</span>
     <span>${d.total_commits||0} commits</span>
     <span>${shown}/${DATA.batches.length} batches</span>
     <span>group-by: ${esc(d.group_by||"")}</span>
     <span>fork: ${esc(d.fork||"")}</span>`;
  const notes = [];
  if(!d.token) notes.push("No GitHub token found: PR and CI state "
    + "unavailable. Set $GH_TOKEN or run `gh auth login`.");
  if(d.note) notes.push(d.note);
  document.getElementById("bar").innerHTML = notes.length
    ? `<div class="warnbar">${notes.map(esc).join("<br>")}</div>` : "";
}
function tog(b){ open_.has(b) ? open_.delete(b) : open_.add(b); draw(); }
function log(t, append){
  const el = document.getElementById("log");
  el.classList.add("on");
  el.textContent = append ? el.textContent + t : t;
  el.scrollTop = el.scrollHeight;
}
async function load(){
  try{ DATA = await api("/api/plan"); diffs_ = {}; draw(); }
  catch(e){ log("error: " + e.message); }
}
async function run(action, branches){
  if(busy) return; busy = true;
  const apply = document.getElementById("apply").checked;
  const mutating = ["push","open","update"].includes(action);
  const only = branches || (sel_.size ? [...sel_] : null);
  const scope = only ? (only.length === 1 ? only[0] : `${only.length} batches`)
                     : "all batches";
  if(mutating && apply &&
     !confirm(`Really ${action} (${scope})? This writes to your fork / GitHub.`)){
    busy = false; return;
  }
  document.querySelectorAll("button").forEach(b=>b.disabled=true);
  log(`$ ${action}${only ? " --only " + only.join(" --only ") : ""}` +
      `${mutating && apply ? " --yes" : ""}\n`);
  try{
    const r = await api("/api/action",
                        {action, apply, nonce: NONCE, branches: only});
    log(r.output || "(no output)", true);
    diffs_ = {};
    if(r.batches) { DATA = r; draw(); } else { await load(); }
  }catch(e){ log("\nerror: " + e.message, true); }
  document.querySelectorAll("button").forEach(b=>b.disabled=false);
  busy = false;
}
document.getElementById("all").addEventListener("change", e=>{
  const q = document.getElementById("q").value.toLowerCase();
  for(const b of DATA.batches){
    const hay = [b.branch, (b.maintainers||[]).join(" "),
                 (b.areas||[]).join(" "), b.title].join(" ").toLowerCase();
    if(q && !hay.includes(q)) continue;
    e.target.checked ? sel_.add(b.branch) : sel_.delete(b.branch);
  }
  draw();
});
document.getElementById("q").addEventListener("input", draw);
load();
</script></body></html>
"""


def _batch_payload(args, batches, base_ref, source, state, base_sha, gh, fork,
                   with_github: bool) -> dict:
    templates = load_templates(args.repo, args.templates)
    out = []
    for b in batches:
        rec = state["batches"].setdefault(b.branch, {})
        detail = None
        ci = "-"
        if with_github and gh and rec.get("pr"):
            try:
                detail = gh.pr_detail(args.upstream, int(rec["pr"]))
                head = (detail.get("head") or {}).get("sha", "")
                if head and not detail.get("merged"):
                    ci = gh.check_summary(fork, head)
            except Fatal:
                detail = None
        label, _colour, action = derive_status(b, rec, base_sha, detail)
        if detail:
            rec["state"] = (
                ST_MERGED if detail.get("merged")
                else ST_CLOSED if detail.get("state") == "closed" else ST_OPEN
            )
        title, body = render(b, templates, args.base, len(batches), args.upstream)
        out.append({
            "branch": b.branch, "count": len(b.commits),
            "maintainers": b.maintainers, "areas": b.areas,
            "title": title, "body": body,
            "commits": [f"{cm.sha[:9]}  {cm.subject}" for cm in b.commits],
            "pr": rec.get("pr"), "url": rec.get("url"),
            "state": label, "ci": ci, "action": action,
        })

    known = {b.branch for b in batches}
    for br, rec in sorted(state["batches"].items()):
        if br in known or not rec.get("head_sha"):
            continue
        if rec.get("state") in (ST_MERGED, ST_CLOSED):
            continue
        out.append({
            "branch": br, "count": 0, "maintainers": [], "areas": [],
            "title": "(no longer part of the series)", "body": "", "commits": [],
            "pr": rec.get("pr"), "url": rec.get("url"), "state": "orphaned",
            "ci": "-",
            "action": "close PR and delete branch" if rec.get("pr") else "delete branch",
        })
    return {
        "batches": out, "source": source, "base": base_ref,
        "total_commits": sum(len(b.commits) for b in batches),
        "group_by": args.group_by, "fork": fork,
        "token": bool(GitHub.discover_token(args.token)),
    }


def cmd_serve(args) -> int:
    """Serve a local dashboard for the series."""
    import http.server
    import io
    import secrets
    import socketserver
    import threading
    import webbrowser
    from contextlib import redirect_stdout, redirect_stderr

    nonce = secrets.token_urlsafe(16)
    page = PAGE.replace("__NONCE__", nonce)
    lock = threading.Lock()

    def build(with_github: bool) -> dict:
        batches, base_ref, source = compute(args)
        path = state_path(args.repo, args.state)
        state = load_state(path)
        fork = resolve_fork(args.repo, args.remote, args.fork)
        base_sha = git("rev-parse", base_ref, cwd=args.repo)
        token = GitHub.discover_token(args.token)
        gh = GitHub(token) if token else None
        data = _batch_payload(args, batches, base_ref, source, state, base_sha,
                              gh, fork, with_github)
        save_state(path, state)
        return data

    class Handler(http.server.BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def _send(self, code, body: bytes, ctype: str) -> None:
            self.send_response(code)
            self.send_header("Content-Type", ctype)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _json(self, code, obj) -> None:
            self._send(code, json.dumps(obj).encode(), "application/json")

        def _ok_nonce(self) -> bool:
            # The page is on localhost, but any site the browser visits could
            # POST here; a per-run nonce keeps those requests out.
            return self.headers.get("x-nonce") == nonce

        def log_message(self, *a):  # quiet
            pass

        def do_GET(self):
            if self.path == "/":
                self._send(200, page.encode(), "text/html; charset=utf-8")
            elif self.path == "/api/plan":
                if not self._ok_nonce():
                    return self._json(403, {"error": "bad nonce"})
                with lock:
                    try:
                        self._json(200, build(False))
                    except Fatal as exc:
                        self._json(500, {"error": str(exc)})
            else:
                self._send(404, b"not found", "text/plain")

        def do_POST(self):
            if not self._ok_nonce() or self.path not in ("/api/action", "/api/diff"):
                return self._json(403, {"error": "bad request"})
            n = int(self.headers.get("Content-Length", 0))
            req = json.loads(self.rfile.read(n) or b"{}")

            if self.path == "/api/diff":
                with lock:
                    try:
                        batches, base_ref, _ = compute(args)
                        match = next((b for b in batches
                                      if b.branch == req.get("branch")), None)
                        if match is None:
                            return self._json(404, {"error": "unknown batch"})
                        ok, out = batch_diff(args.repo, match, base_ref)
                        return self._json(200, {"diff": out if ok
                                                else f"cannot build batch: {out}"})
                    except Fatal as exc:
                        return self._json(500, {"error": str(exc)})

            action = req.get("action")
            handlers = {"verify": cmd_verify, "push": cmd_push,
                        "open": cmd_open, "update": cmd_update,
                        "status": cmd_status}
            if action not in handlers:
                return self._json(400, {"error": f"unknown action {action}"})
            with lock:
                sub = argparse.Namespace(**vars(args))
                sub.yes = bool(req.get("apply")) and action != "verify"
                picked = req.get("branches") or None
                sub.only = picked if action in ("push", "open", "update") else None
                buf = io.StringIO()
                os.environ["NO_COLOR"] = "1"
                try:
                    with redirect_stdout(buf), redirect_stderr(buf):
                        handlers[action](sub)
                except Fatal as exc:
                    buf.write(f"\nerror: {exc}\n")
                except Exception as exc:  # keep the server alive
                    buf.write(f"\nunexpected error: {exc}\n")
                payload = {"output": buf.getvalue()}
                try:
                    payload.update(build(action == "status"))
                except Fatal as exc:
                    payload["output"] += f"\nerror rebuilding plan: {exc}\n"
                self._json(200, payload)

    class Server(socketserver.ThreadingTCPServer):
        allow_reuse_address = True
        daemon_threads = True

    url = f"http://{args.host}:{args.port}/"
    with Server((args.host, args.port), Handler) as httpd:
        print(f"serving {url}  (ctrl-c to stop)")
        if not args.no_browser:
            threading.Timer(0.4, lambda: webbrowser.open(url)).start()
        try:
            httpd.serve_forever()
        except KeyboardInterrupt:
            print("\nbye")
    return 0


# --------------------------------------------------------------------------- #


def main(argv: list[str] | None = None) -> int:
    p = argparse.ArgumentParser(
        prog="zephyr-series-prs",
        description="Split one long commit series into per-subsystem draft PRs.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog="Mutating commands are dry runs unless you pass --yes.",
    )
    p.add_argument("--repo", default=".", help="path to the git repo (default: .)")
    p.add_argument("--source", help="branch holding the series (default: current)")
    p.add_argument("--base", default=DEFAULT_BASE, help="upstream base branch")
    p.add_argument("--upstream", default=DEFAULT_UPSTREAM,
                   help=f"upstream repo (default: {DEFAULT_UPSTREAM})")
    p.add_argument("--upstream-remote", default="upstream",
                   help="git remote pointing at upstream (default: upstream)")
    p.add_argument("--remote", default="origin",
                   help="git remote for your fork (default: origin)")
    p.add_argument("--fork", help="fork repo as owner/name (default: infer from remote)")
    p.add_argument("--exclude-paths", action="append", default=[TOOL_DIR],
                   help="drop commits that only touch these path prefixes "
                        f"(default: {TOOL_DIR})")
    p.add_argument("--branch-prefix", default=DEFAULT_BRANCH_PREFIX,
                   help=f"branch namespace (default: {DEFAULT_BRANCH_PREFIX})")
    p.add_argument("--group-by", choices=["maintainers", "scope"],
                   default="maintainers",
                   help="batch by MAINTAINERS.yml owner (default, so each PR "
                        "lands on the right reviewer) or by commit subject scope")
    p.add_argument("--max-commits", type=int, default=40,
                   help="split an area into sub-batches above this size")
    p.add_argument("--split-threshold", type=int, default=3,
                   help="sub-batches smaller than this are folded into <area>-assorted")
    p.add_argument("--min-commits", type=int, default=3,
                   help="areas smaller than this are pooled into shared "
                        "'assorted' batches (1 = one PR per area)")
    p.add_argument("--state", help="state file (default: <git-dir>/" + STATE_BASENAME + ")")
    p.add_argument("--splits",
                   help="JSON file cutting named batches into smaller ones "
                        "(default: <git-dir>/" + SPLITS_BASENAME + ", then "
                        + TOOL_DIR + "/splits.json)")
    p.add_argument("--templates",
                   help="PR title/body templates (default: <git-dir>/"
                        + TEMPLATES_BASENAME + ", falls back to built-in defaults)")
    p.add_argument("--token", help="GitHub token (default: $GH_TOKEN or gh auth token)")
    p.add_argument("--yes", action="store_true", help="actually push / open PRs")
    p.add_argument("--force", action="store_true", help="rebuild and push every batch")
    p.add_argument("--include-closed", action="store_true",
                   help="also rebuild batches whose PR is merged or closed")
    p.add_argument("--keep", action="store_true",
                   help="verify: keep the locally built branches instead of deleting them")
    p.add_argument("--host", default="127.0.0.1", help="serve: bind address")
    p.add_argument("--port", type=int, default=8765, help="serve: port")
    p.add_argument("--no-browser", action="store_true",
                   help="serve: do not open a browser window")
    p.add_argument("--only", action="append",
                   help="limit push/open/update to this batch (repeatable; "
                        "branch name, with or without the prefix)")
    p.add_argument("--stat", action="store_true",
                   help="diff: show a diffstat instead of the full patch")
    p.add_argument("-v", "--verbose", action="store_true")
    p.add_argument("command", nargs="?", default="status",
                   choices=["plan", "verify", "diff", "push", "open", "update",
                            "status", "templates", "serve", "all"],
                   help="what to do (default: status)")

    p.add_argument("batch", nargs="?", help="diff: which batch to show")
    args = p.parse_args(argv)
    args.repo = os.path.abspath(args.repo)

    handlers = {
        "plan": cmd_plan, "verify": cmd_verify, "push": cmd_push,
        "open": cmd_open, "update": cmd_update, "status": cmd_status,
        "templates": cmd_templates, "serve": cmd_serve, "diff": cmd_diff,
        "all": cmd_all,
    }
    try:
        return handlers[args.command](args)
    except Fatal as exc:
        print(c(f"error: {exc}", "red"), file=sys.stderr)
        return 2
    except KeyboardInterrupt:
        return 130


if __name__ == "__main__":
    sys.exit(main())
