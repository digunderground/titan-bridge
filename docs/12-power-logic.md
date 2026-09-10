# Power logic

Power broke repeatedly over two days. Two distinct causes: interactions
between inputs that mostly could not affect the outcome, and — for power-off —
four rewrites chasing a step missing from a procedure nobody had written down. This document is the
single place the rules are written down, and the standard any change to power is
held to.

## What is actually knowable

**Nothing reports the projector's power state.** Measured, repeatedly:

- The serial daemon answers the temperature probe **identically in standby**, so
  a reply means the link is alive, not that the projector is on
  (`TEMP_PROBE_INDICATES_POWER 0`).
- Instruction `0x13` params `01`–`20` all ACK and return no data. There is no
  readback for input, picture mode, volume or power.
- Changing anything on the physical remote produces **no serial traffic at all**.

So power state is **assumed, and user-correctable**. That is not a shortcut; it
is the only design the protocol permits.

## The two commands

| | command | behaviour |
|---|---|---|
| **Power key** | `0x07 00` | **A toggle.** The only thing that changes power state. Sent to an off projector it turns it **on**; sent to an on projector it starts a shutdown. |
| `wake` | `0x09` + ASCII `"wakeup"` | Discrete on, and **idempotent** — sending it to an awake projector does nothing (measured 2026-09-06). No longer used by the power path, but still available by name. |

There is **no discrete off**. `0x09` is string-keyed, so `sleep`, `standby`,
`poweroff` and `shutdown` were each tried: all four ACK with the payload echoed
verbatim, identical to the working `wakeup`, and none does anything.

## THE RULE

**Power on and power off send ONLY power keys. Nothing else. Ever.**

On sends one. Off sends two, 2 s apart — the one agreed exception, and the
reason is below. No OK, no nudge, no prelude, no probe, no third press.

| action | frames | and then |
|---|---|---|
| **On** | `power` — `2A 2A 02 07 00 09` ×1 | nothing |
| **Off** | `power` — `2A 2A 02 07 00 09` ×2, **2 s apart** | nothing |

**VERIFIED WORKING 2026-09-07 and not to be changed.** Captured cycle:

```
[2956.376] TX 07 00              on, one frame
[3086.243] TX 07 00  key 1 of 2  off
[3088.246] TX 07 00  key 2 of 2  +2.003 s
```

### Why off sends it twice

**The first power key after a power-on is swallowed by the projector.** Proven
with two byte-identical, acknowledged frames 91 s apart:

```
[178.905] TX 07 00 -> ACK 19 ms -> nothing happened
[270.291] TX 07 00 -> ACK 32 ms -> countdown, projector off
```

The original firmware sent it twice *by accident* — its verify-then-retry loop
tested `probeAnswered()`, which is always true here, so it always fired. That is
why power off "worked perfectly" before, and removing that retry on 2026-09-05
is what broke it. Every failure since was a single press; every success involved
a second one, including manual ones where the operator simply pressed again when
nothing happened.

This is the **one agreed exception** to one-command-per-power-action. It is
still only power keys.

Anything sent after the power key turns the projector back on — the `OK` lands
on the next screen, a second power key is simply a second toggle. Observed
directly: *"it turns off, then right back on again."*

If a power action needs to do more — switch an input, set a picture mode, wait
for a source — **that belongs in the hub's automation or in a macro**, not in
the power path. Macros are the only thing in this firmware that may send more
than one command for a single action.

Every one of these was tried in the power path and every one made it worse:

| tried | result |
|---|---|
| verify-then-retry (`probeAnswered()`) | always true here → double toggle → projector comes back on |
| `OK` 900 ms after the power key | lands after the shutdown and wakes it |
| `OK` at a "measured" 2.5 s | same |
| two full `power`+`OK` cycles | turns it off, then straight back on |
| `back` keypress before power | no effect; failed 10/10 |
| `setting`+`back`+`back` OSD prelude | worked once, failed once on identical ACKed frames |
| waiting out the 15 s countdown | pointless; the countdown does not decide anything |
| power key instead of `wake` for on | a confounded test — the hub also sends `InputHDMI1` |
| unconditional belief guard on off | suppressed legitimate power-offs silently, before any frame went out |

