# Zephyr documentation Sphinx extensions

In-tree Sphinx extensions used to build the Zephyr documentation. They are listed in
[`doc/conf.py`](../conf.py)'s `extensions`, which puts `doc/_extensions` on `sys.path` and
makes `zephyr.*` importable.

`moderncmakedomain/` is a vendored, unmodified copy of
[sphinxcontrib-moderncmakedomain](https://pypi.org/project/sphinxcontrib-moderncmakedomain/)
and is excluded from linting; the rest of this document covers the `zephyr` package.

## Extensions

| Module | What it provides |
|---|---|
| `api_overview` | `api-overview-table`, the table on `develop/api/overview.rst`, built from the Doxygen XML |
| `application` | `zephyr-app-commands`, the build/flash/debug command snippets |
| `build_timer` | Reports build wall-clock time and incremental speedup |
| `domain` | The `zephyr` domain: code samples, code-sample categories, the board catalog and board pages |
| `doxybridge` | `doxygengroup`, and resolution of C-domain references into the Doxygen HTML |
| `doxyrunner` | Runs Doxygen before Sphinx reads sources, and owns where its output lands |
| `doxytooltip` | Hover tooltips on the C-domain links `doxybridge` produces (JS/CSS only) |
| `doxyxref` | The reverse direction: rewrites Doxygen HTML placeholders into links back into the Sphinx docs |
| `dtcompatible_role` | The `dtcompatible` cross-reference type used by the generated bindings docs |
| `external_content` | Copies documentation living outside `doc/` into the Sphinx source directory |
| `gh_utils` | Jinja filters for GitHub blob/edit/issue URLs and last-commit metadata |
| `html_redirects` | Static redirect stubs for pages that have moved |
| `kconfig` | The `kconfig` domain and the client-side Kconfig option browser |
| `link_roles` | `zephyr_file`, `zephyr_raw` and `module_file`, resolved through the west manifest |
| `manifest_projects_table` | `manifest-projects-table`, the west manifest project listing |
| `partial_build` | Keeps `SKIP_*` preview builds warning-free under `-W` |
| `_paths` | Not an extension: shared tree locations and URI helpers (see below) |

## Dependencies between extensions

Each edge below is declared in code with `app.setup_extension()`, so the order of
`conf.py`'s `extensions` list does not matter. Add a `setup_extension()` call whenever an
extension starts relying on another's config values, directives, roles or domains.

| Consumer | Depends on | Why |
|---|---|---|
| `doxybridge` | `doxyrunner` | `doxyrunner_projects`/`doxyrunner_skip`, and the Doxygen XML |
| `doxyxref` | `doxyrunner` | same config, and the Doxygen HTML it rewrites |
| `api_overview` | `doxyrunner` | same config, and the Doxygen XML |
| `domain` | `doxybridge` | `DoxygenGroupDirective`, which `ZephyrDoxygenGroupDirective` overrides |
| `domain` | `gh_utils` | `gh_link_get_url()` and the `gh_link_*` config values |
| `domain` | `link_roles` | the `zephyr_file` role, resolved when rendering board hardware tables |
| `partial_build` | `api_overview`, `domain`, `kconfig` | it looks their directives up in the registry to stub them out |

Two couplings are *not* expressed this way and are worth knowing about:

- `doxytooltip`'s JavaScript targets the exact node shape `doxybridge` emits
  (`a.reference.internal` wrapping `code.c`). Nothing enforces this.
- `doxyxref` reads `app.env.kconfig_all_names` if `zephyr.kconfig` populated it, and treats
  its absence as "Kconfig was not built" rather than as an error.

### Doxygen output

`doxyrunner_projects` in `conf.py` is the single source of truth for where Doxygen output
goes. Consumers must not re-derive it; call `doxyrunner.doxygen_outputs(app.config)`, which
returns each project's `root`, `html` and `xml` directories, reading the subdirectory names
from the Doxyfile. It returns nothing when `doxyrunner_skip` is set, which is what makes
`SKIP_DOXYGEN` builds work without any extra configuration.

### The source directory is rewritten mid-build

`external_content.sync_contents()` **deletes** everything in `app.srcdir` that is neither
copied by `external_content_contents` nor listed in `external_content_keep`. The
`build/dts` and `build/requirements` entries in `external_content_keep` exist to protect
trees written by the `devicetree` and `requirements` CMake targets before Sphinx starts;
they must be kept in sync with [`doc/CMakeLists.txt`](../CMakeLists.txt) by hand.

## Event handlers and priorities

Sphinx's default listener priority is 500, and handlers at equal priority run in
registration order. Explicit priorities below are the ones that carry a real ordering
requirement.

| Event | Priority | Handler |
|---|---|---|
| `config-inited` | 500 | `partial_build._configure` |
| `builder-inited` | **100** | `build_timer._on_builder_inited` — starts before every generator |
| | **200** | `external_content.sync_contents` — the only handler that deletes from `srcdir` |
| | **300** | `doxyrunner.doxygen_build` — produces the Doxygen output |
| | **400** | `doxybridge.doxygen_parse` — consumes it |
| | 500 | `domain` static path + `load_board_catalog_into_domain`, `gh_utils.add_jinja_filter`, `kconfig.kconfig_build_resources` |
| `env-before-read-docs` | 500 | `build_timer._on_before_read_docs` |
| `env-updated` | 500 | `domain.compute_sample_categories_hierarchy` |
| `html-page-context` | 500 | `domain.install_static_assets_as_needed`, `kconfig.kconfig_install` |
| `build-finished` | 500 | `doxyxref.doxyxref_resolve`, `html_redirects.create_redirect_pages` |
| | **900** | `build_timer._on_build_finished` — reports after everything else |

### Post-transform ladder

All of these must run before Sphinx's `ReferencesResolver`, which would otherwise warn
about references they are responsible for resolving.

| Priority | Transform |
|---|---|
| 5 | `doxybridge.DoxygenReferencer` |
| 6 | `domain.ProcessCodeSampleListingNode` |
| 7 | `domain.CodeSampleCategoriesTocPatching` |
| 8 | `domain.ProcessRelatedCodeSamplesNode` |
| 9 | `partial_build._PlainTextCReferences` |
| 10 | *Sphinx `ReferencesResolver`* |

`domain` also registers three read-phase transforms at priority 100
(`ConvertCodeSampleNode`, `ConvertCodeSampleCategoryNode`, `ConvertBoardNode`).

## Shared code

`zephyr/_paths.py` holds what would otherwise be recomputed per extension: `ZEPHYR_BASE`,
`DOC_DIR`, `SCRIPTS_DIR`, `add_script_paths()` for the `sys.path` bootstrap,
`resources_dir()` for an extension's `static/` directory, and `relative_uri()` /
`outdir_relative_uri()` for linking to generated files.

It is deliberately *not* `zephyr/__init__.py`: that module runs before every `zephyr.*`
extension and is re-imported by the worker processes `doxybridge` spawns, so it is kept
empty. `_paths` imports nothing from `zephyr.*` for the same reason.

### Why there are three `sys.path` roots

- `doc/_extensions` — makes `zephyr.*` importable; added by `conf.py`.
- `doc/_scripts` — generator modules shared with the CMake targets (`gen_boards_catalog`,
  `dts_binding_types`, `redirects`). These stay top-level modules rather than moving into
  the `zephyr` package because CMake also runs them as standalone scripts.
- `scripts/…` — Zephyr's own tooling (`kconfiglib`, `list_boards`, `zephyr_module`,
  `get_maintainer`, python-devicetree).

Use `add_script_paths()` rather than writing `sys.path.insert` calls. Because it is a
function call rather than a literal `sys.path` assignment, ruff no longer recognises the
imports that follow as intentionally late, so they need `# noqa: E402`.

## Conventions

- Extensions are linted: `ruff check doc/_extensions doc/conf.py` and `ruff format --check`.
- `setup()` should return `parallel_read_safe` and `parallel_write_safe`; CI builds with
  `-j auto`.
- Declare `env_version` on any extension that caches non-trivial state on `app.env`, and
  bump it when the shape of that state changes; otherwise a stale `_build/doctrees` is
  silently reused.
- CI builds with `-W --keep-going`, so any new warning fails the build.

## Testing

There is no unit-test suite for these extensions. The checks that exist are:

```sh
make -C doc html            # the full build, as CI runs it
make -C doc html-minimal    # every SKIP_* switch on; the only thing exercising partial_build
make -C doc html SKIP_DOXYGEN=1
ruff check doc/_extensions doc/conf.py
```

Note that CI runs neither `html-minimal` nor `SKIP_DOXYGEN=1`, so changes touching
`partial_build` or the Doxygen consumers should be exercised locally.
