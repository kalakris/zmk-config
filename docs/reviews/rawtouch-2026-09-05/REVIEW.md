# RawTouch open-source release review — 2026-09-05

The scrolling engine has substantial test coverage, the module is separated from ZMK core, and the host/firmware contract is documented. I would fix the lifecycle and transport findings below before calling this a supported v0.1.0 release. Successful daily use and green CI do not exercise these transitions.

This was a review, not a repair or deployment. Neither release repository was modified. The new files in this directory contain the review, isolated regression tests, source-extraction harnesses, and validation logs.

## Scope and evidence

| Component | Reviewed revision |
| --- | --- |
| `/Users/kalakris/src/rawtouch` | `902398811fab25fce168fc3f9d95684a538fe7d7` |
| `/Users/kalakris/src/zmk-raw-touch` | `1f58b7fa550cbd1c355c361dda746863d248e48a` |
| `/Users/kalakris/zmk-config` integration | `c5fafdc1b47357f1da9e5bacfc7d2d659e5e445e` |

Both release repositories were clean. The vendored module matches its source; the only differences found were vendoring metadata and an empty directory. Existing unrelated changes in zmk-config were preserved.

Read the repository's CLAUDE.md, next-steps and publication documents, plus Claude Code's project memory index, release-prep memory, and environment notes. These establish why the claim, per-pad geometry, physical gain, separate host, and existing deployment choices look the way they do. I independently inspected the current implementation, rather than treating earlier review notes as proof.

Validation on this Mac, in an isolated copy under `/private/tmp/rawtouch-review.EjKN1g`:

