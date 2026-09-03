/*
 * Copyright (c) 2026 Mrinal Kalakrishnan
 *
 * SPDX-License-Identifier: MIT
 *
 * The module's log module, alone in its own translation unit.
 *
 * It cannot live in src/raw_touch_hid.c: the two input processors are enabled by
 * their own devicetree nodes, independently of CONFIG_ZMK_RAW_TOUCH. A
 * build that references the scroll marker but has no zmk,raw-touch-pad
 * node would then compile a LOG_MODULE_DECLARE with no matching
 * LOG_MODULE_REGISTER anywhere, and fail to link. This file is compiled
 * whenever any part of the module is.
 */

#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(zmk_raw_touch, CONFIG_ZMK_RAW_TOUCH_LOG_LEVEL);
