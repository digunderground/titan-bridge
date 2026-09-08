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
| **On** | `wake` — `0x09` + ASCII `"wakeup"` | **Idempotent.** Sending it to an awake projector does nothing (measured 2026-09-06). |
| **Off** | `power` key — `0x07 00` | **A toggle.** Sent to an off projector it turns it **on**. Raises a shutdown dialog that **must be confirmed** with `OK` (`0x07 0D`) — it does *not* time out into a shutdown. |

There is **no discrete off**. `0x09` is string-keyed, so `sleep`, `standby`,
`poweroff` and `shutdown` were each tried: all four ACK with the payload echoed
verbatim, identical to the working `wakeup`, and none does anything.

## THE RULE

**Power on and power off send exactly ONE command each. Nothing else. Ever.**

| action | frame | and then |
|---|---|---|
| **On** | `wake` — `2A 2A 07 09 77 61 6B 65 75 70 9D` | nothing |
| **Off** | `power` — `2A 2A 02 07 00 09` | **nothing** |

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
3. **Off is the power key AND a confirmed dialog.** The countdown does **not**
   shut the projector down on its own — measured 2026-09-07: an off with no `OK`
   ran the full countdown and the projector stayed on, while the byte-identical
   sequence followed by an `OK` worked. Everything elaborate that was tried here
   (countdown-only, a `back` nudge, an OSD prelude) was working around a missing
   confirm.

   The confirm delay is **2.5 s**, measured from an off that actually worked.
   The original 900 ms was a guess sitting on the edge of the dialog appearing,
   which is the best explanation for why power-off was intermittent rather than
   simply broken. The dialog lives ~15 s; there is no reason to be quick.

4. **Off waits for the power key's own ACK.** The power key is occasionally not
   acknowledged at all, while an `OK` on the same wire is acknowledged in ~20 ms
   — the projector dropping that frame, not a sick link. An unacknowledged power
   key raised no dialog, so resending it cannot double-press. Resend up to 3
   times; if never acknowledged, fail loudly and do not send `OK`.
5. **The channel is explicit, never inferred.** Power goes over the configured
   key channel. Deducing it from link liveness routed power to an unwired HID
   channel and recorded success (see R-0002).
6. **Never retransmit a key simulation** (`0x07`). A repeat is a second key
   press, not a duplicate request — for the power key it would cancel the
   countdown the first press raised. Only absolute-state commands are retried.
7. **A command that was not delivered must never be recorded as done.** Silent
   failure recorded as success is what desyncs belief, and a desynced belief is
   what makes the guard suppress the next real attempt.

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
