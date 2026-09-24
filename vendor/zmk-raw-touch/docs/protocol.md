# Wire format (protocol 4)

This document defines the HID reports `zmk-raw-touch` exchanges with a
host, for anyone implementing a host. [RawTouch](https://github.com/kalakris/rawtouch)
is the reference host for macOS. The protocol number is the only
compatibility contract between firmware and host; module and app
versions are diagnostic.

The current firmware sends one contact per pad; the presence of a
contact ID field does not imply implemented multi-touch support.

Like the rest of the repository, this specification is MIT-licensed.

## Collection and report framing

| Field | Value |
|---|---|
| Usage page | `0xFF00` |
| Usage | `0x01` |
| Report ID | `0x04` for input and feature reports |
| USB | Separate HID interface, currently `HID_1` |
| Bluetooth | Separate HID-over-GATT service instance |

The vendor usage pair is not unique to this project. A host **must read
and validate the feature report before interpreting input or acquiring
a lease on the device**: the four magic bytes, the protocol version, and the body
length must all match before a device is treated as this protocol, and a
device that fails the check must never be sent a lease command.
Discovery is protocol identification, not authentication.

All lengths and offsets below refer to report bodies. USB transfers
include a leading report ID; BLE uses the report-reference descriptor
and carries the body alone. Host HID APIs may expose the ID separately
or retain it in a returned buffer, so normalize framing before parsing.
In particular, macOS feature GETs have been observed ID-prefixed on USB
and bare on BLE.

## Input report

The body is **11 bytes**. All multi-byte integers are little-endian.

| Offset | Bytes | Field |
|---|---|---|
| 0 | 1 | `pad_id`, 0–7 |
| 1 | 1 | `contact_id`, currently always 0 |
| 2 | 2 | `x`, unsigned raw coordinate |
| 4 | 2 | `y`, unsigned raw coordinate |
| 6 | 1 | `z`, touch strength; not a calibrated force measurement |
| 7 | 1 | `flags` |
| 8 | 1 | `seq`, per-pad counter incremented for each emitted report, wrapping modulo 256 |
| 9 | 2 | `timestamp`, device-side time in 100 µs units, wrapping every 6.5536 seconds |

Flag bits:

| Bit | Name | Meaning |
|---|---|---|
| 0 | `touched` | A contact is present. Clear means release. |
| 1 | `scroll_mode` | The pad's events reached a processor chain marked for scrolling. |
| 2 | `lease_held` | The selected sending endpoint held a live host lease when the report was produced. |
| 3–7 | reserved | Sent as zero. |

While a lease is held, firmware emits reports for active samples (about 100 Hz
with the tested pads), including pointer-context samples, and one report
on lift-off. It suppresses repeated idle reports. A normal release has
`touched` clear and X/Y/Z zero; hosts **must use the flag**, not the
coordinate values, to determine contact state.

If the lease lapses mid-touch, firmware attempts one trailing release
with `touched` and `lease_held` clear, X/Y/Z zero, and `scroll_mode`
reflecting that frame's context. It then stops sending until a lease is
acquired again. That trailing release may not reach the previous host after an
endpoint switch or disconnection.

Hosts **must generate scrolling only while both `scroll_mode` and
`lease_held` are set**. They must still handle transitions out of those
states: losing scroll context ends the drag, and a trailing release
without the lease bit cancels it **without momentum**, because firmware scrolling has
resumed. Do not simply discard these transitions and leave a gesture open.

Use the device timestamp for velocity estimation, with wrap handling;
USB/BLE arrival times can be batched. The timestamp is taken during
module processing, not read from the sensor. On a split relay it is
taken after events reach the central, unless the peripheral runs the
module's stamp processor, in which case it is the peripheral's own
clock. Pads may therefore be in different clock domains: hosts must keep
one timeline per pad and must not compare timestamps across pads (the
timestamp field itself is unchanged either way). Sequence gaps indicate
reports missing from the delivered sequence; they do not measure events
lost before the module produced a report. Reconnects and endpoint changes
also require resetting or reconciling host timing state.

## Feature report

Read with USB `GET_REPORT(FEATURE)` or the BLE feature characteristic
(report-reference type `0x03`). Its body is **16 + 8 × N bytes**, for
1–8 configured pads. A two-pad build is 32 bytes, or 33 including the
USB report ID. Hosts must support variable pad counts and must not
hard-code the two-pad length.

| Offset | Bytes | Field |
|---|---|---|
| 0 | 1 | `protocol_version`, 4 |
| 1 | 1 | `pads_present`, bit `p` set for pad ID `p` |
| 2 | 1 | `capabilities`, bit 0 = host lease supported, bit 1 = tap confirm supported; other bits reserved |
| 3 | 1 | `module_version` of the half answering, packed major.minor; 0 = unknown |
| 4 | 4 | `magic`, the ASCII bytes `R` `A` `W` `T` (`52 41 57 54`) |
| 8 | 8 | `device_id`, the SoC's hardware identifier; all-zero = unknown |
| 16 + 8 × i | 8 | Geometry slot `i` |

The magic is fixed and is what separates this protocol from anything else
that happens to sit on `0xFF00`/`0x01`. `protocol_version` stays at byte 0
in every version of the protocol, so a host can read it before it decides
which layout to parse. **Reject** a body whose magic differs, whose
protocol version is not 4, or whose length is not `16 + 8 × N`, and do
not write a lease command to it.

`device_id` is the value Zephyr's `hwinfo_get_device_id()` returns for the
SoC — the FICR `DEVICEID` pair on an nRF52 — in the byte order hwinfo
returns it, zero-padded if the SoC reports fewer than eight bytes and
truncated if it reports more. It is stable for the life of the chip and
identical on every transport, which is what lets a host recognize that the
keyboard it sees over USB and the one it sees over Bluetooth are the same
keyboard; nothing else in either transport exposes a shared identifier.
All-zero means the firmware could not read one, which hosts must tolerate
along with every other value: **never reject or admit a device over this
field**. It is readable by any host that can read the feature report —
over Bluetooth, one that is bonded — so treat it as an identifier, not a
secret, and do not authorize anything on the strength of it. Note that a
board may already derive its USB serial number from the same hwinfo id
(the MoErgo Go60 does), in which case the two agree by construction.

For a valid firmware configuration, slots describe the present pads in
ascending pad-ID order. IDs need not be contiguous: a mask naming pads
0 and 3 has two slots, for pads 0 and 3. IDs must be unique and less
than 8; configurations exceeding that are not supported.

Recover the number of complete slots from the normalized body length as
`(len - 16) / 8` and read no more than `min(N, popcount(pads_present))`
slots. Valid bodies have the `16 + 8 × N` shape; a host that requires
exactly 32 bytes will refuse a one-pad or three-pad keyboard using the
same protocol.

Each slot contains:

| Slot offset | Bytes | Field |
|---|---|---|
| +0 | 1 | `resolution`, counts/mm; 0 = unknown |
| +1 | 1 | `orientation`: bit 0 swaps X/Y (`rotate-90`), bit 1 inverts X, bit 2 inverts Y; bit 3 = the pad parks scroll-context taps for host confirmation |
| +2 | 2 | `x_max`, unsigned little-endian |
| +4 | 2 | `y_max`, unsigned little-endian |
| +6 | 1 | `max_contacts`, currently 1 |
| +7 | 1 | `module_version` of the half that owns the pad, packed; 0 = unknown |

Orientation applies after reading raw coordinates: swap axes first,
then apply axis inversions. The HID descriptor also declares coordinate
ranges from one pad node; use the per-pad feature slots when pads differ.

Both module versions pack major in the high nibble and minor in the low
one: `0x01` is 0.1 and `0x10` is 1.0. Byte 3 is the half that answers the
report, the central. A slot's byte 7 is the half that owns that pad, which
for a pad relayed from a split peripheral is the peripheral — 0 until its
version announcement arrives. The peripheral announces before its first
frame after boot and before the next frame after a gap of at least 500 ms.
Treat both as diagnostic: `protocol_version` is the compatibility contract.

The configured pad count determines the descriptor's feature-report
length. Adding or removing a pad — or upgrading across a protocol version
that changes the body — therefore changes the report map and requires a
fresh Bluetooth pairing on hosts that cache it, including macOS.

## Host lease

Check `capabilities` bit 0 before writing. The feature report accepts this
**4-byte command body** through USB `SET_REPORT(FEATURE)` or a BLE
feature-characteristic write:

| Offset | Bytes | Field |
|---|---|---|
| 0 | 1 | Command `0x01` |
| 1 | 1 | `0x01` acquire or renew; `0x00` release |
| 2 | 1 | Lease timeout in seconds; nonzero to acquire or renew, clamped to 5–120; ignored for release |
| 3 | 1 | Reserved, must be zero |

For example, `01 01 1e 00` requests 30 seconds and `01 00 00 00`
releases. USB accepts a body with or without its leading report ID;
BLE writes carry only the body. Invalid USB requests are stalled. BLE
rejects a wrong length with `Invalid Attribute Value Length`, invalid
fields with `Value Not Allowed`, nonzero offsets with `Invalid Offset`,
and writes from a peer outside the bonded host profiles with
`Write Not Permitted`.

A lease belongs to the USB endpoint or the specific BLE profile that
wrote it. It suppresses wheel motion only while that endpoint is
selected. It covers all configured pads on that endpoint, not an
individual pad. Pointer-context relative motion, taps, and key reports
continue in both modes.

Hosts must renew at intervals no longer than half the effective lease,
and should release on a clean exit. Release is safe when no lease is
held. A lease lapses on expiry, USB detach/reset, disconnection of its
BLE profile, or an endpoint switch away from it, and ends on explicit
release. A lease already held by the newly selected endpoint can take
effect when switching to it. Switching back does not restore a lapsed
lease; the host must acquire it again.

Hosts must re-acquire leases after resume, reconnect, and device
re-enumeration. They should acquire a lease only when ready to generate
scroll events. RawTouch leaves the lease released when disabled or when
Accessibility permission is unavailable.

## Tap confirm

Check `capabilities` bit 1 before writing. A pad whose slot has byte +1
bit 3 set (`tap-click` with `tap-click-while-scrolling`) does not emit a
scroll-context tap by itself while a lease is held: at lift-off of a
touch that qualified as a tap by the firmware's own thresholds, it parks
the tap for 250 ms. The host confirms it with this **4-byte command
body**, framed exactly like the lease command:

| Offset | Bytes | Field |
|---|---|---|
| 0 | 1 | Command `0x02` |
| 1 | 1 | `pad_id` of the touch being confirmed |
| 2 | 2 | Reserved, must be zero |

The firmware honors the command only from the endpoint that holds the
lease and ignores it, without error, when nothing is parked or the
window has passed, so a host may confirm liberally. The host should
confirm exactly the scroll-context touches that were not catching a
coasting momentum tail; the firmware's own `tap-max-ms` and
`tap-max-movement` still apply, so a confirmation can never produce a
tap the keymap would not have. The emitted tap enters the pad's listener
chain like any other, so what it does is the keymap's decision. In
Standard mode, and for pads without bit 3, taps are never parked.

The RawTouch app additionally requires the touch to stay in scroll
context throughout, last no more than 300 ms, and move no more than
60 counts from touch-down on either raw axis. These are fixed host limits,
not protocol limits. They exceed the firmware defaults of 180 ms and
30 counts, but can reject taps allowed by larger firmware settings. A
missing release or lapsed lease does not receive a confirmation.

## Delivery and lost releases

Both transports use bounded queues shared by all pads. When full, a queue
evicts the oldest eligible motion frame, preserving queued releases and
the head currently being transmitted. If no entry is eligible, the
incoming report is dropped, even if it is a release. Queues preserve
order among retained reports; a new touch does not overtake an earlier
queued release.

- **USB:** transfer completion drains the queue. A busy interrupt
  endpoint can defer a release without immediately losing it. Bus reset,
  detach, and transport errors can flush pending reports.
- **BLE:** failed release notifications are retried at the head of the
  queue, 8 ms apart, for at most four attempts. Queued entries are bound
  to the profile selected at enqueue time. Endpoint switches flush the
  queue; disconnects discard entries for that profile. Old reports are
  not forwarded to a different host.

These measures protect releases during ordinary congestion; they do not
guarantee delivery. Link loss, power loss, endpoint changes, exhausted
retries, or a queue with no evictable entry can still lose a release.

**A host must close a contact after roughly 150 ms of report silence if
its last state was touched.** This watchdog is required on both
transports. Device removal and a trailing release without the lease bit
should also close the current gesture. Absolute coordinates allow motion to
continue after a missing sample, but do not make dropped input or
transport latency irrelevant.
