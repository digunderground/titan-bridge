# Test log

Paste console transcripts here. Every line the p1 sketch prints is timestamped
in `[seconds.milliseconds]` form for exactly this reason — the gaps between
lines are the evidence, particularly around standby.

```
Date:               ____________________
Projector firmware: ____________________
Bridge firmware:    titan_bridge_p1
Projector USB port: [ ] 3.0  [ ] 2.0
Adapter:            [ ] none (native CDC)  [ ] CH340  [ ] CP2102  [ ] FTDI  [ ] DB9+MAX3232
```

## Results summary

| Test | Result | Note |
|---|---|---|
| 0 — bench sanity | | |
| 1 — USB enumeration (HID) | | |
| 2 — CDC serial binding | | |
| 3 — command sweep | | |
| 4 — HDMI3 / undocumented params | | |
| 5 — standby behaviour | | |
| 6 — IR receiver | | |

**Verdict:** ______________________________________________

**Channel chosen:** [ ] native CDC   [ ] UART1 + dongle   [ ] both

---

## Transcript

```
(paste here)
```

---

## Undocumented findings

Anything the published table does not have. These are the valuable bits — new
instruction bytes are the cheapest capability this project will ever gain.

| Instruction | Parameter | Observed effect | Frame |
|---|---|---|---|
| | | | |
