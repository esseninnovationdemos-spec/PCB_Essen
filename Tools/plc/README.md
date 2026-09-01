# PLC control of the factory twin

Making an Opto 22 groov EPIC decide when the simulated stations cycle, over the
Sparkplug B bus the demo already runs on.

## The one architectural decision

**The twin follows the PLC. The PLC does not command the twin.**

That is backwards from what Sparkplug normally does, and it is deliberate. A
groov EPIC is a Sparkplug *edge node*: it publishes its own tags, but the spec
reserves commands (`DCMD`) for a primary application, so an edge node has no way
to command a peer. The alternatives were to put Ignition in the control path as
a translator — one more hop, and a line that stops when Ignition does — or to
have the twin subscribe to the PLC's data stream and act on it. The second is
one broker hop and has no third party in it, so that is what this does.

The canonical direction still works: a real primary application (Ignition) can
write the same commands over `DCMD` and the twin honours them. Both paths land
in the same code.

```
  groov EPIC  ──NDATA──▶   EMQX   ──▶  Unreal twin      (following: the PLC path)
  Ignition    ──DCMD───▶   EMQX   ──▶  Unreal twin      (commanding: the SCADA path)
```

## What is here

| File | What it is |
|---|---|
| `PAC_Control/SMT_Line_Sequencer.osc` | The strategy. OptoScript for the sequencer chart — takt, station offsets, enables, fault reset, stack light. |
| `PAC_Control/tag_database.md` | The tags to create, and how the twin reads their names. The names are the wire contract. |
| `groov_manage_setup.md` | Configuring the device to publish. **Holds the IP blanks.** |
| `plc_stand_in.py` | A software PLC speaking the same protocol, for building and testing without hardware. |
| `watch_line.py` | Prints what each station is doing. The commissioning tool. |
| `sparkplug_lite.py` | Just enough MQTT and Sparkplug for the two scripts above. No dependencies. |

## Try it now, with no PLC

Two terminals, with EMQX up and the level running:

```bash
python Tools/plc/plc_stand_in.py --takt 5 --line Line1
```

```bash
python Tools/plc/watch_line.py --line Line1
```

Enable external control first, either in `Config/DefaultGame.ini` or for one run:

```
-ini:Game:[/Script/FactorySim.FactoryTwinSettings]:bExternalControlEnabled=True
```

Watch a second line at the same time (`--line Line2`) and it sits `Idle` and
`Blocked` with zero cycles, because the stand-in addresses only Line1. That
contrast is the quickest proof the whole path works: same broker, same sim, and
the difference is entirely which tags the controller publishes.

## How a tag name is read

PAC Control permits only letters, digits and underscore in a tag name, so the
slash form is unavailable and `Line1_PIN_INSERTION_trigger` is what actually
arrives. Splitting on `_` cannot work — the station ids contain underscores
themselves — so the twin reads the name **from the right**:

1. Strip a recognised command word off the end: `trigger`, `enable`, `hold`,
   `reset`, or at line level `new_material`, `mode`.
2. Match what remains against the station ids registered on this line, longest
   first, so `PIN_CHECK` is not shadowed by a shorter id ending the same way.
3. Require what is *still* left to name that station's edge node.

Step 3 is not optional: this plant runs three lines with identical station
names, so `LOADER` alone matches three machines, and without the `Line1` prefix
a tag would be as likely to cycle Line 3.

## Command semantics

| Command | Read as | Notes |
|---|---|---|
| `trigger` | edge | Any change to a non-zero value starts one cycle. A counter and a boolean pulse both work. One trigger buys exactly one cycle. |
| `enable` | level | `0` blocks the station, `1` releases it. A latch. |
| `hold` | level | Like `enable`, but never interrupts a cycle in progress. |
| `reset` | edge | Clears a fault. |
| `new_material` | edge (from PLC) / level (from DCMD) | Releases one board. |
| `mode` | level | `"external"` or `"local"`. |

Edges from a followed PLC, levels from `DCMD` — because a PLC republishes its
whole tag set every scan, where a `DCMD` is a discrete instruction and two
identical writes mean two triggers.

## What the twin publishes back

Added to every station's birth certificate and every DDATA:

| Metric | Meaning |
|---|---|
| `ready` | A trigger arriving now would start a cycle. |
| `busy` | A cycle is in progress. |
| `station_enabled` | Enabled and not held. |
| `control_mode` | `local` or `external`. |

These carry no Sparkplug alias, so they travel by name only and cannot disturb
the pinned alias map the ClickHouse bridge depends on.

## The watchdog

External control means a controller that goes quiet stops the line — that is the
point of an interlock, and also a good way to lose a demo to a nudged cable. So
`PlcTimeoutSeconds` (default 10) bounds the silence, and
`bFallBackToLocalOnPlcTimeout` (default on) hands sequencing back to the line
rather than leaving it stopped. Turn the fallback off when the demonstration is
the interlock itself.

An `NDEATH` from the PLC is treated as an announced silence and trips the
watchdog on its next tick, rather than waiting out a full timeout for news that
has already arrived.

## Not done

- **Closing the loop.** The strategy is open loop: it owns takt and fires
  stations on offsets, without waiting for the twin to report a cycle complete.
  Reading Sparkplug back into strategy variables needs the groov MQTT client's
  subscribe side and a decode step. Until that exists the offsets stand in for
  the handshake, and `ready` / `busy` are published but nothing on the PLC reads
  them.
- **Real I/O.** `diEStopOk`, `diRunSelector`, `diResetButton` and the three
  stack-light outputs are referenced by the strategy and unwired. Everything
  runs without them; an unwired input reads `0`, so the permissive is false and
  the line sits blocked, which is the right direction to fail in.
- **The device itself.** Not on the network as of 2026-08-27 — see
  `groov_manage_setup.md` for the two addresses that need filling in.

## Timing, honestly

This is supervisory sequencing, not real-time control. A broker round trip is
tens of milliseconds with no upper bound, and the twin ticks at about 25 ms.
That is entirely adequate for "start the next station" and completely unfit for
interpolated motion or anything safety-rated.

Nothing safety-related routes through any of this. `diEStopOk` is read so the
twin can *display* an e-stop; the chain that actually stops a machine is
hardwired and stays that way.
