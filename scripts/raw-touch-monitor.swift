// Standalone read-only monitor for the zmk-raw-touch vendor HID stream.
// Matches the vendor collection (usage page 0xFF00, usage 0x01), filters
// input report 0x04, and prints one CSV line per protocol-v3 frame:
//
//     host_ns,dev,pad,contact,x,y,z,flags,seq,ts_ticks
//
// host_ns is CLOCK_UPTIME_RAW at callback time (arrival time); ts_ticks is
// the device-side 100 µs sample clock (wraps at 6.5536 s). Device add /
// removal / transport notes go to stderr so stdout stays clean CSV.
//
// Passive observer: opens the device non-seizing, so LinearMouse keeps
// working while this runs. Feed the output to analyze-touch-timing.py.
//
// Build & run:
//     swiftc -O scripts/raw-touch-monitor.swift -o /tmp/raw-touch-monitor
//     /tmp/raw-touch-monitor > capture.csv    (Ctrl-C to stop)

import Foundation
import IOKit.hid

let usagePage = 0xFF00
let usage = 0x01
let frameReportID: UInt32 = 0x04
let v3PayloadLength = 11

var deviceIndex: [IOHIDDevice: Int] = [:]
var nextDeviceIndex = 0

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
    var buffer = [UInt8](repeating: 0, count: 64)
    var length: CFIndex = buffer.count
    let result = IOHIDDeviceGetReport(device, kIOHIDReportTypeFeature,
                                      CFIndex(frameReportID), &buffer, &length)
    if result == kIOReturnSuccess, length >= 3 {
        note(String(format: "# dev %d: feature report: protocol_version=%d pads_present=0x%02X capabilities=0x%02X",
                    index, buffer[0], buffer[1], buffer[2]))
    } else {
        note(String(format: "# dev %d: feature report read failed (0x%X)", index, result))
    }
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
}, nil)

IOHIDManagerRegisterDeviceRemovalCallback(manager, { _, _, _, device in
    if let index = deviceIndex.removeValue(forKey: device) {
        note("# dev \(index): removed")
    }
}, nil)

IOHIDManagerRegisterInputReportCallback(manager, { _, _, sender, _, reportID, report, reportLength in
    let hostNs = clock_gettime_nsec_np(CLOCK_UPTIME_RAW)
    guard reportID == frameReportID, reportLength >= v3PayloadLength else { return }
    // USB delivers the buffer with the report ID prepended; BLE does not.
    // Same defensive strip as LinearMouse: one byte longer than the
    // contract means a report-ID prefix.
    let offset = reportLength == v3PayloadLength + 1 ? 1 : 0
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
note("# monitoring (Ctrl-C to stop)...")

signal(SIGINT) { _ in
    note("# stopped")
    exit(0)
}

CFRunLoopRun()
