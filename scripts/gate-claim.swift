// Ad-hoc mode-gate claim writer for the zmk-raw-touch vendor HID device.
// Bench tool for the host claim: sends the 4-byte gate command as a SET
// feature report, no Accessibility/TCC needed. Companion to
// raw-touch-monitor.swift, which shows the resulting flags bit 2.
//
// Usage:
//     gate-claim claim [timeout_s]    one claim write (default 30 s)
//     gate-claim release              one release write
//     gate-claim hold [timeout_s]     claim + refresh at timeout/2 until Ctrl-C,
//                                     release on exit (dead-host sim: kill -9)
//     gate-claim raw B0 B1 B2 B3      arbitrary body, for malformed-write tests
//
// Writes the bare 4-byte body first; on failure retries once with the 0x04
// report-ID prefix (host stacks differ; firmware accepts both).
//
// Build & run:
//     swiftc -O scripts/gate-claim.swift -o /tmp/gate-claim
//     /tmp/gate-claim claim 30

import Foundation
import IOKit.hid

let usagePage = 0xFF00
let usage = 0x01
let reportID: CFIndex = 0x04

// Feature-report wire format (protocol 4; the module README's appendix is
// normative): a 16-byte header — protocol version, pads-present,
// capabilities, module version, the four "RAWT" magic bytes, then the
// 8-byte hwinfo device id — plus one 8-byte geometry slot per pad, so a
// valid body is 16 + 8N bytes (32 on the Go60's two pads).
let featureProtocolVersion: UInt8 = 4
let featureHeaderLength = 16
let featurePadSlotLength = 8
let featureMagicOffset = 4
let featureMagic: [UInt8] = Array("RAWT".utf8)

/// The feature body out of whatever macOS handed back, or nil if the buffer
/// is not a protocol-4 raw-touch body at all.
///
/// macOS quirk: over USB, GetReport returns the buffer WITH the report-ID
/// byte prefixed; over BLE it comes back bare. The old "strip it if the
/// first byte is the report ID" rule cannot be used any more — a bare body
/// starts with the protocol version, which is 4, the same value as the
/// report ID, so that rule would eat a byte off every Bluetooth read.
/// Validate instead: try the buffer as it came, and only then the
/// ID-stripped reading. Magic, version and the exact 16 + 8N length all have
/// to agree, which is also the squatter check.
func featureBody(_ buffer: [UInt8]) -> [UInt8]? {
    func valid(_ body: [UInt8]) -> Bool {
        body.count >= featureHeaderLength + featurePadSlotLength
            && (body.count - featureHeaderLength) % featurePadSlotLength == 0
            && body[0] == featureProtocolVersion
            && Array(body[featureMagicOffset ..< featureMagicOffset + featureMagic.count])
                == featureMagic
    }
    if valid(buffer) { return buffer }
    if buffer.first == UInt8(reportID) {
        let stripped = Array(buffer.dropFirst())
        if valid(stripped) { return stripped }
    }
    return nil
}

func note(_ message: String) {
    FileHandle.standardError.write((message + "\n").data(using: .utf8)!)
}

func fail(_ message: String) -> Never {
    note("error: " + message)
    exit(1)
}

// --- argument parsing -------------------------------------------------------

enum Mode {
    case claim(timeout: UInt8)
    case release
    case hold(timeout: UInt8)
    case raw([UInt8])
}

func parseTimeout(_ args: [String], at index: Int, default def: UInt8) -> UInt8 {
    guard args.count > index else { return def }
    guard let value = UInt8(args[index]) else { fail("bad timeout '\(args[index])'") }
    return value
}

var args = Array(CommandLine.arguments.dropFirst())
// Optional leading transport pin: "usb" or "ble" (default: prefer USB,
// since ZMK selects USB when both transports are connected).
var requestedTransport: String? = nil
if let first = args.first, ["usb", "ble"].contains(first) {
    requestedTransport = first == "usb" ? "USB" : "Bluetooth Low Energy"
    args.removeFirst()
}
guard let verb = args.first else {
    fail("usage: gate-claim [usb|ble] claim [timeout_s] | release | hold [timeout_s] | raw B0 B1 B2 B3")
}

let mode: Mode
switch verb {
case "claim": mode = .claim(timeout: parseTimeout(args, at: 1, default: 30))
case "release": mode = .release
case "hold": mode = .hold(timeout: parseTimeout(args, at: 1, default: 30))
case "raw":
    let bytes = args.dropFirst().map { UInt8($0, radix: 16) ?? UInt8($0) ?? 0xFF }
    guard !bytes.isEmpty else { fail("raw needs at least 1 byte") }
    mode = .raw(Array(bytes))
default: fail("unknown verb '\(verb)'")
}

