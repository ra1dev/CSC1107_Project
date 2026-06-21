# kbmonitor Test Plan

## Build Tests

| Test | Command | Expected Result |
| --- | --- | --- |
| Build all | `make` | `kernel/kbmonitor.ko` and `user/kbmon` are created |
| Clean build outputs | `make clean` | Kernel and user build artifacts are removed |
| Rebuild after clean | `make clean && make` | Build still succeeds |
| Setup helper | `bash scripts/setup_pi.sh` | Required packages are installed and kernel headers are found |
| Validation helper | `bash scripts/validate_all.sh` | Normal build, module lifecycle, user commands, text build, and TLS cert helper are checked |

## Module Lifecycle Tests

| Test | Command | Expected Result |
| --- | --- | --- |
| Load module | `bash scripts/load.sh` | Module loads and `/dev/kbmonitor` plus `/dev/kbmonitor_log` exist |
| Confirm module | `lsmod \| grep kbmonitor` | `kbmonitor` is listed |
| Check logs | `sudo dmesg \| tail` | Load and device creation logs appear |
| Unload module | `bash scripts/unload.sh` | Module unloads cleanly |
| Confirm removal | `lsmod \| grep kbmonitor` | No `kbmonitor` entry remains |

## Character Device Tests

| Test | Command | Expected Result |
| --- | --- | --- |
| Read summary | `./user/kbmon summary` | Valid key-value stats are printed |
| Manual read | `cat /dev/kbmonitor` | Summary stats are printed |
| Manual log read | `cat /dev/kbmonitor_log` | Recent keypress log entries are printed |
| Select view | `echo "view summary" > /dev/kbmonitor` | Command succeeds |
| Status view | `./user/kbmon status` | Driver/device status is printed |
| Event view | `./user/kbmon events` | Recent keypress history is printed |
| Log view | `./user/kbmon log` | Recent keypress log metadata is printed |
| Report export | `./user/kbmon export` | JSON evidence is printed and contains `"exports_text": false` |
| Reset | `./user/kbmon reset` | Counters return to zero |
| Invalid command | `echo bad > /dev/kbmonitor` | Command fails and `dmesg` logs warning |

## Keyboard Activity Tests

| Test | Steps | Expected Result |
| --- | --- | --- |
| Initial state | Load module, run `./user/kbmon summary` | `total_presses=0` before activity |
| Count key presses | Press 10 keys, run summary | `total_presses` increases by about 10 |
| Ignore releases | Press and release one key | Only the press is counted |
| Track repeats | Hold one key | `repeat_events` may increase; `total_presses` should not increase for every repeat |
| Active keyboard | Load with USB keyboard connected | `active_keyboards=1` or greater |
| Ring buffer | Press more than 64 keys | `buffered_events=64`, `buffer_dropped` increases |
| Rate stats | Press keys quickly, run summary | `presses_last_10s`, `presses_per_minute`, and `peak_presses_per_second` are present |
| Event history labels | Press known keys, run `./user/kbmon events` | Recent events show key labels and Linux key codes |
| Linux key-name log | Press `A` and Enter, run `./user/kbmon log` | Output includes `key=KEY_A` and `key=KEY_ENTER` |
| Log ring buffer | Press more than 128 keys, run `./user/kbmon log` | `events=128`, `log_dropped` increases |

## Level 2 Analytics Tests

| Test | Steps | Expected Result |
| --- | --- | --- |
| Select key view | `./user/kbmon keys` | Output shows `view=keys` |
| Per-key count | Press `A` three times, run `./user/kbmon keys` | Output contains `key_30=3` or higher |
| Category count | Press letters and digits, run `./user/kbmon keys` | `letters` and `digits` increase correctly |
| Top keys | Press one key repeatedly, run `./user/kbmon keys` | Top key code matches the repeated key |
| Friendly key view | `./user/kbmon keys` | Output uses key labels, not raw Linux key-code fields |
| Per-key table | `./user/kbmon keys` | Pressed keys show `KEY`, `CODE`, and `COUNT` columns |
| Heatmap | `./user/kbmon heatmap` | Keyboard layout displays `[KEY:count]` values |

