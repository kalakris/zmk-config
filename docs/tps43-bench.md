# TPS43 bench evaluation — multi-finger successor to the Pinnacle

**Status: not started. Electrical design closed, hardware not yet ordered.**
Session of 2026-08-26 did the research and de-risking; nothing has been
bought, wired, or flashed.

## Why

`raw-touch` synthesises scroll from the Pinnacle's **single** contact. The
Azoteq TPS43 (IQS5xx-B000 / IQS550) reports **five** contacts with absolute
XY, touch strength and area. That would remove the biggest structural
compromise in the stack — but only if the gesture quality actually improves
enough to justify a protocol v3.

**This doc covers a bench rig to answer that question without touching the
Go60.** See [raw-touch.md](raw-touch.md) for the shipping v2 system.

## Decisions already made (don't relitigate)

**Do not mod the Go60 — yet.** MoErgo documents the pads as *integrated*
2x40mm trackpads; user-serviceable parts are batteries and hotswap switches
only, and there is no teardown or expansion-port documentation. The Cirque
sits behind a **round 40mm aperture**; a TPS43 is a **43 x 40 rectangle**.
Fitting it means cutting the visible top surface of the case — the one
irreversible step in the whole project, on the daily driver. Everything else
(firmware, wiring) is reversible.

**Do not buy a Toucan2 instead.** beekeeb's Toucan2 is a real shipping
TPS43 keyboard and validates the part, but:
- Cantor/Piantor layout: 3 rows + 3 thumbs per side. The Go60 keymap uses
  4 rows (`ROW`, rows 0-3) plus 6 bottom/thumb keys per side. Loses the num
  row and half the thumb cluster — including position 57 `RH T1`, the
  Go60-only inner thumb key used as left click and listed in
  `MOUSE_KEEP_KEYS` (`positions_go60.dtsi:33`, `:87`).
- Its driver emits `INPUT_REL_WHEEL` / `INPUT_REL_HWHEEL` — on-chip gestures
  as **wheel ticks**. That is the driverless fallback `main` already has, and
  the exact thing raw-touch exists to escape.

**Do not switch to maXTouch (Procyon/Peacock/Ploopy) for this.** See the
alternatives table below — it's the better sensor but the wrong trade at
43mm.

## Sourcing (time-sensitive)

**The "TPS43 is EOL" claim in both ZMK driver READMEs is over-broad.**
Azoteq's [EOL notice rev 1.7, May 2024][eol] (final production 2024-02-16,
notice date 2024-04-01, final order from stock CLOSED) kills
`TPS43-101X`, `TPS42-201X`, `TPS48-P201`, `TPS65-101X/201X`,
`TPE48-P201/P203` and several TPR variants. Replacement column says
"Contact Sales" for every TPS part — no drop-in successor for the
rectangular modules. Surviving TPR variants named in the notice itself:
`TPR40-V101`, `TPR48-P201`, `TPR54-P202`.

**`TPS43-201A` is not on that list** and is what the keyboard world buys:

| Source | Part | Status (checked 2026-08-26) |
|---|---|---|
| DigiKey | `TPS43-201A-S` | **Active**, 507 in stock, $5.29/1, $3.57/1k |
| holykeebs | `TPS43-201A-B` | shipping in their touchpad module |
| Keycapsss, Svalboard | TPS43 | listed |

**Preferred buy: [beekeeb Toucan Upgrade Kit][kit], $48.** Includes the
TPS43, **custom-made glass overlay** (thickness matched to the sensor
tuning), trackpad tape, trackpad mount, convertor, FPC, 7P cable — plus
Toucan case parts to discard. The glass and the FPC/ZIF breakout are the two
genuinely annoying things to source: the module is 43 x 40mm with a **6-pin
ZIF connector**, and tempered glass cannot be cut after tempering.
Listed low-stock with ship dates into ~Sep 2026.

Risk note: single active variant of a line whose vendor publicly could not
find a fab for its siblings. Buy two.

## Bench rig — Eyelash Sofle (unused daily-driver spare)

