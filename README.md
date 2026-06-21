# CSC1107 Project 2: USB Keyboard Activity Logger Driver

`kbmonitor` is a Raspberry Pi Linux kernel module project for CSC1107 Operating
Systems Project 2. It creates a character device at `/dev/kbmonitor`, observes
USB keyboard activity through the Linux input subsystem, and exposes keyboard
activity statistics to user-space C programs.

The project does **not** replace the real Linux keyboard driver. The normal USB
HID keyboard driver still handles typing; `kbmonitor` passively observes input
events for monitoring, analytics, local demo text reconstruction, and TLS
statistics export.

## Implemented Features

### Core Driver

- Loadable kernel module: `kernel/kbmonitor.c`
- Lab-style character device registration using `alloc_chrdev_region`, `cdev`,
  `class_create`, and `device_create`
- Device node: `/dev/kbmonitor`
- Linux input subsystem handler for keyboard-like USB input devices
- `read()` interface for retrieving statistics
- `write()` interface for commands such as `reset`, `view summary`, and
  `view keys`
- `printk()` / `pr_info()` / `pr_warn()` logging visible through `dmesg`
- Safe module load/unload workflow using `insmod` and `rmmod`

### Level 1: Activity Summary

Level 1 records general keyboard activity:

- total physical key presses
- active keyboard devices
- module uptime
- timestamp of the last keypress
- average and recent keypress rates
- repeat events
- recent-event ring buffer size and dropped entries

Run:

```bash
./user/kbmon summary
```

Example:

```text
driver=kbmonitor
view=summary
total_presses=42
active_keyboards=1
uptime_ms=15320
last_press_ms=15110
presses_per_minute=164
presses_last_10s=8
peak_presses_per_second=3
repeat_events=3
buffered_events=42
buffer_dropped=0
```

### Level 2: Key Analytics And Heatmap

Level 2 records per-key counts and key categories without reconstructing typed
text.

Run human-friendly analytics:

```bash
./user/kbmon keys
```

Example:

```text
Keyboard analytics
Total key presses: 42
Active keyboard devices: 1
Repeat events: 0
Unique keys pressed: 8

Categories:
  Letters:       25
  Digits:        3
  Modifiers:     4
  Navigation:    0
  Function keys: 0
  Control keys:  10
  Other keys:    0

Top keys:
 1. SPACE    count=8
 2. A        count=6

Pressed keys:
  KEY        CODE   COUNT
  A          30     6
  SPACE      57     8
```

Run a terminal heatmap with per-key counts:

```bash
./user/kbmon heatmap
```

Show recent buffered keypress events:

```bash
./user/kbmon events
```

Example:

```text
Recent keypress events (3/64 buffered, dropped=0)
NO    KEY        CODE   SINCE_START  AGE
1     A          30     14022ms      3050ms ago
2     SPACE      57     14610ms      2462ms ago
3     ENTER      28     17042ms      30ms ago
```

For raw machine-readable driver output:

```bash
./user/kbmon raw-keys
```

### Status And Report Export

Show driver/device status:

```bash
./user/kbmon status
```

Export report-friendly JSON evidence:

```bash
./user/kbmon export
```

The JSON export includes Level 1 summary, Level 2 categories, per-key counts,
recent event metadata, and the privacy marker `"exports_text": false`.

### Level 3: Local Text Demo

Level 3 is an optional local-only text reconstruction demo. It is disabled in the
normal build and must be compiled in explicitly.

Supported demo input:

- US-style letters
- digits
- common punctuation
- shifted symbols such as `!`, `@`, `#`, `(`, `)`, `_`, `+`, `{`, `}`, `:`,
  and `?`
- Space
- Enter
- Backspace
- Shift
- Caps Lock

This is not a full keyboard layout or input-method implementation. It is for a
controlled demo only and is never sent by the TLS exporter.

Build with Level 3 enabled:

```bash
make clean
make TEXT_MODE=1
```

Commands:

```bash
./user/kbmon text
./user/kbmon clear-text
./user/kbmon disable-text
```

Safer guided demo:

```bash
bash scripts/text_demo.sh
```

### TLS Statistics Export

The TLS exporter sends only Level 1 and Level 2 statistics. It does not read or
send `view text`.

Implemented files:

- TLS client: `user/kbmon_tls.c`
- TLS receiver: `server/tls_receiver.py`
- Certificate helper: `scripts/generate_tls_cert.sh`
- Client helper: `scripts/tls_client_demo.sh`

The exported JSON includes:

