# Advanced Feature Backlog

These features are intentionally deferred until the core assignment requirements
are stable.

## Completed: Level 2 Per-Key Analytics And Heatmap

Goal:

- Track frequency for each Linux key code.
- Show top keys and key categories.
- Render a terminal heatmap with per-key counts.

Implemented direction:

- Added `key_counts[KEY_MAX + 1]` in the kernel module.
- Increment the matching counter when `EV_KEY` has `value == 1`.
- Added a `view keys` command.
- Added `./user/kbmon keys`.
- Added `./user/kbmon keys` for human-friendly analytics.
- Added `./user/kbmon heatmap` for per-key counts in keyboard layout form.
- Kept `./user/kbmon raw-keys` for machine-readable driver output.
- Keep key-code analytics as statistics, not typed text.

## Completed: TLS Statistics Export

Goal:

- Send Level 1 and Level 2 keyboard statistics to a remote server using TLS.

Implemented direction:

- Kept TLS in user space using C and OpenSSL.
- Added `user/kbmon_tls.c`.
- Reads `view summary` and `view keys` only.
- Serializes summary/key analytics as JSON.
- Added `server/tls_receiver.py` for demo evidence.
- Added certificate and client helper scripts.
- Never transmit reconstructed typed text.

## Completed: Level 3 Local Text Reconstruction Demo

Goal:

- Demonstrate how simple typed text can be approximated from key events.

Guardrails:

- Compile-time gated with `make TEXT_MODE=1`.
- Runtime opt-in with `mode text`.
- Local-only.
- Clear warning in `dmesg`.
- No network transmission.

Limits:

- Demo-focused US keyboard mapping only.
- Letters, digits, common punctuation, shifted US symbols, space, enter,
  backspace, shift, and caps lock.
- Not a full keyboard layout or input method implementation.

## Optional Web Dashboard

Goal:

- Visualize Level 1 and Level 2 stats in a browser.

Design direction:

- Build only after the terminal heatmap and TLS exporter work.
- Consume the same JSON format used by the TLS exporter.
