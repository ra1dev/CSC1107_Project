# Report Outline

## 1. Introduction

- Project topic: USB Keyboard Activity Logger Driver.
- Goal: create a Linux character device driver that records keyboard activity.
- Target: Raspberry Pi 4, Raspberry Pi OS, USB keyboard.

## 2. Requirements Summary

- Linux kernel module.
- Character device under `/dev`.
- User-space C program using `read()` and `write()`.
- USB keyboard interaction.
- `printk()` and `dmesg` logging.
- Makefile build and Bash automation.
- Safe `insmod` and `rmmod`.
- Testing procedure and technical documentation.

## 3. Background Research

- USB HID keyboards.
- Linux input subsystem.
- `EV_KEY` event values:
  - `1`: press
  - `0`: release
  - `2`: repeat
- Kernel space vs user space.
- Character device drivers and `file_operations`.

## 4. Architecture

Recommended diagram:

```text
USB Keyboard
  -> Linux USB HID/input subsystem
  -> kbmonitor kernel module
  -> /dev/kbmonitor
  -> user-space C app
```

Explain that the module observes events and does not replace the real keyboard
driver.

## 5. Kernel Module Design

- Module lifecycle: `module_init`, `module_exit`.
- Character device registration:
  - `alloc_chrdev_region`
  - `cdev_init`
  - `cdev_add`
  - `class_create`
  - `device_create`
- Input handler registration.
- Keyboard filtering logic.
- Event counting policy:
  - count press events
  - ignore release events
  - track repeats separately
- Timestamp ring buffer.
- Synchronization using spinlocks.
- Cleanup path on errors and unload.

## 6. User-Space Program Design

- Opens `/dev/kbmonitor`.
- Sends commands using `write()`.
- Reads stats using `read()`.
- Supports summary, reset, and watch modes.
- Handles missing device and permission errors.

## 7. Testing And Results

Use the table from `docs/test_plan.md`.

Include screenshots or copied terminal output for:

- build success
- module load
- `/dev/kbmonitor`
- stats before typing
- stats after typing
- reset
- `dmesg`
- unload

## 8. Strengths And Limitations

Strengths:

- Meets the core driver requirements.
- Uses real USB keyboard activity.
- Keeps normal keyboard behavior intact.
- Uses a clear `/dev` interface.
- Includes Level 2 per-key analytics and terminal heatmap with per-key counts.
- Includes compile-time gated, local-only Level 3 text demo.
- Includes user-space TLS export for Level 1/2 statistics.
- Avoids reconstructing typed text.

Limitations:

- TLS demo uses a self-signed certificate unless a CA file is configured.
- Level 3 text mode is limited to controlled US-style demo input, including
  common shifted symbols but not full international layouts or input methods.
- Different kernels may require matching Raspberry Pi kernel headers.

## 9. Advanced Feature Backlog

- Optional dashboard.

## 10. GenAI Reflection

Discuss:

- how GenAI helped compare implementation approaches
- why the team chose passive input-subsystem monitoring instead of replacing the keyboard driver
- what code was reviewed and adapted by team members
- limitations of GenAI suggestions
- what was learned about kernel development and debugging

## 11. Individual Contributions

List each member's work:

- kernel character device
- input event monitoring
- user-space app
- scripts/testing
- documentation/report
- presentation/demo