No cutting, no soldering, fully reversible. Both keyboards stay intact.

### Why the Sofle works

- Same nRF52840 as the Go60 — driver and stream code port straight across.
- **TWIM1 is free.** `eyelash_sofle.dtsi` uses `spi0` (nice!view: SCK p0.20,
  MOSI p0.17, MISO p0.25, CS p0.6) and `spi3` (WS2812, p1.12) only. No SPI1,
  so no peripheral-instance conflict on nRF52840.
- The MCU is a **soldered-down module** (marked `N52840 QIAAD0`), not a
  socketed nice!nano — but we don't need it.
- **The nice!view sits in a socketed 5-pin header that is originally an I2C
  OLED header.** Per `nice_view_adapter/README.md`: "an adapter between the
  nice!view and existing shields/boards that expose an I2C OLED header. The
  nice!view will use the SDA/SCL pins of the OLED, and then the adapter
  expects a final pin to be 'bodged' ... to the nice!view CS pin."
  So we are plugging an I2C device into an I2C header, not repurposing SPI.

### Pinout (verified: display silkscreen + `eyelash_sofle.dtsi`)

Display underside labels, top to bottom: `SCS`, `GND`, `3V-5V`, `SCLK`, `SI`.

| Socket pin | Label | nRF pin | Original OLED fn | -> TPS43 |
|---|---|---|---|---|
| 1 | SCS | p0.6 | CS (bodge pin) | RDY (optional) |
| 2 | GND | — | GND | GND |
| 3 | 3V-5V | — | VCC | VDD |
| 4 | SCLK | p0.20 | **SCL** | SCL |
| 5 | SI | p0.17 | **SDA** | SDA |

Mark which end is SCS before pulling the display — the row is symmetric and
reversing it puts VCC where RDY should be.

### Measurements taken (2026-08-26)

- **Pin 3 = 3.333 V.** Regulated 3.3V rail, not raw LiPo. Inside the
  IQS550's 1.65-3.6 V window (absolute max 3.6 V — a 4.2 V LiPo rail would
  have killed the part). **Wire VDD straight through, no LDO needed.**
- **Pull-ups: none.** 3->4 and 3->5 read `OL` on the MOhm range *with the
  display connected*, i.e. the display-in-parallel-with-board combination is
  open, so both paths are open.

### Consequence: I2C rise time

No external pull-ups means the nRF internal ~13 kOhm pull-ups carry the bus.
Rise time is R x C, and flying leads carry far more capacitance than the PCB
traces beekeeb's design has. 13 kOhm against ~50-100 pF is roughly
0.7-1.3 us; **I2C fast mode allows 300 ns.** Not a comfortable margin.

- **Start at `I2C_BITRATE_STANDARD` (100 kHz)** — allows 1 us rise time.
  Costs nothing: TPS43 reports ~100 Hz with small payloads, ~20 kbit/s
  against a 100 kbit/s budget.
- Keep **2.2-4.7 kOhm** resistors on hand (SDA/SCL to pin 3) if you want
  400 kHz or the bus misbehaves.
- **If the pad enumerates at 0x74 then drops out intermittently or throws
  I2C errors under fast finger motion, suspect this first, not the driver.**

### Firmware sketch

```dts
&pinctrl {
    i2c1_tps43_default: i2c1_tps43_default {
        group1 {
            psels = <NRF_PSEL(TWIM_SDA, 0, 17)>, <NRF_PSEL(TWIM_SCL, 0, 20)>;
            bias-pull-up;   /* load-bearing — no externals on this board */
        };
    };
    /* + matching _sleep node with low-power-enable */
};

&i2c1 {
    status = "okay";
    clock-frequency = <I2C_BITRATE_STANDARD>;   /* 100 kHz to start */
    pinctrl-0 = <&i2c1_tps43_default>;
    pinctrl-1 = <&i2c1_tps43_sleep>;
    pinctrl-names = "default", "sleep";

    tps43: trackpad@74 {
        compatible = "azoteq,tps43";
        reg = <0x74>;
        rdy-gpios = <&gpio0 6 GPIO_ACTIVE_HIGH>;
        /* rst-gpios optional and unavailable — only 3 GPIOs on the header */
    };
};
```

