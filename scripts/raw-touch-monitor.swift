// Standalone read-only monitor for the zmk-raw-touch vendor HID stream.
// Matches the vendor collection (usage page 0xFF00, usage 0x01), filters
// input report 0x04, and prints one CSV line per protocol-v4 frame:
//
//     host_ns,dev,pad,contact,x,y,z,flags,seq,ts_ticks
//
// host_ns is CLOCK_UPTIME_RAW at callback time (arrival time); ts_ticks is
// the device-side 100 µs sample clock (wraps at 6.5536 s). Device add /
// removal / transport notes go to stderr so stdout stays clean CSV.
//
// Passive observer by default: opens the device non-seizing, so a running
// stream consumer keeps working while this runs. Feed the output to
// analyze-touch-timing.py.
//
// Since the firmware gates frame emission on the host claim (2026-08-31),
// a passive monitor sees frames only while some claim is live. Two modes:
//
// - RawTouch (or another host) running: just run passively — its claim
//   keeps frames flowing, HID input reports fan out to every client, and
//   you observe exactly what the host sees without perturbing anything.
//   This is the preferred debug mode. Do NOT also pass --claim: it adds
//   nothing (frames already flow) and muddies claim-refresh/expiry timing.
// - No host running: the stream is silent by design (the frames are never
//   transmitted), so pass --claim: the monitor writes the claim feature
//   report itself (4-byte gate-claim body, refreshed periodically,
//   released on Ctrl-C — same SET-report path as scripts/gate-claim.swift,
//   including the USB report-ID-prefix quirk).
//
// Observer effect of --claim: holding the claim puts the keyboard in
// RawTouch mode, so the wheel-scroll fallback you may be trying to debug
// stops firing and scroll gestures move nothing on screen until release.
// (A claiming monitor does NOT double-scroll — the two-consumers rule is
// about two scroll synthesizers, and this tool synthesizes nothing.)
//
// Build & run:
//     swiftc -O scripts/raw-touch-monitor.swift -o /tmp/raw-touch-monitor
//     /tmp/raw-touch-monitor > capture.csv            (Ctrl-C to stop)
//     /tmp/raw-touch-monitor --claim > capture.csv    (standalone capture)

import Foundation
import IOKit.hid

let usagePage = 0xFF00
let usage = 0x01
let frameReportID: UInt32 = 0x04
let framePayloadLength = 11

// Feature-report wire format (protocol 4; the module README's appendix is
// normative): a 16-byte header — protocol version, pads-present,
// capabilities, module version, the four "RAWT" magic bytes, then the
// 8-byte hwinfo device id — plus one 8-byte geometry slot per pad, so a
// valid body is 16 + 8N bytes (32 on the Go60's two pads; 33 over USB,
// where macOS prefixes the report ID).
let featureProtocolVersion: UInt8 = 4
let featureHeaderLength = 16
let featurePadSlotLength = 8
let featureMagicOffset = 4
let featureMagic: [UInt8] = Array("RAWT".utf8)
let featureDeviceIDOffset = 8
let featureDeviceIDLength = 8

/// The feature body out of whatever macOS handed back, or nil if the buffer
/// is not a protocol-4 raw-touch body at all.
///
/// Over USB the GET comes back report-ID-prefixed, over BLE bare. The old
/// "strip it if the first byte is the report ID" rule cannot be used any
/// more — a bare body starts with the protocol version, which is 4, the
/// same value as the report ID, so that rule would eat a byte off every
/// Bluetooth read. Validate instead: try the buffer as it came, then the
/// ID-stripped reading. Magic, version and the exact 16 + 8N length all have
/// to agree.
func featureBody(_ buffer: [UInt8]) -> [UInt8]? {
    func valid(_ body: [UInt8]) -> Bool {
        body.count >= featureHeaderLength + featurePadSlotLength
            && (body.count - featureHeaderLength) % featurePadSlotLength == 0
            && body[0] == featureProtocolVersion
            && Array(body[featureMagicOffset ..< featureMagicOffset + featureMagic.count])
                == featureMagic
    }
    if valid(buffer) { return buffer }
    if buffer.first == UInt8(frameReportID) {
        let stripped = Array(buffer.dropFirst())
        if valid(stripped) { return stripped }
    }
    return nil
}