- summary totals
- key categories
- top keys
- per-key counts
- explicit privacy marker: `"exports_text": false`

## Requirements

Recommended target:

- Raspberry Pi 4
- Raspberry Pi OS 64-bit
- USB keyboard
- Matching Raspberry Pi kernel headers
- `gcc`
- `make`
- Bash
- OpenSSL development headers
- Python 3 for the demo TLS receiver

Install dependencies on Raspberry Pi OS:

```bash
sudo apt update
sudo apt install -y raspberrypi-kernel-headers build-essential libssl-dev python3 openssl zip
```

Or use the setup helper:

```bash
bash scripts/setup_pi.sh
```

Check kernel headers:

```bash
uname -r
ls /lib/modules/$(uname -r)/build
```

If the headers path does not exist after installation, reboot the Pi and check
again.

## Build

Normal build:

```bash
make clean
make
```

Build with optional Level 3 text demo:

```bash
make clean
make TEXT_MODE=1
```

Build through the helper script:

```bash
bash scripts/build.sh
```

Expected outputs:

```text
kernel/kbmonitor.ko
user/kbmon
user/kbmon_tls
```

## Load And Unload

Load:

```bash
bash scripts/load.sh
```

Manual load:

```bash
sudo insmod kernel/kbmonitor.ko
sudo chmod 666 /dev/kbmonitor
```

Confirm:

```bash
lsmod | grep kbmonitor
ls -l /dev/kbmonitor
sudo dmesg | tail -n 30
```

Unload:

```bash
bash scripts/unload.sh
```

Manual unload:

```bash
sudo rmmod kbmonitor
```

## Basic Usage

From the project root after loading the module:

```bash
./user/kbmon summary
./user/kbmon keys
./user/kbmon heatmap
./user/kbmon events
./user/kbmon status
./user/kbmon export
./user/kbmon reset
./user/kbmon watch 1 10
```

Manual `/dev/kbmonitor` interaction:

```bash
echo "view summary" > /dev/kbmonitor
cat /dev/kbmonitor

echo "view keys" > /dev/kbmonitor
cat /dev/kbmonitor

echo "view events" > /dev/kbmonitor
cat /dev/kbmonitor

echo "view status" > /dev/kbmonitor
cat /dev/kbmonitor

echo "reset" > /dev/kbmonitor
cat /dev/kbmonitor
```

## Demo Scripts

Core demo:

```bash
bash scripts/demo.sh
```

Expected flow:

1. Build the project.
2. Unload any old `kbmonitor` module.
3. Load the new module.
4. Show initial stats.
5. Wait for keyboard activity.
6. Show updated Level 1 stats.
7. Show Level 2 key analytics.
8. Show heatmap with per-key counts.
9. Show recent event history.
10. Show report evidence JSON export.
11. Demonstrate direct `write()` to `/dev/kbmonitor`.
12. Reset counters.
13. Show recent `dmesg`.
14. Unload cleanly.

Optional Level 3 demo:

```bash
make clean
make TEXT_MODE=1
sudo insmod kernel/kbmonitor.ko
sudo chmod 666 /dev/kbmonitor
bash scripts/text_demo.sh
```

## TLS Demo

The TLS demo uses two terminals or two machines:

- **Receiver machine**: laptop/server that listens for encrypted JSON stats.
- **Sender machine**: Raspberry Pi running `kbmonitor` and `user/kbmon_tls`.

The Raspberry Pi must have the module loaded before sending TLS data:

```bash
cd ~/linux/CSC1107OS/kbmonitor
sudo rmmod kbmonitor 2>/dev/null || true
make clean
make
sudo insmod kernel/kbmonitor.ko
sudo chmod 666 /dev/kbmonitor
./user/kbmon summary
./user/kbmon keys
```

### 1. Start The TLS Receiver

On the receiver machine, generate a self-signed demo certificate:

```bash
bash scripts/generate_tls_cert.sh
```

Start the receiver on port `8443`:

```bash
python3 server/tls_receiver.py --cert server/server.crt --key server/server.key --port 8443
```

Expected receiver message:

```text
[tls-receiver] listening on 0.0.0.0:8443
```

Find the receiver machine's IP address.

Linux:

```bash
ip addr
```

Windows PowerShell:

```powershell
ipconfig
```

Use the receiver's IPv4 address as `<SERVER_IP>`.

### 2. Send Stats From The Raspberry Pi

Send one encrypted stats sample:

```bash
./user/kbmon_tls <SERVER_IP> 8443 --insecure
```

Example:

```bash
./user/kbmon_tls 192.168.1.50 8443 --insecure
```