Also: `CONFIG_INPUT_TPS43=y`, add `beekeeb/zmk_driver_azoteq` to
`config/west.yml`, and **drop the `nice_view` shield from that build
target** in `build.yaml`. Physically remove the display too — leaving a
Sharp memory LCD on those lines adds capacitance and a driver that can fight
the bus.

Mount the pad with the kit's trackpad tape. For a bench test it does not
need to be mounted prettily.

## Reference implementation

[beekeeb/zmk-keyboard-toucan2][t2] builds against **upstream ZMK v0.3** — no
forked ZMK — pulling `beekeeb/zmk_driver_azoteq` and `beekeeb/zmk-input-zoom`.
`boards/shields/toucan/toucan_right.overlay` is nearly a drop-in template,
including `&trackpad_split { device = <&tps43_trackpad>; }`, which is
structurally identical to the Go60's `cirque_split` pattern.

### Driver landscape

| Fork | compatible | Notes |
|---|---|---|
| `beekeeb/zmk_driver_azoteq` | `azoteq,tps43` | Richest: power management, LP timeouts, scroll-angle, 3-finger swipe, zoom, filter tuning. `rdy-gpios` and `rst-gpios` both `required: false` |
| `AYM1607/zmk-driver-azoteq-iqs5xx` | `azoteq,iqs5xx` | De-facto standard, 73*, reach |
| `finestedm/...` (fork of above) | `azoteq,iqs5xx` | Adds `report-rate-active-ms` (200 Hz floor), I2C-retry robustness |

**All of them emit relative/gesture events, not raw frames**
(`INPUT_REL_X/Y`, `INPUT_REL_WHEEL/HWHEEL`, `INPUT_REL_MISC` for zoom,
`BTN_0/1`, `BTN_TOUCH`). A raw-touch v3 needs a new stream source; the
drivers are a bring-up vehicle and a register reference, not the endpoint.

### IQS5xx-B000 registers worth knowing

| Reg | Meaning |
|---|---|
| 0x0011 | NUM_FINGERS |
| 0x0012 / 0x0014 | REL_X / REL_Y |
| **0x0016 / 0x0018** | **ABS_X / ABS_Y** (per finger) |
| 0x001A | TOUCH_STRENGTH |
| 0x001C | TOUCH_AREA |
| 0x057A | REPORT_RATE_ACTIVE (default 10ms; 5ms practical floor) |
| SystemInfo1 (0x0010) | bit1 PALM_DETECT, bit2 TOO_MANY_FINGERS |

