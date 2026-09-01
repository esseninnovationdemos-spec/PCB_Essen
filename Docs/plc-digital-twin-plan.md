# PLC digital twin — plan and current state

Making a physical Opto 22 groov EPIC decide when the simulated stations cycle,
over the Sparkplug B bus the demo already runs on.

**This is unfinished, and the reason is external:** the device is not on the
network. Everything that can be built and proven without it has been, against a
software stand-in. What remains is commissioning against real hardware, plus two
pieces of engineering that only make sense once it is there.

The architecture, the wire contract and the tag-name grammar are settled and
documented in [`Tools/plc/README.md`](../Tools/plc/README.md). This document is
the plan: what is done, what is blocked, and the order to do the rest in.

---

## 1. Where it actually stands

### Built and verified against a stand-in

| Piece | Where | Proven by |
|---|---|---|
| Observe a foreign edge node's `NDATA` | `SparkplugEdgeNode` — `ObserveEdgeNode`, `OnForeignNodeData`, re-subscribed per session | Stand-in publishes, twin cycles |
| Tag name → station resolution | `FactoryLineSubsystem::ResolveCommandTarget` | Differential test, below |
| Control gate on the machine | `FactoryMachineComponent` — `CanStartCycle`, `Blocked` state | A disabled station refuses triggers and does not emit a phantom completion |
| Edge vs level command semantics | `ApplyStationCommand(..., bEdgeSensitive, bSeedOnly)` | A republished tag set does not re-trigger |
| Feedback metrics | `ready`, `busy`, `station_enabled`, `control_mode` on every birth and `DDATA` | Alias 0, so the pinned alias map the ClickHouse bridge depends on is untouched |
| Silence watchdog | `PlcTimeoutSeconds`, `bFallBackToLocalOnPlcTimeout` | Stopping the stand-in hands sequencing back |
| `NDEATH` handling | Treated as announced silence, trips on next tick | Does not wait out a full timeout for news already arrived |
| The PAC Control strategy | `Tools/plc/PAC_Control/SMT_Line_Sequencer.osc` | Written, not yet loaded on a device |

**The differential test is the one that matters.** Run the stand-in against
`Line1` and watch `Line2` at the same time: Line 1 cycles, Line 2 sits `Idle`
and `Blocked` with zero cycles. Same broker, same sim, same station names — the
only difference is which tags the controller publishes. That proves the whole
path end to end *and* proves the resolution is line-qualified, which matters
because this plant runs three lines with identical station ids: `LOADER` alone
matches three machines.

### Blocked on hardware

- The EPIC is not reachable. As of 2026-08-27 the address probed for it
  answered on 443 as a Philips Hue bridge — so the address is wrong, not the
  device dead. Both blanks are marked `<<PLC_IP>>` and `<<BROKER_IP>>` in
  [`groov_manage_setup.md`](../Tools/plc/groov_manage_setup.md).
- No tags have been created in groov Manage.
- Real I/O is unwired: `diEStopOk`, `diRunSelector`, `diResetButton` and three
  stack-light outputs are referenced by the strategy and connected to nothing.

### Known gaps that are not hardware

- **The loop is open.** The strategy owns takt and fires stations on fixed
  offsets. It does not wait for the twin to report a cycle complete. `ready` and
  `busy` are published and nothing on the PLC reads them.
- **No fault injection.** There is a `reset` command and no way to *cause* a
  fault from outside, so "clear a fault from the panel" cannot be demonstrated
  end to end.

---

## 2. The one architectural decision, restated

**The twin follows the PLC. The PLC does not command the twin.**

A groov EPIC is a Sparkplug *edge node*. The specification reserves `DCMD` for a
primary application, so an edge node has no way to command a peer. Rather than
put Ignition in the control path as a translator — one more hop, and a line that
stops when Ignition does — the twin subscribes to the PLC's own data stream and
acts on it.

The canonical direction still works: a real primary application can write the
same commands over `DCMD` and the twin honours them. Both paths land in the same
code, which is why `ApplyStationCommand` takes `bEdgeSensitive` — a followed PLC
republishes its whole tag set every scan, where a `DCMD` is a discrete
instruction and two identical writes must mean two triggers.

Do not "fix" this by making the EPIC a primary application. It is not one.

---

## 3. Commissioning runbook

Each phase has an acceptance test. Do not start the next one until the current
one passes — most of the ways this fails are silent, and a failure two phases
back looks like a bug in the phase you are in.

### Phase 1 — Reach the device

1. Find the EPIC's address (its display shows it; otherwise the DHCP lease
   table). Fill `<<PLC_IP>>` in `groov_manage_setup.md`.
