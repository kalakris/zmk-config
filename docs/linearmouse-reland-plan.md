# LinearMouse cleanup re-land plan (next-steps item b)

Static analysis of `a204c40` (the reverted review-pass batch) vs the live
regression (first touch dead, then motion with added latency, both USB and
BLE, cleared by rolling the app back with the new firmware kept). Prepared
2026-08-28 without building or deploying anything.

The batch's commit message has **eight bullets** that the revert counts as
seven — the two `TouchStreamFrame` bullets (no-heap parse + strict parse)
are one file rewrite. They are split here because their risk differs.

**Structural constraint discovered:** `d4c9403` (pad-1 arbitration) landed
*after* the revert, on top of the pre-batch multi-pad code. It touched
`TouchStreamManager`, `TouchScrollEngine`, `TouchStreamCapabilities`, and
the engine tests. Consequences: (1) **item 7 is dead — do not re-land it**
(the per-pad `padStates` dict now carries pad 1's clock/watchdog for LH
scrolling; a pad-0-only collapse would break the LH pad); (2) most hunks
no longer apply cleanly and need hand-adaptation; only
`TouchStreamFrame.swift` is untouched by `d4c9403`.

## The items

| # | Item | Suspicion | Why |
|---|------|-----------|-----|
| 1 | Stale-touch watchdog → lazy deadline (armed at touch begin, re-arms for remainder on fire) | **High** | The only genuinely new stateful timing logic on the live gesture path. A fault here synthesizes spurious lift-offs mid-gesture — which *is* "touch goes dead, motion resumes laggy". No smoking gun found statically (arrival is captured before the v3 clock rewrite; re-arm math checks out), but live timer behavior at 100 Hz is exactly where static reading lies. |
| 2 | Synthesized stale release routes through `process(frame:)` | **Medium** | The release now passes every process() guard (scrollingEnabled, pad, contact) and the bookkeeping tail; timestamp changed from `lastTimestamp + 150 ms` to `lastTimestamp + elapsed` (host-clock elapsed on the engine timeline). Only active after 150 ms silences, so it can't alone explain steady latency — but it feeds the engine's momentum seed. |
| 3 | Parse from `UnsafeRawBufferPointer` (no per-report heap alloc) | **Medium-low** | Offset math verified byte-for-byte identical to the old v2/v3 parses; the prepended-report-ID condition is unchanged. Residual risk is only pointer-lifetime misuse, and the frame copies all fields out during init. |
| 4 | Strict parse per verified protocol version (v2-length fallback removed, short frames rejected) | **Medium-high** | Turns previously-parsed frames into silent drops. Any real-transport length quirk (truncated frame, unexpected padding) now kills frames — dropped touch-begin frames = dead first touch; a thinned stream = laggy motion. The fallback was defensive-only per `51a1659` (never observed in the wild), and a v2-parse of a truncated v3 frame yields garbage anyway — but "drop silently" failure modes fit the symptom best of anything in the batch. |
| 5 | Report-ID check before the lock-taking verified-device gate | **Low** | Pure reorder of two rejecting guards; every input reaches the same verdict. |
| 6 | Merge the two locked mirrors into one `VerifiedDevice` array | **Low** | Same lock, same single writer, same readers projecting the same fields. |
| 7 | `padStates` → single pad-0 state + pad-0 guard in process() | **Low** (for the regression) | Externally equivalent at the time (the engine already hard-filtered pad ≠ 0). **Obsolete now — skip.** |
| 8 | Capabilities: drop no-op `maxContacts` store, factor `presentPadIDs` | **Low** | v3 path semantically unchanged; `Pad`'s memberwise default `maxContacts = 1` confirmed, which is what the deleted v2 store set. |

No single item is *provably* the culprit from reading; no dangerous pair
was found either — items 1+2 are the only real interaction (they rewrite
the same function) and are separated in the order below.

## The environmental theory