let claimMode = CommandLine.arguments.contains("--claim")
let claimTimeoutS: UInt8 = 30
let claimRefreshInterval: TimeInterval = 10 // < timeoutS / 2

var deviceIndex: [IOHIDDevice: Int] = [:]
var nextDeviceIndex = 0
var claimedDevices: Set<IOHIDDevice> = []

func stringProperty(_ device: IOHIDDevice, _ key: String) -> String {
    (IOHIDDeviceGetProperty(device, key as CFString) as? String) ?? "?"
}

func intProperty(_ device: IOHIDDevice, _ key: String) -> Int {
    (IOHIDDeviceGetProperty(device, key as CFString) as? Int) ?? -1
}

func note(_ message: String) {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
}

func describe(_ device: IOHIDDevice, index: Int) {
    let product = stringProperty(device, kIOHIDProductKey)
    let transport = stringProperty(device, kIOHIDTransportKey)
    let vid = intProperty(device, kIOHIDVendorIDKey)
    let pid = intProperty(device, kIOHIDProductIDKey)
    note(String(format: "# dev %d: %@ transport=%@ vid=0x%04X pid=0x%04X",
                index, product, transport, vid, pid))

    // Read the capability feature report (same report ID) for context.
    var buffer = [UInt8](repeating: 0, count: 128)
    var length: CFIndex = buffer.count
    let result = IOHIDDeviceGetReport(device, kIOHIDReportTypeFeature,
                                      CFIndex(frameReportID), &buffer, &length)
    if result == kIOReturnSuccess, length > 0, length <= buffer.count,
       let body = featureBody(Array(buffer[0 ..< length])) {
        // The device id is the same eight bytes over USB and BLE, so it
        // is how a capture from one transport is matched to the other.
        let deviceID = body[featureDeviceIDOffset ..< featureDeviceIDOffset + featureDeviceIDLength]
            .map { String(format: "%02X", $0) }.joined()
        note(String(format: "# dev %d: feature report: %d bytes, protocol_version=%d pads_present=0x%02X capabilities=0x%02X module_version=0x%02X magic=%@ device_id=%@",
                    index, body.count, body[0], body[1], body[2], body[3],
                    String(decoding: body[featureMagicOffset ..< featureMagicOffset + featureMagic.count], as: UTF8.self),
                    deviceID))
    } else if result == kIOReturnSuccess {
        note(String(format: "# dev %d: feature report is not protocol %d (%d bytes)",
                    index, Int(featureProtocolVersion), Int(length)))
    } else {
        note(String(format: "# dev %d: feature report read failed (0x%X)", index, result))
    }
}

// --- claim mode (--claim) ---------------------------------------------------
// Lifted from scripts/gate-claim.swift. Validation and framing both handle
// the macOS quirk: over USB, feature-report GETs arrive with the report-ID
// byte prefixed (and some stacks want SETs prefixed too); over BLE both are
// bare. Claim writes go bare-body first, then retry once with the prefix.

func gateCapable(_ device: IOHIDDevice) -> Bool {
    var buffer = [UInt8](repeating: 0, count: 128)
    var length: CFIndex = buffer.count
    guard IOHIDDeviceGetReport(device, kIOHIDReportTypeFeature, CFIndex(frameReportID),
                               &buffer, &length) == kIOReturnSuccess,
          length > 0, length <= buffer.count,
          let body = featureBody(Array(buffer[0..<length])) else { return false }
    return body[2] & 0x01 != 0 // protocol 4 (checked above) + claim capability
}

@discardableResult
func writeClaim(_ device: IOHIDDevice, claim: Bool) -> Bool {
    let body: [UInt8] = [0x01, claim ? 0x01 : 0x00, claimTimeoutS, 0x00]
    for payload in [body, [UInt8(frameReportID)] + body] {
        let result = payload.withUnsafeBufferPointer {
            IOHIDDeviceSetReport(device, kIOHIDReportTypeFeature, CFIndex(frameReportID),
                                 $0.baseAddress!, payload.count)
        }
        if result == kIOReturnSuccess { return true }
    }
    return false
}