- Original `swift test`: **258 tests passed**.
- `swift build -c release`: **passed**, including the app and CLI executables.
- `scroll-bench --offline`: **passed its assertions**. This is deterministic simulation, not a new Safari or hardware feel test. Its default printed table exercises 0 and 5 ms latency, not the shipped 10 ms Bluetooth default.
- Three new regression tests: **all three fail**, with five failed assertions. They reproduce the untrusted startup configuration, active-pad disable lockout, and axis-change jump described below.
- A C harness using the actual queue/drain functions reproduces delivery of a report queued for BLE host A to BLE host B after a profile switch.
- A separate C harness demonstrates two possible lease-state interleavings. This is a concurrency model, not proof that the reference Go60's thread priorities permit those interleavings; see the qualified finding below.
- Existing firmware [Build and Draw run 33950631268](https://github.com/kalakris/zmk-config/actions/runs/33950631268) succeeded for the reviewed configuration commit. I did not trigger another build or flash hardware.
- A targeted credential-pattern scan over all locally reachable historical blobs found no matches: 352 app blobs and 111 module blobs. It checked common GitHub/OpenAI/AWS token formats and private-key headers; this is not an exhaustive secret or personal-data audit.

No app was launched, quit, installed, or granted Accessibility. No real scroll events were posted by the tests. Older macOS versions, Intel, live reconnection, first pairing, and a fresh user's install remain outside this run's validation.

## Findings

### 1. [P1] Missing Accessibility still disables firmware scrolling

Location: [AppModel.swift:158](/Users/kalakris/src/rawtouch/Sources/RawTouchAppCore/AppModel.swift:158), [RawTouchDeviceManager.swift:253](/Users/kalakris/src/rawtouch/Sources/RawTouchCore/RawTouchDeviceManager.swift:253), and the CLI's permission check.

The app starts the service with `enabled = true` before checking Accessibility, and the trust result only changes UI state. The device manager has no posting-readiness input: once validation succeeds it claims and keeps refreshing. The CLI likewise logs missing permission and continues into an enabled service. Firmware then suppresses its wheel reports, while macOS cannot accept the replacement scroll events. A new user's first launch can therefore remove working Standard-mode scrolling indefinitely while onboarding is open. Revoking permission after startup is also not reconciled with claim ownership; polling stops after trust is granted.

Keep device discovery available, but make effective claim ownership depend on both user intent and permission to post. Release on loss of readiness and reacquire on recovery, without changing the user's saved enabled preference. Apply the policy in the shared lifecycle so app and CLI agree. The added trust test confirms that an untrusted app currently starts an enabled manager; the actual claim consequence follows directly from the manager's validation path.

### 2. [P1] Disabling during the initial asynchronous claim can leave scrolling dead for 30 seconds

Location: [RawTouchDeviceManager.swift:262](/Users/kalakris/src/rawtouch/Sources/RawTouchCore/RawTouchDeviceManager.swift:262), [claim completion at line 645](/Users/kalakris/src/rawtouch/Sources/RawTouchCore/RawTouchDeviceManager.swift:645).

Repro sequence from the code: a newly validated device queues a claim on `reportQueue`; before its result reaches the main queue, the user disables scrolling. `gateClaimed` is still false, so the disable loop skips releasing that device and stops the refresh timer. The queued claim then succeeds. Firmware stops wheel scrolling, the host pipeline is disabled, and nothing releases the claim until its 30-second expiry. The menu nevertheless reports Standard mode because status is gated by `configuration.enabled`.

On disable, enqueue an ordered release for every admitted device that could have a pending claim, not just those with a completed-success flag. Track desired state or a generation so obsolete claim completions cannot overwrite newer state. The serial queue already provides the ordering needed for claim-then-release. This is a source-level finding; the real IOKit boundary currently has no injected transfer seam for a deterministic unit test.

### 3. [P1] Disabling an active pad can lock out the other pad indefinitely

Location: [TouchScrollPipeline.swift:242](/Users/kalakris/src/rawtouch/Sources/RawTouchCore/TouchScrollPipeline.swift:242), [disabled-frame guard at line 403](/Users/kalakris/src/rawtouch/Sources/RawTouchCore/TouchScrollPipeline.swift:403).

Live configuration removes the active pad from `padConfigs` without interrupting its engine or resampler. Subsequent frames, including its release and the watchdog's synthetic release, return at the disabled-pad guard. The old pad retains `activeScrollPad`; with resampling enabled its display link also keeps running. Another pad cannot take ownership while the engine remains touching.

The regression test begins scrolling on pad 0, disables it, delivers its release, advances past the watchdog, and tries pad 1. Pad 0 still owns the gesture, the ticker remains live, and pad 1 cannot scroll. Interrupt immediately when the active pad becomes disabled or disappears from the capabilities/configuration set. Cover both touching and coasting transitions.

### 4. [P2] BLE queued frames can cross host/profile boundaries

Location: [raw_touch_hog.c:394](/Users/kalakris/src/zmk-raw-touch/src/raw_touch_hog.c:394).

Queue entries contain only report bodies. Each drain fetches whichever BLE profile is active at that later instant, rather than the endpoint for which the report was sampled. Neither an endpoint switch nor disconnect invalidates the queue. In particular, a release waiting for its delayed retry can be delivered to a different host after a profile switch, with its old `host_claimed` bit still set. Old motion can cross the same boundary; a no-connection return can preserve it even longer. The host may interpret old data as current gesture input, and the documented endpoint-scoping guarantee is broken.

The extracted C harness queues a claimed release for A, simulates a transient notify failure, switches to B, and retries. It prints `queued for host 1, delivered to host 2, flags=6`. Bind queue entries to an endpoint/connection generation and discard obsolete generations; synchronise invalidation with any drain already in flight. Do not merely look up the active profile again at transmission time.

### 5. [P2] USB release frames are still silently lossy on a healthy but busy endpoint

Location: [raw_touch_usb_hid.c:251](/Users/kalakris/src/zmk-raw-touch/src/raw_touch_usb_hid.c:251), [raw_touch.c:173](/Users/kalakris/src/zmk-raw-touch/src/raw_touch.c:173).

Every USB report, including a lift-off or claim-clearing release, is dropped when `hid_sem` is busy. The frame producer ignores the send result and advances `stream_touched`/`prev_touched`, so the discarded release is never retried. With two pads sharing the interface, another pad's in-flight transfer is sufficient to take this path. It does not require a disconnect. The host is then dependent on its 150 ms watchdog; a quick new touch before that deadline can be joined onto the previous gesture.

Retain terminal reports for transmission after `in_ready_cb`, preserving order before a subsequent touch. Keep the existing protection against overwriting an in-flight DMA buffer. The protocol documentation should describe actual bounded delivery guarantees: BLE also gives up after four failures or when no evictable slot exists. The watchdog remains part of correctness under transport pressure, not solely a link-loss safety net. This finding is from the actual send and producer paths; USB saturation was not tested on hardware here.

### 6. [P2] Changing the selected sensor axis creates motion from a stationary finger

Location: [TouchScrollPipeline.swift:248](/Users/kalakris/src/rawtouch/Sources/RawTouchCore/TouchScrollPipeline.swift:248).

The live update changes `engine.config.axis` and resets the resampler's axis bookkeeping, but leaves the engine's `lastPosition` and velocity history on the old axis. Its next delta is therefore new Y minus old X, or the reverse. The regression test changes from X=1040 to an unchanged Y=800 and observes a **−60 point** scroll at the test's 0.25 pt/count gain, despite no Y movement.

End and re-anchor the gesture on an axis change, or defer this structural setting until the next gesture. Re-anchor both engine and resampler together; resetting only the resampler is insufficient.

### 7. [P2] The per-pad direction toggle cannot reverse a pad whose automatic inversion is true

Location: [SettingsView.swift:220](/Users/kalakris/src/rawtouch/Sources/RawTouchApp/SettingsView.swift:220).

The switch reads `pad.invert ?? false` and writes only `true` or `nil`. However, automatic inversion is `!invertY`; it is true for an ordinary pad without firmware `y-invert`. On such a pad, toggling on forces true and toggling off inherits true: neither reverses the direction, and the UI cannot express the required explicit false override. The Go60's mounting happens to hide this because its automatic value is false.

Expose automatic/normal/inverted as three choices, or make the boolean a reversal relative to the resolved firmware baseline. Preserve the ability to write explicit false. This is established by the view binding and configuration resolution, not a live UI test.

### 8. [P2] The HID read buffer is smaller than the documented maximum feature report

Location: [RawTouchDeviceManager.swift:591](/Users/kalakris/src/rawtouch/Sources/RawTouchCore/RawTouchDeviceManager.swift:591).

The module and host parser support eight pads: a 68-byte feature body, or 69 bytes with the USB report ID. The IOKit read allocates only 64 bytes. An eight-pad keyboard therefore cannot be read completely: a rejected read causes the device to be ignored; a shortened result would lose the last pad's geometry. Parser tests using full arrays do not test this transport boundary.

Size the buffer from `payloadLength(padSlots: maximumPadSlots) + 1` and validate the returned length. Add a transfer-boundary test for the maximum supported report, alongside one- and two-pad cases. Apple's [IOHIDDeviceGetReport documentation](https://developer.apple.com/documentation/iokit/1588659-iohiddevicegetreport) describes the caller-provided report buffer.

### 9. [P2, binary distribution] The app bundle omits its license and third-party notices

Location: [make-app.sh:29](/Users/kalakris/src/rawtouch/scripts/make-app.sh:29).

The bundle contains the executable, Info.plist and PkgInfo; Resources is created but no LICENSE or notices are copied into it. The Info.plist's short “MIT License” string does not include the permission notice or LinearMouse attribution. A downloadable app assembled by this script therefore does not carry the notices present in the source repository. The [MIT license text](https://opensource.org/license/mit) requires preserving its copyright and permission notice with copies/substantial portions.

Include LICENSE/third-party notices in the bundle and release archive. Refresh the derived-file inventory too: it still names the moved test fixture's old path and omits the derived pipeline. For the module, verify the Apache-2.0 provenance mentioned in `raw_touch_hog.c` against the actual inherited code and carry any required upstream notices; I have not established that its current MIT labeling is legally incorrect.

## Additional concerns and release decisions

**Lease locking needs a targeted concurrency check.** In [raw_touch_gate.c:99](/Users/kalakris/src/zmk-raw-touch/src/raw_touch_gate.c:99), clearing `engaged` and cancelling expiry are separate operations. A concurrent refresh between them can leave `engaged=true` with no expiry. Conversely, an expiry between setting `engaged=true` and rescheduling can cancel a freshly accepted claim. The extracted harness produces both states. However, Zephyr's default cooperative workqueue scheduling may prevent some interleavings on this single-core reference build; I have not proved a reachable Go60 execution, so this is a qualified portability/correctness concern rather than a reproduced hardware blocker. Store and check an absolute lease deadline under one synchronization policy, or establish and enforce the required scheduling constraints. Zephyr's [workqueue guidance](https://docs.zephyrproject.org/latest/kernel/services/threads/workqueue.html) specifically calls for shared-state synchronization and cautions against reasoning from transient pending/busy snapshots.

**Explicitly support one physical keyboard, or retain device identity.** The manager admits and claims every matching keyboard, but configures one pipeline from `streamDevices.first`; pad clocks and ownership are keyed only by pad ID. Two separate keyboards with pad 0 will share a state machine and the first keyboard's geometry. USB/BLE aliases of one keyboard are a different case. Either select one physical device and leave the others in Standard mode, or use device-and-pad identity throughout. Also decide how Fast User Switching should behave: the instance lock is per home directory, so two user sessions are not excluded by it.

**Device validation is weaker than the README suggests.** Capabilities admission mainly checks minimum length, version 3, and a nonempty pad bitmap; claim support is one more bit. It accepts truncated slot sets with guessed geometry and does not check plausible ranges/reserved fields. Before freezing the public protocol, decide whether a more distinctive identification field is warranted, and test malformed/unrelated collections before allowing feature writes. This is accidental-matching hardening, not authentication against a hostile USB device.

**Bring the public integration example up to the tested configuration.** The example explains a local SPI pad but does not show peripheral absolute-event forwarding and a central raw-touch node pointing at the split relay. The working Go60 configuration has this essential wiring. Add a split example and pin a tested manifest combination so a new user can reproduce the result without reading this personal config repository. Keep the macOS 13+, legacy USB-stack, tested ZMK/MoErgo version, single-touch and vertical-only limits prominent. Do not claim upstream ZMK/main compatibility from the vendor-fork CI result alone.

**Treat deployment polish separately from source publication.** Neither release repo contains a CI workflow. Add macOS build/tests and a pinned firmware example build; exercise the transfer/lease seams found here. A downloadable macOS release additionally needs a deliberately signed/notarized artifact, version/build provenance, checksums, architecture support stated accurately, and install/uninstall testing from a fresh account. Notarization is not a prerequisite to publishing experimental source. The icon, demo video, CONTRIBUTING file, release tag, public repo visibility and un-vendoring are still release work; their earlier deliberate deferral was respected.

**Correct a few public claims before promotion.** Describe the implementation as not collecting keystrokes, without promising that an Accessibility-authorized executable is technically incapable of accessing them. Avoid “the moment the host goes away” for crash fallback, which can take the 30-second lease. Align release-delivery language with finding 5. The LaunchAgent's `SuccessfulExit=false` restarts on exit 2 and 3 as well as crashes; decide whether retrying unsupported firmware or a second-instance conflict is desired. Its comment currently implies that the firmware error will not loop. Internal CLAUDE.md/AGENTS.md still call the old dead-pad boot race unfixed, while next-steps and the pinned driver record the fix; reconcile that context before future reviews/deploys.

## Suggested order

1. Fix posting readiness, asynchronous claim cancellation and active-pad teardown. Add deterministic tests at the device-transfer boundary as well as the existing pure pipeline tests.
2. Fix BLE endpoint ownership and USB release retention. Exercise contention, rapid retouch, profile switching, unplug/replug and claim expiry on the reference hardware.
3. Fix axis/inversion configuration transitions, maximum report sizing and bundle notices.
4. Add reproducible examples/CI and complete the chosen release packaging. Publish with an explicit tested-support envelope; keep unverified generalizations out of the release claims.

The retained Swift tests intentionally fail against the reviewed revision. Copy `ReleaseReviewTests.swift` into `Tests/RawTouchCoreTests` and `ReleaseReviewTrustTests.swift` into `Tests/RawTouchAppCoreTests` in an isolated app checkout, then run `swift test --filter ReleaseReview`. Both C harnesses are standalone and use extracted reviewed source functions with minimal scheduler/transport stubs; compile them with `clang -std=c11 <file.c> -o <temporary-output>`. Their assertions verify the observed bad states, not a repaired implementation.