The revert's own A/B was strong: rollback swapped **only the app build**
(same firmware, same session, rollback was itself a deploy+restart), and
the symptom cleared — so restart/HID-reopen effects are already ruled out.
The one environmental variable *not* controlled is the per-copy
Accessibility/TCC grant: posting synthetic scroll events and the wheel-
suppression tap both need it, and grants bind to specific app copies (see
raw-touch.md's accessibility-loop trap). An ungranted or half-granted new
copy could plausibly produce dropped/laggy posted events while the old,
granted copy works. That fits the symptom shape; it does not fit
"build-and-install.sh signs so the grant persists" being normal.

**Discriminating test (do this first, one deploy, zero code):** build and
deploy the exact `a204c40` tree (`git checkout a204c40`, build, install),
confirm the AX grant is present, scroll. Symptom reproduces → batch
guilty, proceed item-by-item. Doesn't reproduce → the original deploy's
grant state was the culprit; land everything (minus item 7) in one or two
batches. Note: this build predates arbitration, so LH scroll being dead is
*expected* there — judge only the RH pad. Roll back to HEAD's build after.

## Re-land order and mechanics

Do **not** `git revert 29a8f88` — it re-lands all eight at once, restores
the obsolete item 7, and conflicts with `d4c9403`. Instead hand-apply per
item on `go60-inputscale`, one commit per item, using the batch as the
reference diff: `git show a204c40 -- <file>`. Only
`TouchStreamFrame.swift` could take a file checkout
(`git checkout a204c40 -- …`), but that lands items 3+4 together plus the
manager call-site — prefer the split below.

1. **Item 6** (VerifiedDevice merge) — clean refactor, hand-apply; hunks
   shift slightly under `d4c9403` but don't overlap it.
2. **Item 8** (capabilities cleanup) — hand-apply; `d4c9403` touched this
   file, so no file checkout.
3. **Item 5** (guard reorder) — two-hunk swap in `handleReport`.
4. **Item 3** (buffer parse, **keeping** the v2-length fallback for now) —
   new `UnsafeRawBufferPointer` init with the fallback logic ported into
   it, plus the `handleReport` call-site hunk.
5. **Item 2** (stale release via `process(frame:)`) — adapt: keep
   `padID: padID` on the synthesized frame; routing through process()
   subsumes `d4c9403`'s explicit `activeScrollPad` guard in
   `handleStaleTouch` (the release frame doesn't claim — `frame.touched`
   is false — and non-active pads drop at the arbitration gate). Check the
   arbitration tests still pass.
6. **Item 4** (strict parse — remove the fallback) — separate deploy so a
   frame-length surprise is isolated from item 3's mechanics. Dovetails
   with [next-steps item (i)](next-steps.md) (drop protocol v2 from the
   host entirely): item 4 removes the v2-*length* fallback, item (i)
   removes the v2 layout altogether. Consider landing (i) as the deploy
   right after item 4 survives its scroll test.
7. **Item 1** (lazy watchdog) — last, most suspect. Adapt to the per-pad
   dict: `lastTouchedFrameTime` and the lazy re-arm live in each
   `PadStreamState`. While testing, watch Console for
   `silent for … ms while touched; synthesizing lift-off` — that log
   firing mid-gesture is the smoking gun.

Skip item 7 entirely.

## Per-deploy scroll test (~30 s)

Announce the deploy first (standing rule). Then:

1. **First touch**: immediately after the app is up, one RH-pad scroll —
   the very first touch must move content.
2. **Momentum + catch**: flick, let it coast, catch it mid-coast.
3. **LH pad**: one scroll gesture (arbitration path).
4. **Nav-layer**: hold Nav, scroll — smooth, no double-rate (wheel
   suppression intact).
5. **Pointer**: cursor moves normally off-Nav.
6. **Watchdog** (matters from item 2 onward): rest a finger ~1 s, then
   move — motion must not have been killed by a synthesized lift-off.

Any failure: `git revert` that item's commit, redeploy, and the culprit is
isolated — which was the whole point.