// --- device discovery -------------------------------------------------------

let manager = IOHIDManagerCreate(kCFAllocatorDefault, IOOptionBits(kIOHIDOptionsTypeNone))
IOHIDManagerSetDeviceMatching(manager, [
    kIOHIDDeviceUsagePageKey: usagePage,
    kIOHIDDeviceUsageKey: usage,
] as CFDictionary)
guard IOHIDManagerOpen(manager, IOOptionBits(kIOHIDOptionsTypeNone)) == kIOReturnSuccess else {
    fail("IOHIDManagerOpen failed")
}
let devices = (IOHIDManagerCopyDevices(manager) as? Set<IOHIDDevice>).map(Array.init) ?? []

// Validate via the feature report: magic + protocol 4 + gate capability
// (bit 0). See featureBody() for the USB/BLE framing rule.
var candidates: [(device: IOHIDDevice, transport: String)] = []
for device in devices {
    var buffer = [UInt8](repeating: 0, count: 128)
    var length: CFIndex = buffer.count
    guard IOHIDDeviceGetReport(device, kIOHIDReportTypeFeature, reportID, &buffer, &length)
            == kIOReturnSuccess, length > 0, length <= buffer.count else { continue }
    guard let body = featureBody(Array(buffer[0..<length])) else { continue }
    let transport = (IOHIDDeviceGetProperty(device, kIOHIDTransportKey as CFString) as? String) ?? "?"
    note(String(format: "# device: transport=%@ capabilities=0x%02X", transport, body[2]))
    guard body[2] & 0x01 != 0 else {
        note("# skipping: no mode-gate capability (bit 0 = 0)")
        continue
    }
    candidates.append((device, transport))
}
let picked: (device: IOHIDDevice, transport: String)?
if let wanted = requestedTransport {
    picked = candidates.first { $0.transport == wanted }
} else {
    picked = candidates.first { $0.transport == "USB" } ?? candidates.first
}
guard let (device, transport) = picked else {
    fail("no gate-capable protocol-4 raw-touch device found matching request (\(devices.count) candidate(s) on 0xFF00/0x01)")
}
note("# targeting: \(transport)")

// --- write path -------------------------------------------------------------

@discardableResult
func write(_ body: [UInt8], expectSuccess: Bool = true) -> Bool {
    // Bare body first, then one retry with the report-ID prefix.
    for payload in [body, [UInt8(reportID)] + body] {
        let result = payload.withUnsafeBufferPointer {
            IOHIDDeviceSetReport(device, kIOHIDReportTypeFeature, reportID,
                                 $0.baseAddress!, payload.count)
        }
        if result == kIOReturnSuccess {
            note(String(format: "# SetReport OK (%d bytes: %@)", payload.count,
                        payload.map { String(format: "%02X", $0) }.joined(separator: " ")))
            return true
        }
        note(String(format: "# SetReport 0x%X (%d bytes)", result, payload.count))
    }
    if expectSuccess { note("# WRITE FAILED on both framings") }
    return false
}

func claimBody(op: UInt8, timeout: UInt8) -> [UInt8] { [0x01, op, timeout, 0x00] }

switch mode {
case .claim(let timeout):
    exit(write(claimBody(op: 0x01, timeout: timeout)) ? 0 : 1)

case .release:
    exit(write(claimBody(op: 0x00, timeout: 30)) ? 0 : 1)

case .raw(let body):
    // Malformed-write probe: report firmware's verdict, never retry framing
    // confusion into a false negative — try bare only.
    let result = body.withUnsafeBufferPointer {
        IOHIDDeviceSetReport(device, kIOHIDReportTypeFeature, reportID,
                             $0.baseAddress!, body.count)
    }
    note(String(format: "# raw write result: 0x%X (%@)", result,
                result == kIOReturnSuccess ? "ACCEPTED" : "rejected"))
    exit(0)

case .hold(let timeout):
    guard write(claimBody(op: 0x01, timeout: timeout)) else { exit(1) }
    let interval = max(1.0, Double(timeout) / 2.0)
    note("# holding claim: refresh every \(interval)s, Ctrl-C to release+exit, kill -9 to simulate dead host")
    signal(SIGINT, SIG_IGN)
    let sigSource = DispatchSource.makeSignalSource(signal: SIGINT, queue: .main)
    sigSource.setEventHandler {
        write(claimBody(op: 0x00, timeout: 30))
        note("# released")
        exit(0)
    }
    sigSource.resume()
    let timer = DispatchSource.makeTimerSource(queue: .main)
    timer.schedule(deadline: .now() + interval, repeating: interval)
    timer.setEventHandler {
        write(claimBody(op: 0x01, timeout: timeout))
    }
    timer.resume()
    dispatchMain()
}
