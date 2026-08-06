# Remaining "PureDarwin" occurrences (intentional)

OpenOSX is a deep-renamed successor of PureDarwin. All *functional* uses of the
old name (CMake project/variables, the `__PUREDARWIN__` macro, `PUREDARWIN_*`
env vars, `org.puredarwin.*` bundle identifiers and launchd job labels, Nix
package names, image/runner names) have been renamed by
`tools/rename-openosx.pl`, which is replayable after upstream syncs.

Occurrences that remain are **intentional and must not be renamed**:

| Where | Why it stays |
|---|---|
| `PUREDARWIN_LICENSE.txt` | License text authored by the PureDarwin developers; body, `@PUREDARWIN_LICENSE_HEADER_*@` markers, and filename stay verbatim. |
| `@PUREDARWIN_LICENSE_HEADER_START/END@` markers in source headers | License markers — legal text. |
| Copyright lines anywhere (`Copyright … PureDarwin Project` etc.) | Authorship attribution; APSL and the PureDarwin license require preserving notices. |
| `github:PureDarwin/*` flake inputs and other upstream URLs | Live external dependencies (xnu-loader, kc-tools, iig-tools) and credit links. |
| `README.md` Heritage / License sections | Deliberate credit and license pointers. |
| `tools/xnu-loader/`, `tools/kc-tools/` vendored subtrees | Kept pristine for clean subtree pulls from upstream. |

The rename script's guards encode these rules (line-level: copyright lines,
upstream URLs; path-level: license files, vendored subtrees). To re-apply after
merging upstream `next`:

```sh
perl tools/rename-openosx.pl
```

Anything matching `puredarwin` (case-insensitive) outside these categories is a
regression — check with `git grep -i puredarwin`.
