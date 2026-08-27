# PAC Control tag database — SMT_Line_Sequencer

These names are the wire contract. The twin matches on them, so a typo here is
a station that silently never fires — check spelling against the station list
below rather than against memory.

## How the twin reads a name

PAC Control permits only letters, digits and underscore in a tag name, so the
slash-separated form a broker-native controller would use (`Line1/PIN_INSERTION/trigger`)
is not available. The twin therefore reads names **from the right**: it strips a
recognised command word off the end, then matches what remains against the
station ids actually registered on the line, longest match first.

Two consequences worth knowing:

- **The prefix is ignored.** `Line1_PIN_CHECK_trigger` and
  `SMT_Line1_PIN_CHECK_trigger` are read identically. Prefix however suits the
  panel.
- **The station id must be the whole tail**, behind a separator. `PIN_CHECK` is
  matched even though `PIN_INSPECTION` and `PIN_INSERTION` also start with
  `PIN_` — the match is on the end of the name, not the start.

Recognised command words: `trigger`, `enable`, `hold`, `reset`, and at line
level `new_material` and `mode`.

## Station ids

Process order. These are the ISA-95 work-unit names the twin publishes under,
so they are fixed by the level, not chosen here.

| # | Station id | Note |
|---|---|---|
| 0 | `LOADER` | |
| 1 | `LASER_MARKING` | |
| 2 | `SOLDER_PASTE` | |
| 3 | `SOLDER_INSP` | inspection |
| 4 | `COMPONENT_PLACER` | |
| 5 | `REFLOW_OVEN` | longest cycle |
| 6 | `AUTO_OPTICALINSP` | inspection |
| 7 | `PCB_CLEANER` | |
| 8 | `HOUSING_ASSEMBLY` | operator bench |
| 9 | `PIN_INSERTION` | |
| 10 | `PIN_INSPECTION` | operator bench, inspection |
| 11 | `ASSEMBLY_ROBOT` | UR5 |
| 12 | `ICT` | inspection |
| 13 | `FLASH_PROGRAMMING` | |
| 14 | `PIN_CHECK` | inspection |
| 15 | `EOL_TEST` | inspection |
| 16 | `PACKAGING` | |

## Tags to create

### Per station — 51 tags, all Integer 32

For each of the 17 station ids above:

| Name | Type | Meaning |
|---|---|---|
| `Line1_<STATION>_trigger` | Integer 32 | Monotonic counter. Any change to a non-zero value starts one cycle. |
| `Line1_<STATION>_enable` | Integer 32 | Level. `0` blocks the station, `1` releases it. |
| `Line1_<STATION>_reset` | Integer 32 | Counter. Any change to a non-zero value clears a fault. |

A counter rather than a toggling bit for the edge-sensitive two: the twin fires
on *change*, so a counter behaves as a pulse and additionally records how many
cycles the strategy believes it commanded. Wrapping is harmless — only the
change is read, never the magnitude.

`hold` is accepted by the twin but unused here. `enable` covers the same ground
for a single controller; `hold` exists so an operator screen can stop a station
without fighting the PLC for the enable bit.

### Line level

| Name | Type | Meaning |
|---|---|---|
| `Line1_new_material` | Integer 32 | Counter. Each change releases one board. |
| `Line1_mode` | String | `"external"` hands sequencing to this strategy; `"local"` returns it to the line. |

### Internal — not published

| Name | Type | Purpose |
|---|---|---|
| `fTaktSeconds` | Float | Line takt. The one number worth changing live in a demo. |
| `tTakt` | Up Timer | Position within the current takt. |
| `nBoardsReleased` | Integer 32 | Diagnostic count. |
| `nIndex` | Integer 32 | Loop variable. |
| `nPermissive` | Integer 32 | E-stop healthy **and** run selected. |
| `nPermissiveLast` | Integer 32 | Change detection, so enables publish only on transition. |
| `nEStopHealthy` | Integer 32 | Mirror of `diEStopOk`. |
| `nRunSelector` | Integer 32 | Mirror of `diRunSelector`. |
| `nResetButton` | Integer 32 | Mirror of `diResetButton`. |
| `nResetButtonLast` | Integer 32 | Edge detection on the reset button. |
| `nTaktRunning` | Integer 32 | Whether the takt timer is running. |
| `nStationFired` | Integer 32 Table (17) | Which stations have fired in this takt. |
| `fStationOffset` | Float Table (17) | Fire time per station, as a fraction of takt. |
| `pTrigger` | Pointer Table (17) | Points at the 17 `_trigger` tags. |
| `pEnable` | Pointer Table (17) | Points at the 17 `_enable` tags. |
| `pReset` | Pointer Table (17) | Points at the 17 `_reset` tags. |

### I/O points

Wire these to real terminals. Everything runs without them — an unwired input
reads `0`, so with none of them present the permissive is false and the line
sits blocked, which is the right failure direction.

| Name | Type | Notes |
|---|---|---|
| `diEStopOk` | Digital input | **Status only.** Reflects a hardwired chain so the twin can display it. Never the thing that stops a machine. |
| `diRunSelector` | Digital input | Run / hold selector or keyswitch. |
| `diResetButton` | Digital input | Momentary. Pulses every station's `_reset`. |
| `doStackGreen` | Digital output | Running. |
| `doStackAmber` | Digital output | Held. |
| `doStackRed` | Digital output | E-stopped. |

The three outputs are worth wiring even for a demo: a physical tower lit from
the panel's own view of the line reads from across a room in a way a screen
does not, and it keeps telling the truth when the network is down.

## Generating the tag list

51 tags is tedious by hand and easy to typo. Paste this into any shell to print
them in creation order:

```bash
for s in LOADER LASER_MARKING SOLDER_PASTE SOLDER_INSP COMPONENT_PLACER \
         REFLOW_OVEN AUTO_OPTICALINSP PCB_CLEANER HOUSING_ASSEMBLY \
         PIN_INSERTION PIN_INSPECTION ASSEMBLY_ROBOT ICT FLASH_PROGRAMMING \
         PIN_CHECK EOL_TEST PACKAGING; do
  for c in trigger enable reset; do echo "Line1_${s}_${c}"; done
done
```

## Driving more than one line

The level runs three. Duplicate the chart per line and change the `Line1_`
prefix — the twin resolves the station from the tail of the name and ignores
the prefix entirely, so nothing else changes. One PLC can sequence all three;
tags naming a station that is not in the loaded level are ignored without
complaint.
