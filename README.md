# IRremoteESP8266 components for ESPHome

This is a collection for `climate` implementations using the awesome [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) library.

For now only some protocols are implemented, please open an issue or an PR to add more.

Most platforms are transmit-only, but every platform on this fork can also **receive**: it can decode frames sent by the physical remote (captured via ESPHome's `remote_receiver`) and sync the climate entity's (and any related `select`/`switch`/`number` entity's) state to match. Two platforms (`fujitsu-264`, `mitsubishi`) are verified against real hardware; the rest are compile-tested only. See the [verification status table](#receive-mode-syncing-from-a-physical-remote) below before relying on receive mode for a platform other than those two.

Every platform also exposes `apply_batch(...)`: apply any combination of climate fields (mode/temperature/fan/swing, plus platform-specific extras) from a single call and transmit **at most once**, regardless of how many fields changed — useful when driving the climate from something other than ESPHome's own API (e.g. an MQTT subscription), where the caller would otherwise have to make several separate calls that each transmit their own IR frame. See [Batch commands](#batch-commands-single-transmission-multi-field-updates) below.

**Supported platforms:**
- [fujitsu](#fujitsu)
- [fujitsu-264](#fujitsu-264)
- [panasonic](#panasonic)
- [electra](#electra)
- [sharp](#sharp)
- [mitsubishi](#mitsubishi)

## Usage

```yaml
esp32:
  framework:
    type: arduino

external_components:
  - source:
      type: git
      url: https://github.com/yoshikik1991/ESPHome-IRremoteESP8266
    components: [ ir_remote_base, <platform_name> ]

remote_transmitter:
  pin: GPIOXX
  carrier_duty_percent: 50%

climate:
  - platform: <platform_name>
    name: 'Living Room AC'
```

Replace `<platform_name>` by the name of one of the platforms available.

It supports other options of [climate_ir](https://esphome.io/components/climate/climate_ir.html) like `sensor` and `transmitter_id`.

> [!NOTE]
> For platforms with a `model` option, please refer to the [supported Protocols page](https://github.com/crankyoldgit/IRremoteESP8266/blob/master/SupportedProtocols.md) ("A/C Model" column).

> [!WARNING]
> Only Arduino Framework is supported

### Library version policy

Most platforms load `IRremoteESP8266` through the shared `ir_remote_base.load_ir_remote()` helper, which pins the **registry** release `2.9.0` (rather than "latest"). This is deliberate: an earlier unpinned resolve picked up a newer registry release whose `IRrecv.cpp` called Arduino-ESP32 timer APIs (`timerAlarmEnable()`, the 3-argument `timerBegin()`/`timerAttachInterrupt()` overloads) that a newer `arduino-esp32` core had removed, silently breaking the cold build. `2.9.0` is the last version confirmed to build cleanly.

`fujitsu-264` is the one exception: it pins a specific commit of a community fork (`hldh214/IRremoteESP8266@564c20fa...`) directly in its own `climate.py`, because AC264 support ([crankyoldgit/IRremoteESP8266#2030](https://github.com/crankyoldgit/IRremoteESP8266/pull/2030)) isn't in any official release yet. New platforms should default to the shared, registry-pinned loader and only reach for a fork/commit pin if the feature they need genuinely isn't released yet.

## Receive mode (syncing from a physical remote)

Some platforms can also decode frames sent by the *physical* remote and reflect them onto the climate entity (and any related `select`/`switch`/`number` entities), so Home Assistant stays in sync when someone uses the remote directly instead of the app.

### Verification status

| Platform | Receive sync | Verified on |
|---|---|---|
| `fujitsu-264` | yes | Real hardware — Fujitsu `AR-RLB1J` remote, bedroom unit |
| `mitsubishi` | yes (model `MITSUBISHI_AC` only) | Real hardware — living-room MSZ-series unit (2026-07) |
| `fujitsu` (legacy) | yes | Compile-tested only — **unverified** on real hardware |
| `panasonic` | yes | Compile-tested only — **unverified** on real hardware |
| `electra` | yes | Compile-tested only — **unverified** on real hardware |
| `sharp` | yes | Compile-tested only — **unverified** on real hardware |

The four "compile-tested only" platforms were implemented by mirroring the two verified receive paths against each protocol's own byte layout, without a physical unit to confirm the mapping against. If you own one of these units and try receive mode, feedback (working or not) is welcome via an issue.

### Enabling it

Add a `receiver_id:` pointing at your `remote_receiver` to the `climate:` block. Each platform's own `on_receive()` then decodes the frame and updates the climate entity (and any declared `select`/`switch`/`number` sub-entities) automatically — there is no YAML lambda to write:

```yaml
remote_receiver:
  id: my_receiver
  pin:
    number: GPIOXX
    # Most IR receiver modules (e.g. TSOP-series) output an active-low
    # signal, so `inverted: true` is the common case, not the exception.
    # `INPUT_PULLUP` isn't always necessary (depends on the specific
    # receiver module) but is a safe default.
    inverted: true
    mode: INPUT_PULLUP
  dump: []
  # ESP32-S3 only decodes short frames without this — see note below.
  use_dma: true
  rmt_symbols: 512
  receive_symbols: 512

climate:
  - platform: <platform_name>
    name: 'Living Room AC'
    # Required to enable receive mode: without this, the climate entity is
    # never registered as a listener on my_receiver at all. It is NOT
    # auto-resolved even if there's only one remote_receiver in the device —
    # climate_ir's own schema declares it as a plain cv.Optional, not a
    # cv.GenerateID, so it has to be spelled out explicitly every time.
    receiver_id: my_receiver
```

> [!NOTE]
> ESPHome's built-in `remote_receiver` on an ESP32-S3 only allocates 192 RMT symbols by default, enough for short frames but not for longer ones (e.g. fujitsu-264's 33-byte/264-bit frame is ~266 symbols). If frames longer than a few bytes are getting dropped or truncated, set `use_dma: true` together with `rmt_symbols: 512` and `receive_symbols: 512` (or higher) as shown above.

Internally, each platform's `on_receive()` runs ESPHome's own `AEHAProtocol` decoder (for AEHA-family protocols) or a raw pulse decode (for everything else) and hands the result to that platform's `update_from_aeha()`/`update_from_raw()` method — the same methods documented in each platform's own section below.

> [!NOTE]
> ESPHome's built-in AEHA decoder reads bits MSB-first, but every AEHA-family protocol on this fork (fujitsu-264, fujitsu legacy, panasonic) actually transmits LSB-first on the wire. Each platform's `update_from_aeha()` un-reverses `address` and each byte of `data` internally (via `IrRemoteBase::reverse_bits8()`/`reverse_bits16()`), so callers never need to.

> [!TIP]
> Our own transmissions get picked up by the same IR receiver as an echo. `IrRemoteBase::rx_locked_out()` suppresses any frame received within 500 ms of our last transmission (tracked via `sendGeneric()`) so it isn't mistaken for a real remote-initiated change.
>
> That's not the only loop to worry about, though: native `select`/`number`/`switch` sub-entities are opt-in child components that a platform's `update_from_*()` calls `publish_state()` on directly (see each platform's section below) — they no longer route back through a YAML `on_value`/`on_press` automation, so the classic "receive → publish_state → on_value → retransmit → picked back up by the receiver → repeat" loop is now structurally prevented for the entities that ship with a platform. Setters still keep a value-only no-op guard (skip if the new value equals the current one) anyway, both as a second line of defense and to avoid redundant retransmits when a sync republishes an unchanged value.

## Batch commands (single-transmission multi-field updates)

By default, ESPHome only batches whatever attributes arrive together in a single `ClimateCall` (i.e. one API `ClimateCommandRequest`) into one IR transmission — `climate_ir::ClimateIR::control()` already applies every field present in *that one call* before transmitting once. The problem is upstream of that: if the caller (e.g. Home Assistant) issues several separate service calls for what a user thinks of as one change (`climate.set_hvac_mode`, then `climate.set_temperature`, then `climate.set_fan_mode`, ...), each separate call is its own `ClimateCall` and still transmits its own IR frame — so the unit can end up beeping several times for one logical change.

Every platform on this fork exposes an `apply_batch(...)` method that takes the same fields a `ClimateCall` would (`mode`/`target_temperature`/`fan_mode`/`swing_mode`, plus any platform-specific extras — see each platform's own section below) as `optional<T>` arguments, applies whichever ones are provided to internal state, and transmits **at most once** no matter how many fields were included. Fields left as `nullopt` keep their current value. `mode`/`fan_mode`/`swing_mode` (`custom_fan_mode` on `fujitsu-264`) are validated against `this->traits()` — an unsupported value is logged (`ESP_LOGW`) and only that one field is skipped, the rest of the batch still applies and transmits normally. `target_temperature` (and any platform-specific numeric field) is clamped to its valid range rather than rejected.

This is meant to be driven from outside ESPHome's own climate API — typically an MQTT subscription whose handler parses a single JSON payload into the matching `optional<T>` arguments and calls `apply_batch(...)` once — rather than as a replacement for the normal `climate:`/`select:`/`switch:`/`number:` entities, which are unaffected and still transmit immediately on their own when changed individually through Home Assistant.

### Verification status

| Platform | `apply_batch()` | Verified on |
|---|---|---|
| `fujitsu-264` | yes | Real hardware — bedroom unit (2026-08-05) |
| `mitsubishi` | yes (model `MITSUBISHI_AC` only, matching receive mode) | Real hardware — living-room and workshop MSZ-series units (2026-08-05) |
| `fujitsu` (legacy) | yes | Compile-tested only — **unverified** on real hardware |
| `panasonic` | yes | Compile-tested only — **unverified** on real hardware |
| `electra` | yes | Compile-tested only — **unverified** on real hardware |
| `sharp` | yes | Compile-tested only — **unverified** on real hardware |

As with receive mode, the four "compile-tested only" platforms mirror the two verified platforms' `apply_batch()` shape (same field validation/clamping approach) without a physical unit to confirm against.

### Standard fields

Every platform accepts at least these four; see each platform's own section for its exact signature (order matters — arguments are positional) and any extras:

| Field | Type | Notes |
|---|---|---|
| `mode` | `optional<climate::ClimateMode>` | Validated against `traits().supports_mode()` |
| `target_temperature` | `optional<float>` | Clamped to `traits().get_visual_min_temperature()`/`get_visual_max_temperature()` |
| `fan_mode` | `optional<climate::ClimateFanMode>` | Validated against `traits().supports_fan_mode()`. `fujitsu-264` takes `optional<std::string> custom_fan_mode` instead — see its own section |
| `swing_mode` | `optional<climate::ClimateSwingMode>` | Validated against `traits().supports_swing_mode()` |

> [!NOTE]
> Preset/toggle-style commands (fujitsu legacy's Eco/Powerful presets, fujitsu-264's `toggle_powerful()`/`toggle_sterilization()`) are deliberately **not** part of `apply_batch()` on any platform. Each is sent as its own separate command frame, distinct from the main mode/temp/fan/swing state frame the hardware understands — folding one into the batch wouldn't save a transmission anyway, since the unit has no single frame that carries both. Call those methods directly instead.

### Example

```yaml
button:
  - platform: template
    name: 'Batch example'
    on_press:
      then:
        - lambda: |-
            id(my_climate).apply_batch(climate::CLIMATE_MODE_COOL, 24.0f,
                                        climate::CLIMATE_FAN_HIGH, climate::CLIMATE_SWING_VERTICAL);
```

A more realistic use is an MQTT `subscribe_json()` handler that parses per-field JSON keys into the matching `optional<T>` arguments (leaving absent keys as `nullopt`) and calls `apply_batch(...)` once per message — see `configurations/all.yaml` for a compile-tested call against each platform, and the fork's own downstream smart-hub devices for a full MQTT-driven example.

## fujitsu

```yaml
climate:
  - platform: fujitsu
    model: XXXXXX
    name: 'Living Room AC'
```

#### Horizontal swing

Use this option to disable horizontal swing regardless of your remote (it is enabled for `ARRAH2E` and `ARJW2`). Set it to `false` if your unit uses one of these protocols but actually lacks horizontal swing.

```yaml
climate:
  - platform: fujitsu
    model: ARRAH2E
    horizontal_swing: false
    name: 'Living Room AC'
```

#### Control fan direction

You can call the `step_vertical()` and `step_horizontal()` (if supported) methods on the climate controller.

```yaml
button:
  - platform: template
    name: 'Step vertical'
    on_press:
      then:
        - lambda: |-
            id(my_climate).step_vertical();
```

#### Eco and Powerful (boost) modes

The Fujitsu IR protocol implements Eco and Powerful as toggle commands (no state bit), so the platform tracks the assumed state internally. They are exposed in two ways:

1. As HA presets (`Eco` and `Boost`) — switch them via the climate card's preset selector.
2. As lambda methods `toggle_econo()` / `toggle_powerful()` for advanced use:

```yaml
button:
  - platform: template
    name: 'Toggle Eco'
    on_press:
      then:
        - lambda: |-
            id(my_climate).toggle_econo();
```

> [!NOTE]
> Because there is no state feedback, toggling Eco or Powerful from the physical IR remote will desync the assumed state until the next ESPHome-initiated change. Both default to `off` on power-on, matching a fresh boot of the indoor unit.

#### Receive mode

Supported via `receiver_id:` on the `climate:` block (see [Receive mode](#receive-mode-syncing-from-a-physical-remote) above). **Compile-tested only, unverified on real hardware** — no physical unit of this legacy protocol family was available; `update_from_aeha()` was implemented by mirroring fujitsu-264's own verified receive path (it shares the same `0x14 0x63` vendor header and LSB-first-on-the-wire convention).

Only the `ARRAH2E`-family long (16-byte) and short (7-byte) code lengths are recognized — `ARDB1`/`ARJW2`'s one-byte-shorter long/short codes aren't handled. Short frames other than power-off (econo/powerful toggles, step-vane commands) carry no absolute mode/temp/fan/swing payload and are only logged (`ESP_LOGD`), not synced onto the climate entity.

#### Batch commands

`apply_batch(mode, target_temperature, fan_mode, swing_mode)` — see [Batch commands](#batch-commands-single-transmission-multi-field-updates) above for the general shape. Just the four standard fields; does **not** accept a preset (Eco/Powerful) — call `toggle_econo()`/`toggle_powerful()` directly for those, same reasoning as the note above. **Compile-tested only, unverified on real hardware.**

## fujitsu-264

This platform implements the special Fujitsu protocol of the `AR-RLB2J` remote.

**Verified remotes:** `AR-RLB2J`, `AR-RLB1J`

```yaml
climate:
  - platform: fujitsu_264
    name: 'Living Room AC'
    # Enables receive mode — see "Receive mode" subsection below.
    receiver_id: my_receiver
```

#### Toggle powerful

You can call the `toggle_powerful()` methods on the climate controller.

```yaml
button:
  - platform: template
    name: 'Toggle powerful'
    on_press:
      then:
        - lambda: |-
            id(my_climate).toggle_powerful();
```

#### Set fan angle

Low-level passthrough to the underlying library's `setFanAngle()`. Prefer `set_vertical_angle()` below on units with 8 positions; this method is limited to what the library itself accepts.

```yaml
button:
  - platform: template
    name: 'Set fan angle'
    on_press:
      then:
        - lambda: |-
            // Acceptable values are 1-7 and 15(means stay)
            id(my_climate).set_fan_angle(1);
```

#### Fan speed labels

This platform exposes fan speed as a `custom_fan_mode` using the physical remote's own labels (自動/静音/微風/弱風/強風) instead of ESPHome's generic Low/Medium/High, so the Home Assistant UI reads the same as the remote. Select it from `climate.fan_mode` / `climate.set_custom_fan_mode` as usual; there is no YAML option to change the labels.

#### Native select / number / switch entities

Every extra control beyond the standard climate entity (weak dry, fixed louver angles, the auto-mode temperature offset, internal clean) is exposed as an **opt-in native sub-platform** parented to the `fujitsu_264` climate via `fujitsu_264_id:`. If an entity isn't declared in your YAML, it simply doesn't exist — the component keeps working internally with its own standard value (documented per-entity below) and nothing is added to Home Assistant. All declared `select`/`number` entities persist their value across reboots.

```yaml
select:
  - platform: fujitsu_264
    fujitsu_264_id: my_climate
    # Dry mode strength: "通常" (normal) / "ひかえめ" (gentle). Applied the next
    # time dry mode is transmitted (immediately if already in dry mode).
    # Picking a *different* value while not in dry mode switches into dry mode.
    # Component default when undeclared: "通常" (normal).
    weak_dry:
      name: 'Weak dry'
    # Fixed vertical (up/down) louver position, 1 (up) .. 8 (down). Selecting
    # a position stops continuous vertical swing.
    # Component default when undeclared: 1.
    vertical_angle:
      name: 'Vertical angle'
    # Fixed horizontal (left/right) louver position, 1 (left) .. 5 (right).
    # No field in the underlying library at all — reverse-engineered from a
    # real-remote capture. Selecting a position stops continuous horizontal
    # swing. Component default when undeclared: 1.
    horizontal_angle:
      name: 'Horizontal angle'

number:
  - platform: fujitsu_264
    fujitsu_264_id: my_climate
    # Auto (heat/cool) mode temperature adjustment, -2.0..+2.0 in 0.5 steps.
    # In auto mode the unit picks its own base temperature and only accepts
    # this relative offset, so the climate entity's target_temperature is
    # pinned to a fixed display value of 24°C while in auto mode.
    # Component default when undeclared: 0.
    temp_auto_offset:
      name: 'Auto mode temperature offset'

switch:
  - platform: fujitsu_264
    fujitsu_264_id: my_climate
    # Internal clean function, direct sense: ON = enabled. This entity
    # defaults to RESTORE_DEFAULT_ON (ON at first boot, then whatever was
    # persisted across reboots) -- but if this entity is NOT declared at all,
    # the component's own internal default is OFF. That asymmetry is
    # intentional: the entity's default matches what a fresh AC unit already
    # does out of the box, while the component's fallback (with no entity to
    # restore from) stays conservative.
    internal_clean:
      name: 'Internal clean'
```

`CLIMATE_SWING_HORIZONTAL` and `CLIMATE_SWING_BOTH` are supported via the climate entity's standard `swing_mode`, in addition to the fixed-angle selects above.

#### Sterilization

You can call the `toggle_sterilization()` method on the climate controller.

```yaml
button:
  - platform: template
    name: 'Toggle sterilization'
    on_press:
      then:
        - lambda: |-
            id(my_climate).toggle_sterilization();
```

> [!NOTE]
> The sterilization command is only accepted by the unit while it is powered off.

#### Receive mode

`update_from_aeha(address, data)` decodes a frame captured by ESPHome's built-in AEHA decoder and updates the climate entity — mode, target temperature (or the auto-mode offset), fan speed, both swing axes and their fixed angles, weak-dry, and internal-clean state — to match what the physical remote just sent, publishing any declared native `select`/`number`/`switch` entities above to match. It's wired up automatically by `on_receive()` once `receiver_id:` is set on the `climate:` block (see [Receive mode](#receive-mode-syncing-from-a-physical-remote) above) — no YAML lambda required.

> [!NOTE]
> ESPHome's built-in AEHA decoder reads bits MSB-first, but the Fujitsu-264 protocol transmits LSB-first on the wire. `update_from_aeha()` un-reverses `address` and each byte of `data` internally, so callers don't need to.

**Verified remotes:** `AR-RLB1J`.

#### Batch commands

```cpp
void apply_batch(optional<climate::ClimateMode> mode,
                  optional<float> target_temperature,
                  optional<std::string> custom_fan_mode,
                  optional<climate::ClimateSwingMode> swing_mode,
                  optional<uint8_t> vertical_angle,
                  optional<uint8_t> horizontal_angle,
                  optional<bool> weak_dry,
                  optional<float> temp_auto_offset,
                  optional<bool> clean);
```

See [Batch commands](#batch-commands-single-transmission-multi-field-updates) above for the general shape. Takes `custom_fan_mode` (a `std::string` matching one of 自動/静音/微風/弱風/強風, validated via the protected `find_custom_fan_mode_()` rather than `traits()` — see the note below) instead of a `fan_mode` enum, plus this platform's own extras: `vertical_angle`/`horizontal_angle` (same 1-8/1-5 ranges as their selects, clamped), `weak_dry` (same as the `weak_dry` select, but does **not** auto-switch into dry mode the way the select's `on_value` handler does — include `mode: dry` in the same batch for that), and `temp_auto_offset` (clamped to -2.0..+2.0). Does not accept `toggle_powerful()`/`toggle_sterilization()` — see the note above.

> [!NOTE]
> `custom_fan_mode` validation can't reuse `traits().supports_custom_fan_mode()` the way `mode`/`swing_mode` reuse `traits().supports_mode()`/`supports_swing_mode()`: `ClimateIR::traits()` (unmodified by this platform) never populates custom fan modes onto the `ClimateTraits` object it returns — that accessor only serves an unrelated, mostly-deprecated path. The actual supported list set by this platform's constructor lives on the `Climate` object itself, so `apply_batch()` checks it via the protected `find_custom_fan_mode_()` instead (same as `Climate::set_custom_fan_mode_()` does internally). This was caught during real-hardware verification: an earlier version of this method used `traits().supports_custom_fan_mode()` and rejected every custom fan mode as "unsupported" even though all five are registered.

**Verified on real hardware** (bedroom unit, 2026-08-05) — including the custom_fan_mode fix above.

## panasonic

```yaml
climate:
  - platform: panasonic
    model: XXXXXX
    name: 'Living Room AC'
```

#### Receive mode

Supported via `receiver_id:` on the `climate:` block (see [Receive mode](#receive-mode-syncing-from-a-physical-remote) above). **Compile-tested only, unverified on real hardware** — no physical unit was available; `update_from_aeha()` was implemented by mirroring fujitsu-264's own verified receive path, on the (unverified) assumption this protocol is also LSB-first on the wire.

This protocol's real frame is 2 AEHA sections (8 bytes + 19 bytes) separated by a gap that normally splits them into separate receive events at `remote_receiver`'s default idle threshold. `update_from_aeha()` handles all three shapes it might see: a lone 8-byte section 1 carries no climate state and is just logged; a lone 19-byte section 2 has the constant 8-byte section-1 prefix prepended before use; a 27-byte frame (both sections decoded as one, e.g. with a shorter idle threshold) is used directly.

#### Batch commands

`apply_batch(mode, target_temperature, fan_mode, swing_mode)` — see [Batch commands](#batch-commands-single-transmission-multi-field-updates) above. Just the four standard fields; `swing_mode`/`mode` validation naturally respects this platform's model-dependent horizontal-swing restriction (see `traits()` above) since it's checked dynamically. **Compile-tested only, unverified on real hardware.**

## electra

Also known as Aux.

```yaml
climate:
  - platform: electra
    name: 'Living Room AC'
```

#### Receive mode

Supported via `receiver_id:` on the `climate:` block (see [Receive mode](#receive-mode-syncing-from-a-physical-remote) above). **Compile-tested only, unverified on real hardware** — no physical unit was available; `update_from_raw()` was implemented by mirroring mitsubishi's own verified raw-pulse receive path.

This protocol has no ESPHome built-in decoder and isn't AEHA-timed (its leader is 9166/4470 µs vs. AEHA's 3400/1700 µs), so it's decoded from raw pulses via the shared `decode_pulses()` helper instead of the AEHA path. Electra frames may arrive with repeats; `decode_pulses()` stops at the first pulse pair that doesn't fit the expected bit timing, so a single clean frame is what's expected.

#### Batch commands

`apply_batch(mode, target_temperature, fan_mode, swing_mode)` — see [Batch commands](#batch-commands-single-transmission-multi-field-updates) above. Just the four standard fields; no platform-specific extras. **Compile-tested only, unverified on real hardware.**

## sharp

```yaml
climate:
  - platform: sharp
    model: XXXXXX
    name: 'Living Room AC'
```

#### Receive mode

Supported via `receiver_id:` on the `climate:` block (see [Receive mode](#receive-mode-syncing-from-a-physical-remote) above). **Compile-tested only, unverified on real hardware** — no physical unit was available; `update_from_raw()` was implemented by mirroring mitsubishi's own verified raw-pulse receive path.

Same reasoning as electra: no ESPHome built-in decoder and not AEHA-timed (leader 3800/1900 µs), so it's decoded from raw pulses via `decode_pulses()`.

#### Batch commands

`apply_batch(mode, target_temperature, fan_mode, swing_mode)` — see [Batch commands](#batch-commands-single-transmission-multi-field-updates) above. Just the four standard fields; no platform-specific extras. **Compile-tested only, unverified on real hardware.**

## mitsubishi

```yaml
climate:
  - platform: mitsubishi
    model: MITSUBISHI_AC
    name: 'Living Room AC'
    # Enables receive mode (MITSUBISHI_AC only) — see "Receive mode" below.
    receiver_id: my_receiver
```

#### Model-based restriction options

`model: MITSUBISHI_AC` exposes the widest feature set (auto/fan-only modes, quiet fan speed, horizontal swing); `MITSUBISHI136`/`MITSUBISHI112` are narrower physical protocols with their own fixed feature sets. On a real `MITSUBISHI_AC` unit that doesn't implement every feature the model otherwise supports, narrow it further with these options — they only ever remove support, never add it beyond what the `model` already exposes:

```yaml
climate:
  - platform: mitsubishi
    model: MITSUBISHI_AC
    name: 'Living Room AC'
    supports_auto: false
    supports_fan_only: false
    horizontal_swing: false
    supports_quiet_fan: false
```

#### Native select / switch entities

As with fujitsu-264, these are opt-in native sub-platforms parented to the climate via `mitsubishi_id:`. Undeclared entities don't exist in Home Assistant; the component keeps its own internal default (documented per-entity below).

```yaml
select:
  - platform: mitsubishi
    mitsubishi_id: my_climate
    # Dry mode strength: "弱" (weak) / "標準" (normal) / "強" (strong).
    # Picking a *different* value while not in dry mode switches into dry
    # mode. Received frames only sync this while the unit is actually in dry
    # mode -- other modes carry unrelated noise in the same byte nibble.
    # Component default when undeclared: "標準" (normal).
    dry_level:
      name: 'Dry level'
    # Fixed vertical louver position: "自動" (auto) plus 5 fixed positions.
    # Picking a position (including auto) stops continuous vertical swing;
    # turning vertical swing off via the climate's own swing control instead
    # resets this back to auto. Component default when undeclared: auto.
    vertical_vane:
      name: 'Vertical vane'

switch:
  - platform: mitsubishi
    mitsubishi_id: my_climate
    # Internal clean function, direct sense: ON = enabled. Same
    # RESTORE_DEFAULT_ON-entity / OFF-component-default asymmetry as
    # fujitsu-264's internal_clean (see that platform's section above).
    # Unlike the other switches here, toggling this does not transmit
    # immediately -- the flag rides along on whatever frame is next sent for
    # another reason (confirmed on real hardware).
    internal_clean:
      name: 'Internal clean'
    # Powerful (boost) mode, a persistent flag in the state frame (not a
    # stateless toggle like fujitsu-264's toggle_powerful()). Transmits
    # immediately. Component default when undeclared: off.
    powerful:
      name: 'Powerful'
    # 電流切換 (current limit): ON = limit max operating current to ~10A
    # ("小", for weaker household breakers), OFF = normal ~15A ("通常").
    # Transmits immediately. Component default when undeclared: off (normal).
    current_cut:
      name: 'Current cut'
```

`set_isee()`/`get_isee()` (a thin wrapper around the underlying library's own `setISee()`/`getISee()`, not exposed as an entity) is also available for advanced lambda use — combining a plain Cool mode call with `set_isee(false)` reproduces the remote's own Cool+ISee-off "weak cool" preset button.

```yaml
button:
  - platform: template
    name: 'Weak cool preset'
    on_press:
      then:
        - climate.control:
            id: my_climate
            mode: COOL
        - lambda: |-
            id(my_climate).set_isee(false);
```

#### Receive mode

Receive sync is implemented only for `model: MITSUBISHI_AC` (the model this was verified against on real hardware); other models' `update_from_raw()`/`update_from_aeha()` return `false` immediately. It's wired up automatically by `on_receive()` once `receiver_id:` is set on the `climate:` block (see [Receive mode](#receive-mode-syncing-from-a-physical-remote) above) — no YAML lambda required. Two things get decoded:

- `update_from_raw(pulses)`: the main 18-byte state frame (mode, target temperature, fan speed 1/2/3/auto/quiet, both swing axes and the vertical vane position, dry level while in dry mode, internal clean, powerful, current cut), decoded from raw pulses via the shared `decode_pulses()` helper (this protocol has no ESPHome built-in decoder).
- `update_from_aeha(address, data)`: this remote also sends short AEHA-timed side-channel toggle codes (dry-level cycle button, internal-clean button) separately from the main state frame, at a dedicated address (`0xC4D3`). Unlike fujitsu-264, this address does **not** need bit-reversal.

Dry level is only synced from either source while the unit is currently in dry mode — the same byte/nibble carries unrelated, mode-correlated noise in other modes (observed on real hardware: reads as "weak" in Cool, "strong" in Heat).

**Verified hardware:** living-room MSZ-series unit (2026-07).

#### Batch commands

```cpp
void apply_batch(optional<climate::ClimateMode> mode,
                  optional<float> target_temperature,
                  optional<climate::ClimateFanMode> fan_mode,
                  optional<climate::ClimateSwingMode> swing_mode,
                  optional<uint8_t> vertical_vane,
                  optional<uint8_t> dry_level,
                  optional<bool> powerful,
                  optional<bool> current_cut,
                  optional<bool> clean);
```

See [Batch commands](#batch-commands-single-transmission-multi-field-updates) above for the general shape. Plus this platform's own extras: `vertical_vane`/`dry_level` (same 0-5/0-2 ranges as their selects, clamped) and `powerful`/`current_cut`/`clean` (same as their switches). `dry_level` does **not** auto-switch into dry mode the way the select's `on_value` handler does — include `mode: dry` in the same batch for that.

**Verified on real hardware** (living-room and workshop units, 2026-08-05).

## Changelog

- **2026.08.05**: Add `apply_batch()` to every platform (`mitsubishi`, `fujitsu-264`, `fujitsu` (legacy), `panasonic`, `electra`, `sharp`) — applies any combination of the standard climate fields (plus platform-specific extras on `mitsubishi`/`fujitsu-264`) from a single call and transmits at most once, no matter how many fields changed. Intended to be driven by something other than ESPHome's own climate API (e.g. an MQTT subscription), where the caller would otherwise make several separate calls that each transmit their own IR frame. `mitsubishi`/`fujitsu-264` verified on real hardware (living-room, workshop and bedroom units); the other four platforms are compile-tested only, mirroring the same field-validation/clamping approach. Also fixes a `fujitsu-264` `apply_batch()` bug found during that verification: `custom_fan_mode` validation checked `traits().supports_custom_fan_mode()`, which this platform never populates (custom fan modes live on the `Climate` object itself, not on the `ClimateTraits` value `traits()` returns) and so always returned false, rejecting every custom fan mode as unsupported — switched to the correct `find_custom_fan_mode_()` check.
- **2026.07.12**: Add receive mode (`update_from_aeha()`/`update_from_raw()` via `receiver_id:`) to `fujitsu` (legacy), `panasonic`, `electra` and `sharp` — compile-tested only, unverified on real hardware. Release `v2026.07`.
- **2026.07.11**: Move receive-sync from YAML `on_aeha:`/`on_raw:` lambdas into each climate's own `on_receive()` override, enabled by a `receiver_id:` on the `climate:` block (no lambda needed any more); turn `select`/`switch`/`number` entities into native, opt-in ESPHome sub-platforms on fujitsu-264 and mitsubishi (`weak_dry`/`vertical_angle`/`horizontal_angle`/`temp_auto_offset`/`internal_clean` and `dry_level`/`vertical_vane`/`internal_clean`/`powerful`/`current_cut` respectively), replacing the old `platform: template` + lambda pattern; un-invert the Internal Clean switch (ON now means enabled, default ON, persisted across reboots); pin the shared `ir_remote_base` loader's `IRremoteESP8266` version to `2.9.0`.
- **2026.07.09**: Move receive-sync helpers (`reverse_bits8()`/`reverse_bits16()`, `rx_locked_out()`, `decode_pulses()`) onto `ir_remote_base::IrRemoteBase` so other platforms can share them when adding receive mode; refactor fujitsu-264 to use the shared helpers (no behavior change); add receive mode, `set_clean()`/`set_dry_level()`/`set_vertical_vane()`/`set_powerful()`/`set_current_cut()`/`set_isee()`, and model-restriction options (`supports_auto`/`supports_fan_only`/`horizontal_swing`/`supports_quiet_fan`) to the mitsubishi platform, verified on a living-room MSZ-series unit
- **2026.07.08**: Add receive mode to fujitsu-264 platform (`update_from_aeha()`, syncs climate/select/switch/number state from frames sent by the physical remote), `set_temp_auto_offset()` (auto-mode temperature adjustment), `set_vertical_angle()`/`set_horizontal_angle()` (fixed louver position, 1-8/1-5), `CLIMATE_SWING_HORIZONTAL`/`CLIMATE_SWING_BOTH` support, and custom fan-speed labels (自動/静音/微風/弱風/強風); all verified with `AR-RLB1J` remote
- **2026.07.06**: Add `set_clean()`, `toggle_sterilization()` and `set_weak_dry()` methods to fujitsu-264 platform, verified with `AR-RLB1J` remote

- **2026.06.06**: Add `horizontal_swing` to Fujitsu platform
- **2026.05.16**: Add Eco and Powerful presets to Fujitsu platform
- **2026.03.28**: Add Mitsubishi platform
- **2026.02.21**: Add Sharp platform
- **2026.02.19**: Use latest version of IRremoteESP8266
- **2025.07.28**: Add fujitsu-264 platform
- **2025.07.21**: Compatibility with ESPHome 2025.7
- **2025.07.07**: Add Electra platform
- **2025.07.06**: Add Panasonic platform
- **2025.06.04**: Add `step_vertical()` and `step_horizontal()` methods to Fujitsu platform
- **2025.05.22**: Compatibility with ESPHome 2025.5

## Contributors

<!-- readme: contributors -start -->
<table>
	<tbody>
		<tr>
            <td align="center">
                <a href="https://github.com/mistic100">
                    <img src="https://avatars.githubusercontent.com/u/41597?v=4" width="100;" alt="mistic100"/>
                    <br />
                    <sub><b>Damien Sorel</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/gnoto">
                    <img src="https://avatars.githubusercontent.com/u/54668134?v=4" width="100;" alt="gnoto"/>
                    <br />
                    <sub><b>gnoto</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/hldh214">
                    <img src="https://avatars.githubusercontent.com/u/5501843?v=4" width="100;" alt="hldh214"/>
                    <br />
                    <sub><b>Jim Ryu</b></sub>
                </a>
            </td>
            <td align="center">
                <a href="https://github.com/diredocks">
                    <img src="https://avatars.githubusercontent.com/u/26994007?v=4" width="100;" alt="diredocks"/>
                    <br />
                    <sub><b>dd</b></sub>
                </a>
            </td>
		</tr>
	<tbody>
</table>
<!-- readme: contributors -end -->
