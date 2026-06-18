# kbmonitor Test Plan

## Build Tests

| Test | Command | Expected Result |
| --- | --- | --- |
| Build all | `make` | `kernel/kbmonitor.ko` and `user/kbmon` are created |
| Clean build outputs | `make clean` | Kernel and user build artifacts are removed |
| Rebuild after clean | `make clean && make` | Build still succeeds |

## Module Lifecycle Tests

| Test | Command | Expected Result |
| --- | --- | --- |
| Load module | `bash scripts/load.sh` | Module loads and `/dev/kbmonitor` exists |
| Confirm module | `lsmod \| grep kbmonitor` | `kbmonitor` is listed |
| Check logs | `sudo dmesg \| tail` | Load and device creation logs appear |
| Unload module | `bash scripts/unload.sh` | Module unloads cleanly |
| Confirm removal | `lsmod \| grep kbmonitor` | No `kbmonitor` entry remains |

## Character Device Tests

| Test | Command | Expected Result |
| --- | --- | --- |
| Read summary | `./user/kbmon summary` | Valid key-value stats are printed |
| Manual read | `cat /dev/kbmonitor` | Summary stats are printed |
| Select view | `echo "view summary" > /dev/kbmonitor` | Command succeeds |
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

## Level 2 Analytics Tests

| Test | Steps | Expected Result |
| --- | --- | --- |
| Select key view | `./user/kbmon keys` | Output shows `view=keys` |
| Per-key count | Press `A` three times, run `./user/kbmon keys` | Output contains `key_30=3` or higher |
| Category count | Press letters and digits, run `./user/kbmon keys` | `letters` and `digits` increase correctly |
| Top keys | Press one key repeatedly, run `./user/kbmon keys` | Top key code matches the repeated key |
| Friendly key view | `./user/kbmon keys` | Output uses key labels, not raw Linux key-code fields |
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
| Encrypted send | Run `./user/kbmon_tls <SERVER_IP> 8443 --insecure` | Receiver prints JSON payload |
| Level 1 included | Inspect JSON | `summary.total_presses` and related fields exist |
| Level 2 included | Inspect JSON | `analytics.categories`, `top_keys`, and `per_key` exist |
| Text excluded | Inspect JSON | No `text`, `text_begin`, or reconstructed content appears |
| Repeat send | Run with `--interval 5 --count 3` | Receiver gets three samples |

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
- optional local text demo, if built with `TEXT_MODE=1`
- TLS JSON receive output
- reset behavior
- `dmesg` logs
- clean module removal
