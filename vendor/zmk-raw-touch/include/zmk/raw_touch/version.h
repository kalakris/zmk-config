/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * The module's own version - the single source of truth for it. Git tags
 * (v0.1.0 and on) name a release; this header is what the firmware, and
 * therefore the wire, reports. CI checks that a v* tag agrees with it.
 *
 * Distinct from ZMK_RAW_TOUCH_PROTOCOL_VERSION in zmk/raw_touch/hid.h,
 * which is the only compatibility contract with a host: this number is
 * diagnostic, so a host can tell which build it is talking to (and, on a
 * split, which build each half is running) without it deciding how to
 * parse anything.
 */

#pragma once

#include <zephyr/toolchain.h>

#define ZMK_RAW_TOUCH_MODULE_VERSION_MAJOR 0
#define ZMK_RAW_TOUCH_MODULE_VERSION_MINOR 1

/* One byte for the feature report: high nibble major, low nibble minor -
 * 0x01 is 0.1, 0x10 is 1.0. Zero is reserved for "unknown", which is what
 * a central reports for a relayed pad whose half has not announced itself
 * yet (see zmk/raw_touch/split_stamp.h) and what a host reads from any
 * build that predates this field. The patch level is deliberately absent:
 * it never changes anything a host can observe, and a byte only has two
 * nibbles. */
#define ZMK_RAW_TOUCH_MODULE_VERSION_PACKED                                                        \
    ((ZMK_RAW_TOUCH_MODULE_VERSION_MAJOR << 4) | ZMK_RAW_TOUCH_MODULE_VERSION_MINOR)

BUILD_ASSERT(ZMK_RAW_TOUCH_MODULE_VERSION_MAJOR <= 15 && ZMK_RAW_TOUCH_MODULE_VERSION_MINOR <= 15,
             "Module major and minor versions must each fit one nibble of the packed byte");
BUILD_ASSERT(ZMK_RAW_TOUCH_MODULE_VERSION_PACKED != 0,
             "Packed module version 0 is reserved for \"unknown\"");
