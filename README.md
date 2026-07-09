# IRremoteESP8266 components for ESPHome

This is a collection for `climate` implementations using the awesome [IRremoteESP8266](https://github.com/crankyoldgit/IRremoteESP8266) library.

For now only some protocols are implemented, please open an issue or an PR to add more.

Most platforms are transmit-only. `fujitsu-264` additionally supports **receive mode**: it can decode frames sent by the physical remote (captured via ESPHome's `remote_receiver`) and sync the climate entity's state to match. See [Receive mode](#receive-mode-syncing-from-a-physical-remote) below.

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
      url: https://github.com/mistic100/ESPHome-IRremoteESP8266
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

## Receive mode (syncing from a physical remote)

Some platforms can also decode frames sent by the *physical* remote and reflect them onto the climate entity (and any related `select`/`switch`/`number` entities), so Home Assistant stays in sync when someone uses the remote directly instead of the app. This is opt-in per platform — check the platform's own section for a `update_from_aeha()` / `update_from_raw()` method before relying on it; if a platform doesn't mention one, it doesn't support receive mode yet.

The general pattern is a `remote_receiver` wired to the same IR receiver diode, with an `on_aeha` (for AEHA-family protocols) or `on_raw` (for everything else) trigger that hands the decoded frame to the climate entity:

```yaml
remote_receiver:
  id: my_receiver
  pin: GPIOXX
  dump: []
  # ESP32-S3 only decodes short frames without this — see note below.
  use_dma: true
  rmt_symbols: 512
  receive_symbols: 512

  on_aeha:
    then:
      - lambda: |-
          if (id(my_climate).update_from_aeha(x.address, x.data)) {
            // The climate entity itself is already updated by update_from_aeha();
            // publish any other entities that mirror its state so their displayed
            // value follows a remote-initiated change too.
            id(some_related_select).publish_state(id(my_climate).get_something());
          }
```

> [!NOTE]
> ESPHome's built-in `remote_receiver` on an ESP32-S3 only allocates 192 RMT symbols by default, enough for short frames but not for longer ones (e.g. fujitsu-264's 33-byte/264-bit frame is ~266 symbols). If frames longer than a few bytes are getting dropped or truncated, set `use_dma: true` together with `rmt_symbols: 512` and `receive_symbols: 512` (or higher) as shown above.

> [!TIP]
> If you're writing a `set_xxx()` method that both transmits *and* is called from your own `on_value`/`on_press` automations (e.g. a `select` or `number` entity mirroring physical-remote state), give it a no-op guard when the new value equals the current one. Without it, a remote-initiated change flows: physical remote → `update_from_aeha()` → entity `publish_state()` → the entity's `on_value` → your `set_xxx()` → a retransmission → picked back up by the receiver → repeat forever. `rx_locked_out()` (the internal guard platforms like fujitsu-264 use against their own echo) only suppresses *our own* transmission's echo for a short window; it does not prevent this second, unrelated loop through an entity's own automation.

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

## fujitsu-264

This platform implements the special Fujitsu protocol of the `AR-RLB2J` remote.

**Verified remotes:** `AR-RLB2J`, `AR-RLB1J`

```yaml
climate:
  - platform: fujitsu_264
    name: 'Living Room AC'
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

#### Vertical angle

Fixed vertical (up/down) louver position via `set_vertical_angle()` / `get_vertical_angle()`. Unlike `set_fan_angle()` above, this accepts the full **1 (up) to 8 (down)** range some units support, stops continuous vertical swing when sent, and updates the entity's `swing_mode` accordingly.

```yaml
select:
  - platform: template
    name: 'Vertical angle'
    id: select_vertical_angle
    options: ["1", "2", "3", "4", "5", "6", "7", "8"]
    optimistic: true
    on_value:
      then:
        - lambda: |-
            id(my_climate).set_vertical_angle(atoi(x.c_str()));
```

#### Horizontal swing and angle

`CLIMATE_SWING_HORIZONTAL` and `CLIMATE_SWING_BOTH` are supported via the climate entity's standard `swing_mode`, alongside a fixed **1 (left) to 5 (right)** horizontal louver position via `set_horizontal_angle()` / `get_horizontal_angle()` (mirrors `set_vertical_angle()` above, but for the horizontal axis).

```yaml
select:
  - platform: template
    name: 'Horizontal angle'
    id: select_horizontal_angle
    options: ["1", "2", "3", "4", "5"]
    optimistic: true
    on_value:
      then:
        - lambda: |-
            id(my_climate).set_horizontal_angle(atoi(x.c_str()));
```

#### Fan speed labels

This platform exposes fan speed as a `custom_fan_mode` using the physical remote's own labels (自動/静音/微風/弱風/強風) instead of ESPHome's generic Low/Medium/High, so the Home Assistant UI reads the same as the remote. Select it from `climate.fan_mode` / `climate.set_custom_fan_mode` as usual; there is no YAML option to change the labels.

#### Auto mode temperature offset

In auto (heat/cool) mode the unit picks its own base temperature and only accepts a **-2.0 to +2.0** adjustment (in 0.5 steps) on top of it, so the climate entity's absolute `target_temperature` is meaningless there — it is pinned to a fixed display value of 24°C while in auto mode. Use `set_temp_auto_offset()` / `get_temp_auto_offset()` instead:

```yaml
number:
  - platform: template
    name: 'Auto mode temperature offset'
    id: number_auto_temp_offset
    min_value: -2.0
    max_value: 2.0
    step: 0.5
    unit_of_measurement: "°C"
    optimistic: true
    restore_value: true
    on_value:
      then:
        - lambda: |-
            id(my_climate).set_temp_auto_offset(x);
```

#### Internal clean

You can call the `set_clean()` method on the climate controller to enable or disable the internal clean function.

```yaml
switch:
  - platform: template
    name: 'Clean'
    optimistic: true
    turn_on_action:
      then:
        - lambda: |-
            id(my_climate).set_clean(true);
    turn_off_action:
      then:
        - lambda: |-
            id(my_climate).set_clean(false);
```

#### Weak dry

You can call the `set_weak_dry()` method on the climate controller to switch the dry mode between normal and weak ("ひかえめ"). The setting is applied the next time dry mode is transmitted (immediately if the unit is already in dry mode).

```yaml
switch:
  - platform: template
    name: 'Weak dry'
    optimistic: true
    turn_on_action:
      then:
        - lambda: |-
            id(my_climate).set_weak_dry(true);
    turn_off_action:
      then:
        - lambda: |-
            id(my_climate).set_weak_dry(false);
```

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

#### Receive mode (sync from remote)

`update_from_aeha(address, data)` decodes a frame captured by ESPHome's built-in AEHA decoder (see [Receive mode](#receive-mode-syncing-from-a-physical-remote) above for the general `remote_receiver` setup) and updates the climate entity — mode, target temperature (or the auto-mode offset), fan speed, both swing axes and their fixed angles, weak-dry, and internal-clean state — to match what the physical remote just sent. It returns `true` if the frame was recognized and applied.

```yaml
remote_receiver:
  id: my_receiver
  pin: GPIOXX
  dump: []
  use_dma: true
  rmt_symbols: 512
  receive_symbols: 512

  on_aeha:
    then:
      - lambda: |-
          if (id(my_climate).update_from_aeha(x.address, x.data)) {
            // Mirror any other entities that track fujitsu-264 state not
            // exposed directly through the climate entity itself.
            id(select_dry_strength).publish_state(id(my_climate).get_weak_dry() ? "弱" : "標準");
            id(switch_internal_clean).publish_state(id(my_climate).get_clean());
            id(number_auto_temp_offset).publish_state(id(my_climate).get_temp_auto_offset());
            id(select_vertical_angle).publish_state(to_string(id(my_climate).get_vertical_angle()));
            id(select_horizontal_angle).publish_state(to_string(id(my_climate).get_horizontal_angle()));
          }
```

> [!NOTE]
> ESPHome's built-in AEHA decoder reads bits MSB-first, but the Fujitsu-264 protocol transmits LSB-first on the wire. `update_from_aeha()` un-reverses `address` and each byte of `data` internally, so callers don't need to.

**Verified remotes:** `AR-RLB1J`.

## panasonic

```yaml
climate:
  - platform: panasonic
    model: XXXXXX
    name: 'Living Room AC'
```

## electra

Also known as Aux.

```yaml
climate:
  - platform: electra
    name: 'Living Room AC'
```

## sharp

```yaml
climate:
  - platform: sharp
    model: XXXXXX
    name: 'Living Room AC'
```

## mitsubishi

```yaml
climate:
  - platform: mitsubishi
    model: XXXXXX
    name: 'Living Room AC'
```

## Changelog

- **2026.07.09**: Move receive-sync helpers (`reverse_bits8()`/`reverse_bits16()`, `rx_locked_out()`, `decode_pulses()`) onto `ir_remote_base::IrRemoteBase` so other platforms can share them when adding receive mode; refactor fujitsu-264 to use the shared helpers (no behavior change)
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
