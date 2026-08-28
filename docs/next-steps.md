# Raw touch stream — next steps

Resumable-from-zero work list, in rough priority order, as of 2026-08-27.
Context: the module architecture is the daily driver on `main` (protocol
v3, both pads streaming, USB + BLE verified) — see
[raw-touch.md](raw-touch.md) for the full state and
[module-publish-brief.md](module-publish-brief.md) for the publish plan.
Each item below is self-contained enough to start cold.

## a. Fix the dead-pad boot race (firmware, real bug)

In `~/src/cirque-input-module@intree-driver`, patch 3/3
(force-recalibrate-on-init) can leave SW_CC stuck on some boots: DR then
never fires, the pad is dead, and the keys work fine — it looks like a
transport bug but is not. Hit once in the wild (LH pad); power-cycling
the half recovers it. Fix in the patch itself: **clear SW_CC and
re-verify DR after the recalibrate**, push the branch, bump the pinned
revision in `config/west.yml`, then reflash **both halves**. This is
also a MUST-FIX before upstreaming the ERA/recalibrate patches to Zephyr
(see [upstreaming-todo.md](upstreaming-todo.md)).

## b. Re-land the LinearMouse cleanup, item by item

`~/src/linearmouse@go60-inputscale`: the review-pass cleanup batch
`a204c40` caused a live regression (first touch dead, then added
latency) within a minute of deploy and was reverted wholesale in
`29a8f88` — root cause never isolated; the archive building clean proved
nothing. The revert commit lists the seven items. Re-land them **one at
a time, with a scroll test between deploys**, and announce every deploy.

## c. LH pointer choppiness

Left-pad pointer motion is slightly choppy — the split transport relays
frames in bursts. Scroll is immune (v3 device timestamps let the host
reconstruct the true cadence); pointer is not (mouse reports are
arrival-timed). Plan: first **measure** pad-1 frame spacing via the
stream timestamps, then either tune the split relay, or — the elegant
fix — have the host synthesize pointer motion from the stream frames
themselves, which get the device-clock treatment for free.

## d. Demo video

Shot list agreed: **30-second cut, cold-open on the catch, a contrast
cut with LinearMouse quit** (÷24 wheel fallback vs the stream), **hands
cam tight on the right half**. The module README is written demo-first;
the release leads with this clip.

## e. Public release train

In order: host cleanup re-landed (b) → video (d) → flip
`kalakris/zmk-raw-touch` public → un-vendor (uncomment the west entry in
`config/west.yml`, delete `vendor/`, drop the two `cmake-args` from
`build.yaml`) → announce.

## f. Notarized releases (optional)

The LinearMouse fork inherits upstream's release pipeline; making it
produce installable signed builds needs a paid Apple Developer Program
membership, ~7 repo secrets (signing cert, notarization credentials,
etc.), and a tag push. Optional extra: build provenance via
`actions/attest-build-provenance`.

## g. Upstream patches

- Cirque driver patches to **Zephyr** (0xFF/SW_DR guard, ERA edge
  sensitivity — after fixing (a)): **message Pete Johanson first**; the
  patches are his unfinished migration.
- `inputScale` PR to **LinearMouse** (first two commits of the fork,
  self-contained), framed as generalizing
  `LogitechHighResolutionWheelNormalizer`.

## h. Delete the `kalakris/zmk` fork

Nothing depends on it; `cfc4b3e6` is already salvaged as
`patches/zmk-skip-empty-mouse-report-syncs.patch` on `main`. Just
delete the GitHub repo and `~/src/zmk`.