Or use the helper:

```bash
bash scripts/tls_client_demo.sh <SERVER_IP> 8443
```

Send repeated samples:

```bash
./user/kbmon_tls <SERVER_IP> 8443 --interval 5 --count 10 --insecure
```

Verified self-signed demo mode:

```bash
bash scripts/generate_tls_cert.sh
./user/kbmon_tls <SERVER_IP> 8443 --ca-file server/server.crt --server-name kbmonitor-demo
```

The helper also supports verified mode:

```bash
bash scripts/tls_client_demo.sh <SERVER_IP> 8443 --verify
```

Expected sender output:

```text
sent TLS stats sample 1 to 192.168.1.50:8443
```

Expected receiver output is JSON similar to:

```json
{
  "schema": "kbmonitor.stats.v1",
  "source": "kbmonitor",
  "privacy": {
    "exports_text": false
  },
  "summary": {
    "total_presses": 42,
    "active_keyboards": 1,
    "repeat_events": 0
  },
  "analytics": {
    "categories": {
      "letters": 20,
      "digits": 3
    },
    "top_keys": [
      {
        "key": "SPACE",
        "count": 8
      }
    ]
  }
}
```

`--insecure` is intended only for quick self-signed lab demos. It still uses an
encrypted TLS connection, but disables peer certificate verification. For the
stronger demo path, use `--ca-file server/server.crt --server-name
kbmonitor-demo`.

Important privacy rule: `kbmon_tls` exports only Level 1 summary and Level 2 key
analytics. It does not read `view text`, does not send the Level 3 text buffer,
and the JSON includes `"exports_text": false`.

If the connection fails:

- Confirm both machines are on the same network.
- Confirm `<SERVER_IP>` is the receiver IP, not the Pi IP.
- Confirm the receiver script is still running.
- Allow inbound TCP port `8443` through the receiver firewall if needed.
- Confirm `/dev/kbmonitor` exists on the Pi.

## Project Structure

```text
kernel/
  kbmonitor.c          Kernel module and character device driver
  Makefile            Kernel module kbuild file

user/
  kbmon.c             Local user-space monitor app
  kbmon_tls.c         User-space OpenSSL TLS exporter
  Makefile            User-space build rules

server/
  tls_receiver.py     Minimal TLS JSON receiver

scripts/
  build.sh            Build helper
  load.sh             Load module and prepare /dev/kbmonitor
  unload.sh           Remove module
  demo.sh             Main coursework demo flow
  text_demo.sh        Optional Level 3 demo
  setup_pi.sh         Raspberry Pi dependency/setup helper
  validate_all.sh     End-to-end validation/evidence helper
  generate_tls_cert.sh
  tls_client_demo.sh
  package_source.sh

docs/
  test_plan.md
  report_outline.md
  presentation_outline.md
  backlog.md
  submission_checklist.md
```

## Design Notes

The module attaches to keyboard-like input devices through the Linux input
subsystem. It filters for USB devices by default and requires common keyboard
capabilities such as `KEY_A` and `KEY_ENTER`.

Event policy:

- `EV_KEY`, `value == 1`: physical key press
- `EV_KEY`, `value == 2`: repeat event
- `EV_KEY`, `value == 0`: key release, ignored for press count

Safety and privacy:

- The module does not replace the normal keyboard driver.
- Level 1 and Level 2 do not reconstruct typed text.
- Level 3 is compile-time gated and runtime opt-in.
- TLS export reads only `view summary` and `view keys`.
- TLS export never sends reconstructed text.

## Troubleshooting

If `/dev/kbmonitor` does not exist:

```bash
lsmod | grep kbmonitor
sudo dmesg | tail -n 50
```

If the module does not build:

```bash
uname -r
ls /lib/modules/$(uname -r)/build
```

If stats stay at zero:

- Confirm a USB keyboard is connected.
- Run `sudo dmesg | grep kbmonitor`.
- For development only, load with:

```bash
sudo insmod kernel/kbmonitor.ko allow_non_usb=1
```

If TLS build fails:

```bash
sudo apt install -y libssl-dev
make clean
make
```

## Source Packaging

Create a source-only ZIP:

```bash
bash scripts/package_source.sh Px_Groupxx-codes.zip
```

The packaging script excludes generated files such as:

- `*.ko`
- `*.o`
- `*.cmd`
- `*.mod`
- `Module.symvers`
- `modules.order`
- `user/kbmon`
- `user/kbmon_tls`
- generated TLS certificates
- `.git`
- `teammate_version`