Do not add to the power path. Measure against this baseline first, and change
one thing at a time.

## Supporting rules

These follow from the asymmetry above, and they are the whole design:

1. **On is never guarded.** `wake` is idempotent, so refusing it on a stale
   belief can only produce "PowerOn does nothing" — which is a real failure,
   where sending it anyway costs nothing.
2. **Off is always guarded.** It is a toggle, so sending it on a stale belief
   turns the projector **on**. Worst case for guarding is "nothing happened";
   worst case for not guarding is "it switched on and stayed on".
3. **The sequences are editable, and what is stored is what is sent.**
   Settings → Power commands holds both sequences as literal frames. No checksum
   correction, no added confirm key, no retry — so a change can be tested
   without a reflash, and the app shows exactly what goes on the wire. Defaults
   are the table above; "Reset to default" restores them exactly.
4. **The channel is explicit, never inferred.** Power goes over the configured
   key channel. Deducing it from link liveness routed power to an unwired HID
   channel and recorded success (see R-0002).
5. **Never retransmit a key simulation** (`0x07`). A repeat is a second key
   press, not a duplicate request — for the power key it would cancel the
   countdown the first press raised. Only absolute-state commands are retried.
6. **A command that was not delivered must never be recorded as done.** Silent
   failure recorded as success is what desyncs belief, and a desynced belief is
   what makes the guard suppress the next real attempt.

## Not a power bug: the ~10 minute self power-on

For three days the projector powered itself on ~10 minutes after every shutdown,
and it was assumed to be something in this path. It was not. The cause was the
projector's own setting — **Settings → General → Advanced Settings → Power
On/Off Settings → "Auto Power Off When Inactive"** — which on firmware v1.2.92
powers the unit **ON** that many minutes after it is switched **OFF**. It had
been set to 10 minutes as a stop-gap *during* the investigation. See R-0004.

Nine theories were built on that correlation and every one was wrong. Nothing in
this firmware ever caused it.

## Dead inputs — to be removed

These still branch in the power path and cannot affect the outcome. They are
listed because *every* power regression so far came from an interaction, and
unreachable state that still branches is where interactions hide.

| input | why it is dead |
|---|---|
| `pwr` (`PWR_UNKNOWN`/`AWAKE`/`ASLEEP`) | Every `setPower()` is inside `#if TEMP_PROBE_INDICATES_POWER` (0) or gated on `!everFrame && titanUsbPowerKnown()`, false once frames arrive. It never leaves `PWR_UNKNOWN` — yet **six conditions test it**. |
| `powerObey` (obey/assume) | Only reachable as `!onIsIdempotent && !powerObey && …`, and serial `wake` is always idempotent. A user-facing setting that changes nothing. |
| `TEMP_PROBE_INDICATES_POWER` branches | Flag is 0 and the finding behind it is settled. |
| `USB_DEAD_IN_STANDBY` branch | Guards a `pwr == PWR_ASLEEP` test that can never be true. |

## The standard for changing power

- **Do not replace a sequence that is reported working without measuring against
  it.** This has now cost two regressions: the countdown-only rewrite and the
  `back` nudge, both theories that replaced a working mechanism and made it
  worse.
- **Change one thing at a time.** The `back` nudge was added in the same build
  that removed `OK`, so neither could be evaluated.
- **Test a full on → off cycle before tagging a release.** 0.8.2 was tagged
  after verifying only an off, and shipped with power-on broken.
- A correlation over a handful of samples is a hypothesis, not a fix.
- **Write down the operator's working procedure in full before changing it.**
  Power-off was described as "sync, then press power". The actual working
  procedure had a third step — confirming the dialog — which was reflexive
  enough not to be mentioned and never asked about. Every build shipped after
  that omitted the confirm, and four separate theories were built to explain the
  resulting failures.
- **Do not accept a stated observation as a measurement.** "I let it time out
  and the projector is OFF" was taken as evidence the countdown self-completes.
  It does not. That single unverified data point drove two rewrites.
