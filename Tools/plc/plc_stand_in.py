#!/usr/bin/env python3
"""
A software stand-in for the groov EPIC, speaking the wire protocol the real PLC
will speak.

Why this exists: everything downstream of the PLC -- the twin's command path,
the Ignition views, the handshake -- can be built and debugged against this, so
the arrival of real hardware changes a hostname rather than an architecture. It
stays useful afterwards as a regression harness that needs no PLC on the bench.

It publishes exactly what PAC Control publishes: flat integer tags named
`<Line>_<STATION>_<command>`, on its own Sparkplug edge node. The twin follows
that node rather than being commanded by it, because a groov EPIC is an edge
node and Sparkplug reserves commands for a primary application -- an edge node
has no way to issue a DCMD at a peer.

The sequencing logic mirrors PAC_Control/SMT_Line_Sequencer.osc: same station
order, same offsets, same counter-as-pulse convention. Change one, change both.

    python plc_stand_in.py --host 127.0.0.1 --port 31883 --takt 6

Ctrl-C publishes NDEATH, so the twin's watchdog sees a clean stop rather than
having to time one out.
"""

import argparse
import os
import random
import signal
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from sparkplug_lite import (  # noqa: E402
    DT_BOOLEAN, DT_INT32, DT_STRING, MqttClient, encode_payload)

# Process order, and when in the takt each station fires as a fraction of it.
# Staggered so material moves down the line the way it physically would; firing
# everything at zero would be simpler and would look wrong.
STATIONS = [
    ("LOADER", 0.00),
    ("LASER_MARKING", 0.05),
    ("SOLDER_PASTE", 0.10),
    ("SOLDER_INSP", 0.15),
    ("COMPONENT_PLACER", 0.20),
    ("REFLOW_OVEN", 0.28),
    ("AUTO_OPTICALINSP", 0.38),
    ("PCB_CLEANER", 0.44),
    ("HOUSING_ASSEMBLY", 0.50),
    ("PIN_INSERTION", 0.56),
    ("PIN_INSPECTION", 0.62),
    ("ASSEMBLY_ROBOT", 0.68),
    ("ICT", 0.74),
    ("FLASH_PROGRAMMING", 0.80),
    ("PIN_CHECK", 0.86),
    ("EOL_TEST", 0.92),
    ("PACKAGING", 0.97),
]


class StandInPlc:
    def __init__(self, args):
        self.args = args
        self.seq = 0
        self.bd_seq = 0
        self.counters = {}
        self.client = MqttClient(args.host, args.port, args.client_id)
        base = "spBv1.0/%s" % args.group
        self.t_birth = "%s/NBIRTH/%s" % (base, args.node)
        self.t_death = "%s/NDEATH/%s" % (base, args.node)
        self.t_data = "%s/NDATA/%s" % (base, args.node)

    def _next_seq(self):
        value = self.seq
        self.seq = (self.seq + 1) % 256
        return value

    def _tag(self, station, command):
        if station is None:
            return "%s_%s" % (self.args.line, command)
        return "%s_%s_%s" % (self.args.line, station, command)

    def _publish(self, metrics):
        self.client.publish(self.t_data, encode_payload(metrics, self._next_seq()))

    def start(self):
        # NDEATH is registered as the will before connecting, per the spec, so an
        # ungraceful exit still tells the twin this controller is gone.
        death = encode_payload([("bdSeq", DT_INT32, self.bd_seq)], None)
        self.client.connect(self.t_death, death)

        # Birth carries every tag at rest. The twin seeds its edge detector from
        # this without acting on it, so a reconnect cannot fire a phantom cycle.
        metrics = [("bdSeq", DT_INT32, self.bd_seq),
                   ("Node Control/Rebirth", DT_BOOLEAN, False),
                   (self._tag(None, "mode"), DT_STRING, "external"),
                   (self._tag(None, "new_material"), DT_INT32, 0)]
        for station, _offset in STATIONS:
            metrics.append((self._tag(station, "trigger"), DT_INT32, 0))
            metrics.append((self._tag(station, "enable"), DT_INT32, 1))
            metrics.append((self._tag(station, "reset"), DT_INT32, 0))
            self.counters[station] = 0

        self.seq = 0
        self.client.publish(self.t_birth, encode_payload(metrics, self._next_seq()))
        print("NBIRTH  %s  (%d tags)" % (self.t_birth, len(metrics)))

    def stop(self):
        try:
            self.client.publish(self.t_death,
                                encode_payload([("bdSeq", DT_INT32, self.bd_seq)], None))
            print("NDEATH  %s" % self.t_death)
        except OSError:
            pass
        self.client.close()

    def run(self):
        takt = self.args.takt
        boards = 0
        pending = []                       # (fire_at, station) for the takt in progress
        next_takt = time.time()

        while True:
            now = time.time()

            # Due before rollover: a station at 0.97 of takt would otherwise be
            # discarded by the rebuild on the very pass it came due.
            due = [item for item in pending if item[0] <= now]
            if due:
                pending = [item for item in pending if item[0] > now]
                metrics = []
                for _fire_at, station in due:
                    # A monotonic counter, not a toggling bit. The twin fires on
                    # any change to a non-zero value, so a counter works as a
                    # pulse -- and it also records how many cycles this
                    # controller believes it commanded, which a toggle cannot.
                    self.counters[station] += 1
                    metrics.append((self._tag(station, "trigger"),
                                    DT_INT32, self.counters[station]))
                self._publish(metrics)
                print("         trigger %s" % ", ".join(s for _f, s in due))

            if now >= next_takt:
                next_takt = now + takt
                boards += 1
                pending = [(now + offset * takt, station) for station, offset in STATIONS]

                self.counters["new_material"] = self.counters.get("new_material", 0) + 1
                self._publish([(self._tag(None, "new_material"),
                                DT_INT32, self.counters["new_material"])])
                print("takt %-4d release board" % boards)

            # Occasionally block a station, so the interlock path gets exercised
            # rather than only the happy one.
            if self.args.jitter and random.random() < 0.004:
                station = random.choice(STATIONS)[0]
                self._publish([(self._tag(station, "enable"), DT_INT32, 0)])
                print("         HOLD    %s" % station)
                time.sleep(0.4)
                self._publish([(self._tag(station, "enable"), DT_INT32, 1)])
                print("         RELEASE %s" % station)

            # Drains the socket and keeps the session alive. Not draining lets
            # the receive buffer fill and eventually stalls the publisher.
            for _topic, _payload in self.client.poll():
                pass

            time.sleep(0.02)


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--host", default="127.0.0.1", help="broker host")
    parser.add_argument("--port", type=int, default=31883, help="broker port")
    parser.add_argument("--group", default="InnoLab:Essen:SMT", help="Sparkplug group id")
    parser.add_argument("--node", default="PLC01", help="this controller's edge node id")
    parser.add_argument("--line", default="Line1", help="edge node id of the line to drive")
    parser.add_argument("--takt", type=float, default=6.0, help="seconds between board releases")
    parser.add_argument("--client-id", default="plc_stand_in", help="MQTT client id")
    parser.add_argument("--jitter", action="store_true",
                        help="occasionally block a station, to exercise the interlock")
    args = parser.parse_args()

    plc = StandInPlc(args)
    plc.start()

    def shutdown(_signum, _frame):
        plc.stop()
        sys.exit(0)

    signal.signal(signal.SIGINT, shutdown)
    signal.signal(signal.SIGTERM, shutdown)

    try:
        plc.run()
    except KeyboardInterrupt:
        plc.stop()
    except Exception:
        plc.stop()
        raise


if __name__ == "__main__":
    main()
