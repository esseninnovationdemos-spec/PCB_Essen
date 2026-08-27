# Bringing the groov EPIC onto the bus

Everything in this file is done on the device, in groov Manage. The strategy in
`PAC_Control/` computes the tags; this is what publishes them.

> **Blanks to fill.** The device is not on the network yet — a sweep of
> `192.168.250.0/24` on 2026-08-27 found nothing answering on 502 (Modbus),
> 2001 (OptoMMP) or 4840 (OPC UA). Two addresses are needed and neither can be
> guessed:
>
> | | Value | Where it goes |
> |---|---|---|
> | **`<<PLC_IP>>`** | _(blank)_ | The EPIC's own address. Used only to reach groov Manage in a browser and for diagnostics. |
> | **`<<BROKER_IP>>`** | _(blank — currently `192.168.250.167`)_ | This workstation's LAN address, which is what the EPIC connects **out** to. |
>
> `<<BROKER_IP>>` is the one the device actually needs. `<<PLC_IP>>` is only how
> you reach the device to configure it — nothing in the running system dials the
> PLC, because the connection is outbound from it to the broker.

## 0. Confirm it is reachable

```bash
ping <<PLC_IP>>
```

Then open `https://<<PLC_IP>>` and sign in to groov Manage. The landing page
shows the firmware revision — note it, because native Sparkplug B needs a
reasonably current one. If MQTT does not appear where step 2 expects it, that
revision is why, and Node-RED on the device is the fallback bridge.

## 1. Make the broker reachable from the device

EMQX listens on `31883` on this workstation. Two things commonly stop the EPIC
reaching it, and both are on this side, not the device's:

- **Windows Firewall.** Docker's published port is not automatically open to the
  LAN. Allow inbound TCP 31883:

  ```powershell
  New-NetFirewallRule -DisplayName "EMQX MQTT 31883" -Direction Inbound -Protocol TCP -LocalPort 31883 -Action Allow
  ```

- **The address changes.** `192.168.250.167` is a DHCP lease on the WLAN
  adapter. Reserve it, or put the EPIC on the wired network with a static
  address on both ends. A demo that dies because a lease rolled over is a bad
  afternoon.

Check from the device's own network before going further:

```bash
nc -vz <<BROKER_IP>> 31883
```

## 2. Configure the MQTT client

In groov Manage: **MQTT → Add broker** (older firmware: **Configure → MQTT**).

| Field | Value | Why |
|---|---|---|
| Host | `<<BROKER_IP>>` | Outbound from the EPIC. |
| Port | `31883` | |
| TLS | off | The broker has no certificate the EPIC would trust. Fine on a lab LAN; see the note below. |
| Username / password | as provisioned | See "Credentials". |
| Client ID | `groov_epic_plc01` | Must be unique on the broker — a duplicate silently kicks the other session off in a loop. |
| Payload format | **Sparkplug B** | Not "string". String payloads publish JSON on a different topic tree and the twin will not see them. |
| Group ID | `InnoLab:Essen:SMT` | Must match the line exactly, colons included. Sparkplug reserves `/`, `+` and `#` here, which is why the ISA-95 levels are colon-joined. |
| Edge Node ID | `PLC01` | Must match `PlcEdgeNodeId` in the twin's settings. |

Group and Edge Node ID are the two that must be character-exact. Everything
else is recoverable; those two silently produce a controller that publishes
perfectly into a topic tree nobody is listening to.

## 3. Publish the strategy tags

In the broker's tag list, add the PAC Control variables from
`PAC_Control/tag_database.md` — the 51 per-station tags plus `Line1_new_material`
and `Line1_mode`.

Two settings that matter:

- **Publish on change**, not on a fixed interval. The twin reads triggers as
  edges, so a periodic republish of an unchanged counter is wasted traffic; and
  a *changed* counter needs to go out promptly, not at the next poll.
- **Do not publish the internal tags.** `nIndex`, the pointer tables and the
  loop scratch add nothing downstream and make the birth certificate harder to
  read.

## 4. Point the twin at it

In the project, `Config/DefaultGame.ini`:

```ini
[/Script/FactorySim.FactoryTwinSettings]
bExternalControlEnabled=True
PlcEdgeNodeId=PLC01
PlcTimeoutSeconds=10.0
bFallBackToLocalOnPlcTimeout=True
PlcHostAddress=<<PLC_IP>>
```

`PlcHostAddress` is documentation and diagnostics only — nothing opens a
connection to it. The control path is the broker.

## 5. Verify, in this order

Each step isolates one link, so a failure tells you where it is.

1. **The EPIC connected at all.** `MQTT → Clients` in the EMQX dashboard should
   list `groov_epic_plc01`. If not, it is the firewall or the address.
2. **It is publishing Sparkplug.** Subscribe to `spBv1.0/InnoLab:Essen:SMT/#`
   and look for `NBIRTH/PLC01`. A birth certificate with 53 metrics means the
   tag selection is right.
3. **The twin is following.** The Unreal log prints
   `Observing peer edge node 'PLC01' in group 'InnoLab:Essen:SMT'` at line
   start, then `Control mode is now external`.
4. **Stations respond.** Turn the run selector. Enables go to 1, and stations
   begin cycling on takt rather than free-running.

If 1–3 pass and 4 does not, it is almost always a station id spelled
differently from the level. The twin ignores tags it cannot resolve rather than
warning, because one PLC may legitimately name stations that are not in the
loaded level.

## Credentials

Create the MQTT user (or API key) in groov Manage yourself and put the value in
`compose/.env` alongside the rest of the stack's secrets. Passwords should not
be typed into this repository, and nothing here needs to see the value — the
device holds it, and the broker checks it.

## On TLS

Off, above, because the broker presents no certificate the EPIC would trust and
the alternative is disabling verification, which is worse than not pretending.
That is acceptable for a lab LAN carrying no real production data. It is not
acceptable on a plant network: there, give EMQX a certificate from a CA the EPIC
trusts and turn verification on properly — and note that credentials cross the
wire in the clear until you do.