Palm detection exists in silicon but no driver acts on it. Irrelevant at
43mm in a Go60-like position — nothing rests on the pad. (Corrected
`prior-art-survey.md:33`, which claimed absolute per-finger data never
leaves the IQS572. That is UHK's *firmware* choice, not a chip limit.)

### Power

From `toucan_right.conf` comments — beekeeb's own measurements:

- **1.7 mA idle vs 2.9 mA active**
- LP2 at 640ms report rate = **55 uA**, vs default 160ms = **174 uA**

beekeeb copes with `CONFIG_ZMK_IDLE_TIMEOUT=30000`, forcing the pad low-power
aggressively and accepting ~300 ms wake latency on first touch.

**This is also why MoErgo chose Cirque.** Go60 is 1000 mAh/side for a claimed
~336 h => roughly **3 mA average per side** for everything — BLE, scanning,
and *two* pads. A TPS43 at 1.7 mA idle eats over half that alone. If the
Go60 mod ever happens, convert **one** pad.

## Alternatives surveyed (don't re-research)

| Option | Contacts | Bus | Size | Driver |
|---|---|---|---|---|
| **Azoteq IQS5xx** (TPS43/65, TPR40/48/54) | 5, absolute | I2C + RDY + RST | 43x40, 65x49, round 40-54 | Out-of-tree, mature, 3 forks |
| **Microchip maXTouch** ([Procyon][pro] 42x50 / 57x80, Peacock, [Ploopy Trackpad][pl]) | 10 (mXT336UD) / 16 (ATMXT1066TD) | I2C + CHG | fab your own | [maxtouch-zephyr-module][mt] — **reports real `INPUT_ABS_MT_SLOT`**; builds against `petejohanson/zmk@feat/pointers-move-scroll-ptp`; PTP so **no macOS** |
| **Cirque Gen4** (TM5957/5960/105065) | 2 (gestures) | I2C or SPI | 65.8x49.8+ | None. **Raw per-finger access unconfirmed** — Cirque markets "no additional driver required", which smells like on-chip gesture->relative. Ask Cirque before buying |
| **Touchscreen controllers** (GT911, FT5336, FT6146, CST8xx, CHSC5x/6x, ILI2132A, TMA525B) | 5-10 | I2C + INT + RST | panel-dependent | **In-tree Zephyr.** `input_gt911.c` does slot-based MT with tracking IDs when `CONFIG_INPUT_GT911_MAX_TOUCH_POINTS > 1`. Glass panels built for displays — feel/mounting is the problem, not software |
| **Laptop precision touchpad** | 5+, palm rejection | I2C-HID | ~100x60 | None. QMK #19323 still a feature request; Zephyr has no I2C-HID *host* stack |

maXTouch is the better sensor and its "no macOS" gap is exactly what the
LinearMouse fork already closes — but at 43mm, two-finger scroll is
comfortable and three-finger gestures are already cramped, so contacts
beyond ~5 are theoretical. It only becomes interesting for a **large** pad
somewhere else on the board.

## If the Go60 mod ever happens

The `go60_ext` connector (`moergo,go60-ext`, `go60_rh.dts:39`) is a 1:1 fit:

| EXT | nRF pin | Cirque today | TPS43 |
|---|---|---|---|
| EXT1 | p0.19 | SPI SCK | SCL |
| EXT2 | p0.21 | SPI MISO | *(spare)* |
| EXT3 | p0.22 | SPI MOSI | SDA |
| EXT4 | p0.23 | DR | RDY |
| EXT5 | p0.25 | CS | NRST |

SPIM1 and TWIM1 share instance 1 on nRF52840, so dropping `&spi1` frees
`&i2c1` directly. `&spi3` (WS2812) is untouched.

**Unverified:** whether the EXT connector carries a 3.3V rail (it must —
the Cirque needs power) and whether that rail is switched by the
`EXT_POWER`/WS2812_CE node, which would affect reset sequencing.

## Open questions for a protocol v3

1. Does genuine 2-finger data actually feel better than single-contact
   synthesis? **This is the only question the bench rig exists to answer.**
2. Frame format with contact IDs — 5 contacts x (x, y, strength, area) is a
   much bigger frame than v2's 7 bytes. Rate vs size trade at 100-200 Hz.
3. Does the LinearMouse fork's ballistics/phase logic survive contact with
   real two-finger input, or does it need rework?
4. Report rate: `REPORT_RATE_ACTIVE` 0x057A, 5 ms floor. Pinnacle abs mode is
   fixed 100 Hz — 200 Hz may visibly improve scroll smoothness.

## Confidence

Verified from primary sources (source trees, silkscreen, devicetree, meter):
pinout, 3.333V rail, absence of pull-ups, free TWIM1, driver event types,
register map, EOL part list, DigiKey stock, kit contents.

Inferred, not measured: I2C rise-time margin (arithmetic, not scoped);
that the Go60's EXT rail is 3.3V; Cirque Gen4 raw-access limitation.

[eol]: https://mm.digikey.com/Volume0/opasdata/d220001/medias/docus/6284/EOL_TPR-TPE_TPS_May2024.pdf
[kit]: https://shop.beekeeb.com/products/toucan-upgrade-kit
[t2]: https://github.com/beekeeb/zmk-keyboard-toucan2
[pro]: https://github.com/george-norton/procyon
[pl]: https://ploopy.co/trackpad/
[mt]: https://github.com/george-norton/maxtouch-zephyr-module
