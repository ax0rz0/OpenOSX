# Remaining "PureDarwin" occurrences (intentional)

OpenOSX is a deep-renamed successor of PureDarwin. All *functional* uses of the
old name (CMake project/variables, the `__PUREDARWIN__` macro, `org.puredarwin.*`
bundle identifiers) have been renamed. The occurrences below are **intentional
and must not be renamed** — they are legal attribution or historical credit:

| Where | Why it stays |
|---|---|
| `PUREDARWIN_LICENSE.txt` | License text authored by the PureDarwin developers; body, markers, and filename stay verbatim. |
| `README.md` (Heritage / License sections) | Deliberate credit and license pointers. |
| `src/Kernel/Extensions/*/Info.plist` copyright strings | Authorship attribution (NSHumanReadableCopyright etc.). |
| `src/Kernel/Extensions/AppleI386GenericPlatform/*.{h,cpp}` file headers | APSL attribution headers (William Kent / PureDarwin). APSL requires preserving notices. |

Anything matching `puredarwin` (case-insensitive) outside this list is a
regression. Check with:

```sh
git grep -i -l puredarwin -- ':!build' | sort
```

and compare against the file list above.
