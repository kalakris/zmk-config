# Copyright (c) 2026 The ZMK Contributors
# SPDX-License-Identifier: MIT

# NOTE: every source here is compiled INTO ZMK's `app` target rather than
# into a zephyr_library() of our own. That is deliberate and not optional:
# ZMK declares its headers with `target_include_directories(app PRIVATE
# include)`, so a separate Zephyr library cannot see <zmk/endpoints.h>,
# <zmk/keymap.h> or <drivers/input_processor.h>. Compiling into `app` is
# the same approach cirque-input-module uses for its ZMK-aware source.

if(CONFIG_ZMK_RAW_TOUCH OR CONFIG_ZMK_INPUT_PROCESSOR_RAW_TOUCH_SCROLL OR CONFIG_ZMK_INPUT_PROCESSOR_RAW_TOUCH_IDLE_FILTER)
  target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

  # Carries the LOG_MODULE_REGISTER, so it must be built whenever any part
  # of the module is - the input processors are enabled independently of
  # CONFIG_ZMK_RAW_TOUCH, by their own devicetree nodes.
  target_sources(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/log.c)

  target_sources_ifdef(CONFIG_ZMK_RAW_TOUCH app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/hid.c)
  target_sources_ifdef(CONFIG_ZMK_RAW_TOUCH app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/raw_touch.c)
  target_sources_ifdef(CONFIG_ZMK_RAW_TOUCH app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/endpoints.c)
  target_sources_ifdef(CONFIG_ZMK_RAW_TOUCH_USB app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/usb_hid.c)
  target_sources_ifdef(CONFIG_ZMK_RAW_TOUCH_BLE app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/hog.c)
  target_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_RAW_TOUCH_SCROLL app PRIVATE
                       ${CMAKE_CURRENT_SOURCE_DIR}/src/input_processor_raw_touch_scroll.c)
  target_sources_ifdef(CONFIG_ZMK_INPUT_PROCESSOR_RAW_TOUCH_IDLE_FILTER app PRIVATE
                       ${CMAKE_CURRENT_SOURCE_DIR}/src/input_processor_raw_touch_idle_filter.c)
endif()
