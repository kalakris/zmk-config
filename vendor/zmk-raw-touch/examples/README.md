# Examples

The configuration files the main README walks through. They are the source
of truth for its code blocks: `scripts/check-readme-examples.py` fails CI
if a README block differs from its file (the README's diff blocks are
generated from the `before`/`after` pairs), and the CI firmware build
compiles the "after" files on a MoErgo Go60 against the pinned trees in
[`ci/`](../ci).

The files use the README's generic labels (`trackpad`, `trackpad_split`,
`remote_trackpad_listener`, ...). Rename them to your board's nodes, or map
them with `#define` as `ci/go60_rh.keymap` and `ci/go60_lh.keymap` do.

| File | README section | Compiled by CI |
|---|---|---|
| `trackpad/before.overlay` | Start from a trackpad configuration | no (the relative-mode starting point) |
| `trackpad/raw-touch.overlay` | Add raw touch to that configuration | `go60_rh` |
| `trackpad/dedicated-scroll.overlay` | Dedicated scrolling pad | `go60_rh`, on top of the above |
| `split/shared.dtsi` | Shared definitions | no (the Go60 board defines these nodes itself) |
| `split/peripheral-before.overlay` | Peripheral: before | no |
| `split/peripheral.overlay` | Peripheral: with raw touch and the split stamp | `go60_lh` |
| `split/central-before.overlay` | Central: before | no |
| `split/central.overlay` | Central: with raw touch | `go60_rh` |

The "before" files exist so the README's diffs are generated rather than
hand-maintained; they are not built.
