# Menu mapping worksheet

The serial set covers input, picture mode, brightness, blanking, high refresh,
volume, mute, autofocus and standby wake. It does **not** cover 3D mode, lens
memory, iris level, keystone or lens shift.

Instruction `0x07` is the way in. It exposes Settings, Home, Back, OK and all
four directions as simulated key presses, so the bridge can drive the OSD
blind.

## The rule: anchor, then count

Never assume where the menu is. Every macro opens by forcing a known state:

```
s:back*3          unwind whatever is on screen
s:setting         enter at a known root
s:down*n; s:ok    counted path from your photographs
...
s:back*4          exit cleanly
```

The `anchor` token in the macro language expands to exactly that opening, with
`MACRO_ANCHOR_MS` between steps.

Without the anchor, one desynced press sends the rest of the sequence
somewhere random — and because the projector reports nothing, you will not find
out until you look at the screen. With it, macros are repeatable.

## Timing

Start at **150–300 ms** between steps. The OSD animates, and commands sent
during a transition get eaten.

Tune per macro. Find the fastest reliable delay rather than using one global
value — a macro that walks eight menu items at 400 ms takes three seconds and
feels broken; the same macro at 180 ms feels instant. `d<ms>` overrides the
default for the following step.

## The fragility, stated plainly

These macros are positional. A projector firmware update that reorders a menu
breaks every one of them. Budget an evening to re-map after major OTAs, and
keep the menu photographs versioned alongside the firmware number.

That is also why the macro engine lives on the ESP32 and not in the SofaBaton's
activity editor: you need per-step timing control and the ability to re-map in
one place, which a universal remote cannot express.

---

## Worksheet

One block per feature. Fill it in once, carefully, and the macros stop being
guesswork. `logs/menu-map.csv` is the same thing in a form you can sort.

```
Feature:            ________________________________
Projector firmware: ________________________________
Date mapped:        ________________________________

Entry anchor:       [ ] anchor (back×3, setting)
                    [ ] home
                    [ ] other: ______________________

Keypress path:      ______________________________________________
                    ______________________________________________

Observed end state: ______________________________________________

Minimum reliable delay: ________ ms   (tested ____ times, ____ failures)

Exit path:          ______________________________________________

Macro script:
  ____________________________________________________________
  ____________________________________________________________

Saved as:           macdef ______________
Verified:           [ ] ran 5 times from cold  [ ] ran 5 times mid-menu
```

---

## Suggested first four

In rough order of how much they are worth.

1. **3D mode on / off** — the one with no serial equivalent at all.
2. **Lens memory recall** — if your firmware has it yet; XGIMI have promised it
   in a future OTA, possibly with serial commands attached, which would make
   this block obsolete in the best way.
3. **Iris level** — the difference between a good and a great image on a scope
   screen.
4. **Keystone / lens shift reset** — the thing you want after someone knocks
   the projector.

Two templates ship in the firmware, `tpl3d` and `tpllens`, with the counted
path left as `s:down*0` so they cannot accidentally do something. Replace the
path, then save with `macdef` so the definition survives a reflash.

```
macdef 3don anchor; d500; s:down*4; s:ok; d400; s:down*2; s:ok; d300; s:back*3
```

User macros live in NVS, not in the sketch. Reflashing the firmware does not
lose them; `forget` does not either. Only an `erase_flash` does.

---

## Probing for undocumented commands

Before mapping a menu path, check whether a serial command already exists for
it. Cheaper than any macro, and immune to menu reordering.

```
sweep 03 00 20      picture modes: the doc assigns 7 of 33 parameters
sweep 01 00 08      sources: the doc lists 3 on a 4-input projector
sweep 0E 00 05      high refresh: 3 documented, checksum implies a 4th value
```

Each step sends a frame and waits 1200 ms. Watch the screen and note which
parameters do something. Instruction `0x06` is refused — it is factory reset.

New instruction bytes are the cheapest capability you will ever gain. Re-run
these sweeps after every projector firmware update.