2. `ping <<PLC_IP>>`, then open `https://<<PLC_IP>>` and sign in to groov Manage.
3. Fill `<<BROKER_IP>>` with this workstation's LAN address — that is the one
   the device actually needs, because the EPIC connects **outward** to the
   broker.

> **Credentials:** create the API key or MQTT user yourself in groov Manage and
> put it in the compose `.env` or Vaultwarden. Do not paste it into a file that
> is committed, and do not paste it into this repository.

**Passes when:** groov Manage loads and `nc -vz <<BROKER_IP>> 31883` succeeds
from the device's network.

### Phase 2 — Publish one tag

1. Create the MQTT/Sparkplug client in groov Manage per `groov_manage_setup.md`.
2. Create a single tag — `Line1_LOADER_trigger` — and nothing else.
3. Start the twin with external control on:
   `-ini:Game:[/Script/FactorySim.FactoryTwinSettings]:bExternalControlEnabled=True`

**Passes when:** `python Tools/plc/watch_line.py --line Line1` shows the loader
cycling once per change of that tag, and exactly once. If it cycles continuously
the tag is being read as a level rather than an edge; if it never cycles the
name is not resolving — check the edge node id prefix, which is step 3 of the
grammar and is the part most likely to be wrong.

### Phase 3 — The full tag set

Create the rest per [`tag_database.md`](../Tools/plc/PAC_Control/tag_database.md).
Load `SMT_Line_Sequencer.osc`.

**Passes when:** the line runs on the PLC's takt, and `watch_line.py --line
Line2` still shows zero cycles. That is the differential test again, now against
real hardware, and it is the moment the twin is genuinely PLC-driven.

### Phase 4 — Interlocks

Wire `diEStopOk`, `diRunSelector`, `diResetButton` and the stack lights.

An unwired input reads `0`, so the permissive is false and the line sits
blocked — the right direction to fail in, and also why phases 2 and 3 need the
permissive forced true in the strategy until this phase.

**Passes when:** the physical e-stop stops the line in the twin, the reset
button clears it, and the stack light matches what the twin shows.

> Nothing safety-related routes through any of this. `diEStopOk` is read so the
> twin can *display* an e-stop. The chain that actually stops a machine is
> hardwired and stays that way.

---

## 4. Remaining engineering, in priority order

### 4.1 Close the loop — *highest value, most work*

Today the strategy fires on offsets and hopes. Closing the loop means the PLC
waits for `busy` to fall before advancing, which turns a timed animation into
something that genuinely handshakes.

Needs the groov MQTT client's **subscribe** side plus a Sparkplug decode step in
strategy — the twin already publishes everything required. Until it exists the
offsets stand in for the handshake, and any station that runs long silently
desynchronises from the PLC's idea of where the line is.

### 4.2 Fault injection

Add a `fault` command alongside `reset` so a fault can be raised from outside and
cleared from the panel. Small change in `ApplyStationCommand`; makes the most
demonstrable use case in section 5 actually demonstrable.

### 4.3 Wire the stack light to line state

The outputs are referenced but the mapping from twin state to lamp is not
defined. Cheap once phase 4 is wired, and it is the most legible "the PLC and
the twin agree" signal in the room.

---

## 5. What this buys — use cases

| Use case | State |
|---|---|
| A physical PLC sequences a virtual line, with real takt | Ready, pending hardware |
| Prove line-qualified addressing: one controller drives Line 1 while Lines 2–3 stay blocked | **Working now**, against the stand-in |
| Interlock demonstration: controller goes quiet, line stops | **Working now** — turn `bFallBackToLocalOnPlcTimeout` off when the interlock *is* the demo |
| Graceful degradation: PLC dies, sim keeps running locally | **Working now**, fallback on |
| Physical e-stop visibly stops a virtual line | Needs phase 4 |
| Raise a fault, clear it from the panel | Needs 4.2 |
| Closed-loop handshake, not open-loop timing | Needs 4.1 |
| SCADA path: Ignition commands the same stations over `DCMD` | Code path exists, untested against real Ignition |

---

## 6. Timing, honestly

This is supervisory sequencing, not real-time control. A broker round trip is
tens of milliseconds with no upper bound; the twin ticks at about 25 ms. That is
entirely adequate for "start the next station" and completely unfit for
interpolated motion or anything safety-rated.

Do not let the demo's smoothness imply otherwise to anyone watching it.

---

## 7. Do I need PAC Control installed?

For phases 1 and 2, no — tags can be created in groov Manage directly, and the
stand-in needs nothing.

For phase 3 onward, yes: `SMT_Line_Sequencer.osc` is a strategy and has to be
compiled and downloaded by PAC Control (Windows only, free from Opto 22). The
`.osc` file in this repository is the source of record either way.
