#!/usr/bin/env python3
# Copyright (c) 2026 The Linux Foundation
#
# SPDX-License-Identifier: Apache-2.0

import os

import yaml


def _normalizeBuildDir(build_dir, base_dir):
    if not build_dir:
        return None

    if not os.path.isabs(build_dir):
        build_dir = os.path.join(base_dir, build_dir)

    return os.path.abspath(build_dir)


def _loadDomains(domains_file):
    try:
        with open(domains_file, encoding="utf-8") as f:
            domains = yaml.safe_load(f) or {}
    except OSError:
        return None

    if not isinstance(domains, dict):
        return None

    return domains


def _getDomainBuildDirs(domains, base_dir):
    build_dirs = []

    for domain in domains.get("domains", []):
        if not isinstance(domain, dict):
            continue

        build_dir = _normalizeBuildDir(domain.get("build_dir"), base_dir)
        if build_dir:
            build_dirs.append(build_dir)

    return build_dirs


def getCMakeQueryBuildDirs(build_dir):
    build_dir = os.path.abspath(build_dir)
    query_build_dirs = [build_dir]

    current_domains_file = os.path.join(build_dir, "domains.yaml")
    current_domains = _loadDomains(current_domains_file)
    if current_domains is not None:
        base_dir = os.path.dirname(current_domains_file)
        for domain_build_dir in _getDomainBuildDirs(current_domains, base_dir):
            if domain_build_dir not in query_build_dirs:
                query_build_dirs.append(domain_build_dir)
        return query_build_dirs

    parent_domains_file = os.path.join(os.path.dirname(build_dir), "domains.yaml")
    parent_domains = _loadDomains(parent_domains_file)
    if parent_domains is None:
        return query_build_dirs

    base_dir = os.path.dirname(parent_domains_file)
    parent_build_dir = _normalizeBuildDir(parent_domains.get("build_dir"), base_dir)
    domain_build_dirs = _getDomainBuildDirs(parent_domains, base_dir)

    if build_dir in domain_build_dirs and parent_build_dir and parent_build_dir not in query_build_dirs:
        query_build_dirs.append(parent_build_dir)

    return query_build_dirs