## Level 3 Local Text Demo Tests

Build with:

```bash
make clean
make TEXT_MODE=1
```

| Test | Steps | Expected Result |
| --- | --- | --- |
| Default guardrail | Build with normal `make`, run `./user/kbmon text` | Driver rejects text mode as unsupported |
| Compile-time enable | Build with `make TEXT_MODE=1`, load module | Text commands are available |
| Runtime opt-in | Run `./user/kbmon text` | Text mode is enabled and `dmesg` shows a warning |
| Simple letters | Enable text mode, type `hello` through `scripts/text_demo.sh` | Text view shows `hello` |
| Shift/Caps | Type uppercase letters in text demo | Text view shows uppercase letters |
| Digits/space/enter | Type digits, spaces, and Enter | Text view includes those characters |
| Shift symbols | Type `! @ # () _ + { } : ?` using Shift | Text view shows the expected US-layout symbols |
| Punctuation | Type `- = [ ] ; ' , . / \\` | Text view shows the expected punctuation |
| Backspace | Type a character, press Backspace | Previous character is removed from the demo buffer |
| Clear text | `./user/kbmon clear-text` | Text view becomes empty |
| Disable text | `./user/kbmon disable-text` | Text capture stops |

## TLS Export Tests

Install dependency:

```bash
sudo apt install -y libssl-dev python3 openssl
```

Build:

```bash
make clean
make
```

Start receiver on the server/laptop:

```bash
bash scripts/generate_tls_cert.sh
python3 server/tls_receiver.py --cert server/server.crt --key server/server.key --port 8443
```

Send from Raspberry Pi:

```bash
./user/kbmon_tls <SERVER_IP> 8443 --insecure
```

| Test | Steps | Expected Result |
| --- | --- | --- |
| TLS client builds | `make` | `user/kbmon_tls` is created |
| Receiver starts | Run `server/tls_receiver.py` | Server listens on port 8443 |
| Encrypted stream | Run `./user/kbmon_tls <SERVER_IP> 8443 --insecure` and press keys | Receiver prints JSON lines as keys arrive |
| Verified send | Run `./user/kbmon_tls <SERVER_IP> 8443 --ca-file server/server.crt --server-name kbmonitor-demo` | Receiver prints JSON and client verifies the demo certificate |
| Key log stream schema included | Inspect JSON lines | `schema` is `kbmonitor.keylog.stream.v1` |
| Key events included | Inspect `key_event` lines | `event` contains `seq`, `time_ms`, `code`, and `key` fields |
| Log metadata included | Inspect `stream_start` line | `log.events`, `log.capacity`, and `log.dropped` exist |
| Text excluded | Inspect JSON | No `text`, `text_begin`, or reconstructed content appears |
| Count-limited stream | Run with `--interval 1 --count 3` and press keys | Receiver gets three key events and the client exits |

## User-Space Error Handling

| Test | Steps | Expected Result |
| --- | --- | --- |
| Missing device | Run `./user/kbmon summary` before loading module | Clear message explains module must be loaded |
| Permission issue | Remove device permissions and run app as normal user | Clear permission message is printed |
| Watch mode | `./user/kbmon watch 1 5` | Five samples are printed at one-second intervals |

## Demo Acceptance

The demo is considered ready when this sequence works without manual recovery:

```bash
bash scripts/demo.sh
```

The video should capture:

- successful build
- successful module insertion
- `/dev/kbmonitor` creation
- stats before and after typing
- key analytics and heatmap
- recent event history
- JSON report evidence export
- optional local text demo, if built with `TEXT_MODE=1`
- TLS JSON receive output
- reset behavior
- `dmesg` logs
- clean module removal
