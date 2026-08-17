# Keymap Drawings & Desktop Overlay

Every push renders both keymaps to SVG and commits them back to the repo. An
[Übersicht](https://tracesof.net/uebersicht/) widget shows one of those drawings
on the macOS desktop, refreshing itself from the remote so a `git push` is all
it takes to update what's on screen.

```
config/*.keymap  ──push──▶  Build and Draw workflow  ──[Draw] commit──▶  keymap-drawer/*.svg
                                                                              │
                                                            widget git fetch  ▼
                                                                        desktop overlay
```

## Drawings

| File | Board |
|---|---|
| `keymap-drawer/eyelash_sofle.svg` | Eyelash Sofle |
| `keymap-drawer/go60.svg` | MoErgo Go60 |

Both are produced by the `draw` job in `.github/workflows/build-and-draw.yml`,
which calls [keymap-drawer](https://github.com/caksoylar/keymap-drawer)'s
reusable workflow. Styling lives in `keymap_drawer.config.yaml`.

### Physical layouts

keymap-drawer needs to know where the keys physically are. The reusable workflow
looks for `boards/*/*/<keyboard>-layout{,s}.dtsi` in the repo, which covers the
Sofle (`boards/arm/eyelash_sofle/eyelash_sofle-layouts.dtsi`). The Go60 board is
not in this repo — it lives in the ZMK fork, which the draw job deliberately does
not fetch (`west config manifest.project-filter " -zmk,-zephyr"`). So its layout
is vendored at `config/go60-layouts.dtsi` and passed explicitly:

```yaml
draw_args: "go60:'-d config/go60-layouts.dtsi'"
```

That file is a copy of `app/boards/arm/go60/go60-layouts.dtsi` from
`moergo-sc/zmk@go60-zmk0.3.0` **with one fix**: upstream lists the last 12 keys
as `[LH bottom][LH thumbs][RH thumbs][RH bottom]`, but `matrix_transform0` in
`go60.dtsi` numbers those positions `48-50 LH bottom, 51-53 RH bottom,
54-56 LH thumbs, 57-59 RH thumbs`. keymap-drawer maps key *order* in the layout
to key *position*, so without the swap the thumb legends render on the bottom row
and vice versa. It is used for drawing only — the firmware build never sees it.

### Rendering locally

Useful for checking a keymap change before pushing:

```bash
uv venv /tmp/kd && VIRTUAL_ENV=/tmp/kd uv pip install keymap-drawer
KD=/tmp/kd/bin/keymap
$KD -c keymap_drawer.config.yaml parse -z config/go60.keymap > keymap-drawer/go60.yaml
$KD -c keymap_drawer.config.yaml draw keymap-drawer/go60.yaml \
    -d config/go60-layouts.dtsi > keymap-drawer/go60.svg
```

For the Sofle, swap the keymap file and use
`-d boards/arm/eyelash_sofle/eyelash_sofle-layouts.dtsi`.

## Desktop overlay

The widget lives in `ubersicht-widget/keymap.widget/` and is installed by copying
it into `~/Library/Application Support/Übersicht/widgets/`.

```bash
./ubersicht-widget/setup.sh   # first time: installs Übersicht, adds a login item
./ubersicht-widget/sync.sh    # after editing the widget
```

The widget runs a shell command every 60 s that:

1. kicks off a **backgrounded** `git fetch origin` (so a slow or absent network
   never stalls the widget — the fetched drawing shows up on the next refresh);
2. prints `origin/<current-branch>:keymap-drawer/<BOARD>.svg`, falling back to the
   working-tree copy if origin has no drawing yet.

So the overlay tracks what CI has drawn for the branch you're on, without needing
a local `git pull`.

### Customising

Edit `ubersicht-widget/keymap.widget/index.jsx`, then run `sync.sh`:

| Knob | Effect |
|---|---|
| `const BOARD` | `"go60"` or `"eyelash_sofle"` |
| `refreshFrequency` | How often the SVG is re-read (ms) |
| `max-width` / `max-height` on `svg.keymap` | Overlay size; the drawing scales to fit |
| `bottom` / `right` | Screen position |
| `opacity` | Transparency |

The Go60 drawing is ~2120×2114 px for 10 layers in a 2-column grid (set by
`n_columns` in `keymap_drawer.config.yaml`), so at native size it is taller than
any screen — hence scale-to-fit rather than a fixed `transform: scale()`.
