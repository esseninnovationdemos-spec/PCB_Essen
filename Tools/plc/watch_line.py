#!/usr/bin/env python3
"""
Watches a line on the broker and prints what each station is doing.

Written for step 5 of ../groov_manage_setup.md, where the question is always the
same and always specific: is the PLC publishing, is the twin following, and are
the stations reacting? Each column answers one of those, so a failure says which
link broke instead of just "nothing happens".

    python watch_line.py --line Line1

Columns:
    STATE     Idle / Running / Fault / Blocked / Warmup / Cooldown
    MODE      local  -- the line sequences itself
              external -- it is waiting to be told
    READY     a trigger arriving now would start a cycle
    EN        the station is enabled and not held
    CYCLES    cycle completions counted since this watcher started
    AGE       seconds since that station last published

A station sitting at Blocked/external with no cycles is the normal picture when
the twin is following a PLC that is not triggering it -- that is the interlock
working, not a fault.
"""

import argparse
import sys
import time

from sparkplug_lite import MqttClient, decode_payload, split_topic

STATE_NAMES = {0: "Idle", 1: "Running", 2: "Fault",
               3: "Blocked", 4: "Warmup", 5: "Cooldown"}

# Highlight what usually matters: a fault, or a station held out of the line.
STATE_MARK = {"Fault": "!", "Blocked": "*"}


class Station:
    __slots__ = ("state", "mode", "ready", "enabled", "cycles", "last_seen")

    def __init__(self):
        self.state = None
        self.mode = "-"
        self.ready = None
        self.enabled = None
        self.cycles = 0
        self.last_seen = 0.0


def render(stations, node, plc_node, plc_last_seen, started):
    rows = []
    for device in sorted(stations):
        s = stations[device]
        state = STATE_NAMES.get(s.state, "?" if s.state is None else str(s.state))
        rows.append("  %-1s%-18s %-9s %-9s %-6s %-4s %6d  %5.1fs" % (
            STATE_MARK.get(state, " "),
            device,
            state,
            s.mode,
            "yes" if s.ready else ("no" if s.ready is not None else "-"),
            "yes" if s.enabled else ("no" if s.enabled is not None else "-"),
            s.cycles,
            time.time() - s.last_seen,
        ))

    plc = "not seen"
    if plc_last_seen:
        plc = "%s, last heard %.1fs ago" % (plc_node, time.time() - plc_last_seen)

    header = ("\n%s  %s   up %.0fs   PLC: %s\n"
              "   %-18s %-9s %-9s %-6s %-4s %6s  %6s\n   %s"
              % (time.strftime("%H:%M:%S"), node, time.time() - started, plc,
                 "STATION", "STATE", "MODE", "READY", "EN", "CYCLES", "AGE",
                 "-" * 68))
    return header + "\n" + "\n".join(rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=31883)
    parser.add_argument("--group", default="InnoLab:Essen:SMT")
    parser.add_argument("--line", default="Line1", help="edge node id to watch")
    parser.add_argument("--plc", default="PLC01", help="controller edge node id")
    parser.add_argument("--interval", type=float, default=2.0, help="seconds between redraws")
    parser.add_argument("--once", action="store_true", help="print one table and exit")
    parser.add_argument("--settle", type=float, default=6.0,
                        help="seconds to listen before the --once table")
    args = parser.parse_args()

    client = MqttClient(args.host, args.port, "watch_line_%d" % (time.time() % 100000))
    client.connect()
    client.subscribe("spBv1.0/%s/#" % args.group, qos=0)

    stations = {}
    plc_last_seen = 0.0
    started = time.time()
    next_draw = started + (args.settle if args.once else args.interval)

    try:
        while True:
            for topic, payload in client.poll():
                group, verb, node, device = split_topic(topic)
                if group != args.group:
                    continue

                if node == args.plc:
                    plc_last_seen = time.time()
                    continue

                if node != args.line or device is None:
                    continue
                if verb not in ("DBIRTH", "DDATA"):
                    continue

                station = stations.setdefault(device, Station())
                station.last_seen = time.time()

                for metric in decode_payload(payload)["metrics"]:
                    name, value = metric["name"], metric["value"]
                    if name == "state_code":
                        station.state = value
                    elif name == "control_mode":
                        station.mode = value
                    elif name == "ready":
                        station.ready = bool(value)
                    elif name == "station_enabled":
                        station.enabled = bool(value)
                    elif name == "event_type" and value == "CYCLE_COMPLETE" and verb == "DDATA":
                        station.cycles += 1

            now = time.time()
            if now >= next_draw:
                print(render(stations, args.line, args.plc, plc_last_seen, started))
                if args.once:
                    return 0 if stations else 1
                next_draw = now + args.interval

            time.sleep(0.02)

    except KeyboardInterrupt:
        return 0
    finally:
        client.close()


if __name__ == "__main__":
    sys.exit(main())
