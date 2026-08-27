/*
 * Copyright (c) 2026 The ZMK Contributors
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <zmk/raw_touch/hid.h>

/**
 * @brief Send the current raw touch report over the active ZMK endpoint.
 *
 * Dispatches on zmk_endpoints_selected() exactly as ZMK's own reports do,
 * so the raw stream follows the user's USB/BLE output selection and their
 * active BLE profile. Defined in src/endpoints.c.
 *
 * @retval 0 on success, or a negative errno.
 */
int zmk_raw_touch_send_report(void);

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_USB)
/* src/usb_hid.c */
int zmk_raw_touch_usb_send_report(void);
#endif

#if IS_ENABLED(CONFIG_ZMK_RAW_TOUCH_BLE)
/* src/hog.c */
int zmk_raw_touch_hog_send_report(struct zmk_raw_touch_report_body *body);
#endif