setvbuf(stdout, nil, _IOLBF, 0) // line-buffer CSV so captures survive Ctrl-C

let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
let matching: [String: Any] = [
    kIOHIDDeviceUsagePageKey: usagePage,
    kIOHIDDeviceUsageKey: usage
]
IOHIDManagerSetDeviceMatching(manager, matching as CFDictionary)

IOHIDManagerRegisterDeviceMatchingCallback(manager, { _, _, _, device in
    let index = nextDeviceIndex
    nextDeviceIndex += 1
    deviceIndex[device] = index
    describe(device, index: index)
    if claimMode {
        if gateCapable(device) {
            if writeClaim(device, claim: true) {
                claimedDevices.insert(device)
                note("# dev \(index): claim written (timeout \(claimTimeoutS)s); wheel fallback suppressed")
            } else {
                note("# dev \(index): CLAIM WRITE FAILED on both framings")
            }
        } else {
            note("# dev \(index): not claim-capable (no v3 feature report or capability bit 0 clear)")
        }
    }
}, nil)

IOHIDManagerRegisterDeviceRemovalCallback(manager, { _, _, _, device in
    claimedDevices.remove(device)
    if let index = deviceIndex.removeValue(forKey: device) {
        note("# dev \(index): removed")
    }
}, nil)

IOHIDManagerRegisterInputReportCallback(manager, { _, _, sender, _, reportID, report, reportLength in
    let hostNs = clock_gettime_nsec_np(CLOCK_UPTIME_RAW)
    guard reportID == frameReportID, reportLength >= framePayloadLength else { return }
    // USB delivers the buffer with the report ID prepended; BLE does not.
    // Same defensive strip as LinearMouse: one byte longer than the
    // contract means a report-ID prefix.
    let offset = reportLength == framePayloadLength + 1 ? 1 : 0
    let b = UnsafeBufferPointer(start: report + offset, count: reportLength - offset)
    var dev = -1
    if let sender = sender {
        let device = Unmanaged<IOHIDDevice>.fromOpaque(sender).takeUnretainedValue()
        dev = deviceIndex[device] ?? -1
    }
    let x = Int(b[2]) | (Int(b[3]) << 8)
    let y = Int(b[4]) | (Int(b[5]) << 8)
    let ts = Int(b[9]) | (Int(b[10]) << 8)
    print("\(hostNs),\(dev),\(b[0]),\(b[1]),\(x),\(y),\(b[6]),\(b[7]),\(b[8]),\(ts)")
}, nil)

IOHIDManagerScheduleWithRunLoop(manager, CFRunLoopGetCurrent(), CFRunLoopMode.commonModes.rawValue)
let openResult = IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone))
if openResult != kIOReturnSuccess {
    note(String(format: "# IOHIDManagerOpen returned 0x%X; waiting for a matching device anyway", openResult))
}

print("host_ns,dev,pad,contact,x,y,z,flags,seq,ts_ticks")
if claimMode {
    note("# claim mode: this monitor is a stream consumer - do NOT run RawTouch at the same time")
    Timer.scheduledTimer(withTimeInterval: claimRefreshInterval, repeats: true) { _ in
        for device in claimedDevices {
            writeClaim(device, claim: true)
        }
    }
}
note("# monitoring (Ctrl-C to stop)...")

// DispatchSource rather than signal(): releasing the claim does I/O, which
// is not async-signal-safe. The main run loop services the main queue.
signal(SIGINT, SIG_IGN)
let sigintSource = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
sigintSource.setEventHandler {
    for device in claimedDevices {
        writeClaim(device, claim: false)
    }
    if !claimedDevices.isEmpty {
        note("# claim(s) released")
    }
    note("# stopped")
    exit(0)
}
sigintSource.resume()

CFRunLoopRun()
