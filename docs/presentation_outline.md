# Presentation And Demo Outline

Target length: 8-12 minutes for the group presentation video, plus a short demo
video as required by the assignment.

## Suggested 8-12 Minute Presentation Structure

1. Project introduction
   - Topic: USB Keyboard Activity Logger Driver.
   - Target: Raspberry Pi 4 with USB keyboard.
   - Goal: collect keyboard activity statistics through a Linux character driver.

2. System architecture
   - USB keyboard is handled by Linux USB HID/input subsystem.
   - `kbmonitor` observes events; it does not replace the real keyboard driver.
   - `/dev/kbmonitor` exposes stats to user space.

3. Kernel module design
   - Loadable kernel module lifecycle.
   - Lab-style character device registration with `cdev`.
   - `read()` returns stats.
   - `write()` accepts commands such as `reset` and `view summary`.

4. Keyboard event monitoring
   - Input subsystem emits `EV_KEY`.
   - `value == 1` means key press.
   - `value == 0` means key release.
   - `value == 2` means repeat.
   - Core version counts presses and tracks repeats separately.

5. Synchronization and safety
   - Spinlock protects shared counters and event buffer.
   - Event callback stays small and non-blocking.
   - Cleanup removes input handler and character device safely.

6. User-space program and scripts
   - `kbmon summary`
   - `kbmon keys`
   - `kbmon heatmap`
   - `kbmon reset`
   - `kbmon watch`
   - Build/load/demo/unload scripts automate the workflow.

7. Testing results
   - Show build success.
   - Show `/dev/kbmonitor`.
   - Show stats before and after typing.
   - Show Level 2 key analytics and heatmap.
   - Show reset behavior.
   - Show `dmesg`.
   - Show clean unload.

8. Strengths, limitations, and next steps
   - Strengths: real kernel module, real USB keyboard activity, clear `/dev` interface, Level 2 per-key heatmap, local-only text demo, TLS key-name log export.
   - Limitations: text demo is intentionally limited; TLS demo uses self-signed certificate unless configured otherwise.
   - Next steps: optional dashboard or further polish.

9. GenAI reflection and member contributions
   - Each member explains their portion.
   - Discuss how GenAI helped with research, design alternatives, and debugging.

## Short Demo Video Flow

Run:

```bash
bash scripts/demo.sh
```

Capture these moments:

- Build starts and succeeds.
- Module loads.
- `/dev/kbmonitor` is ready.
- Initial stats are shown.
- Keys are pressed.
- Updated stats show increased `total_presses`.
- Key analytics and heatmap are shown.
- Optional text demo is shown only if built with `TEXT_MODE=1`.
- TLS receiver shows encrypted JSON key events.
- Reset clears the counters.
- `dmesg` shows module and event logs.
- Module unloads cleanly.
