# Submission Checklist

## Required Files

- `Px_Groupxx-report-CSC1107.docx`
- `Px_Groupxx-codes.zip`
- `Px_Groupxx-CSC1107.mp4`
- `Px_Groupxx-demo.mp4`

Replace `Px_Groupxx` with the correct lab session and group number.

## Source ZIP

Create source ZIP:

```bash
bash scripts/package_source.sh Px_Groupxx-codes.zip
```

Do not include generated files:

- `*.ko`
- `*.o`
- `*.cmd`
- `*.mod`
- `*.mod.c`
- `Module.symvers`
- `modules.order`
- `user/kbmon`
- `user/kbmon_tls`
- `server/*.crt`
- `server/*.key`
- `.git`
- `teammate_version`

## Evidence To Capture

- `make` successful build.
- `make TEXT_MODE=1` successful build, if showing Level 3.
- `sudo insmod kernel/kbmonitor.ko`.
- `/dev/kbmonitor` exists.
- `./user/kbmon summary`.
- `./user/kbmon keys`.
- `./user/kbmon heatmap`.
- `./user/kbmon text`, if Level 3 is shown.
- TLS receiver output from `server/tls_receiver.py`.
- `sudo dmesg | tail -n 40`.
- `sudo rmmod kbmonitor`.

