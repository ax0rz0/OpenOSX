<!-- SPDX-License-Identifier: MIT -->

# PureDarwin wlroots licensing

This directory contains wlroots 0.20.1, copied from the pinned nixpkgs
source used for the PureDarwin build.

The wlroots source is licensed under the MIT License. The complete upstream
license and copyright notice are retained in `LICENSE`.

The following upstream material is intentionally excluded:

- `protocol/server-decoration.xml`, GPL-licensed protocol description.
- `tinywl/`, CC0 example compositor and its accompanying example license.

Dependencies and generated protocol sources retain their own licensing and
are not relicensed by this file. PureDarwin additions must carry an SPDX
identifier in their file header and must not copy code from excluded material.
