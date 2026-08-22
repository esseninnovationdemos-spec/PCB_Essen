#!/usr/bin/env python3
"""Subscribe to the Sparkplug B namespace and pretty-print decoded payloads.

Run under a SYSTEM Python, not Unreal's embedded interpreter -- the project sets
bIsolateInterpreterEnvironment=True, which blocks site-packages and is exactly
why the in-editor Python layer currently fails with "No module named 'paho'".

    pip install paho-mqtt protobuf
    python Tools/broker/spb_dump.py [--host localhost] [--port 1883]

Reuses Content/Python/sparkplug_b_pb2.py as the schema, so what this prints is
what a real Sparkplug consumer (Ignition, the ClickHouse bridge) would see. That
makes it the oracle for the byte-parity gate in Phase 2.
"""

from __future__ import annotations

import argparse
import datetime as _dt
import pathlib
import sys

_PY_DIR = pathlib.Path(__file__).resolve().parents[2] / "Content" / "Python"
sys.path.insert(0, str(_PY_DIR))

try:
    import paho.mqtt.client as mqtt
except ImportError:
    sys.exit("paho-mqtt is missing.  pip install paho-mqtt protobuf")

try:
    import sparkplug_b_pb2
except ImportError:
    sys.exit(f"sparkplug_b_pb2.py not found under {_PY_DIR}")


# Sparkplug B datatype enum -> label, for readable output.
_DATATYPES = {
    1: "Int8", 2: "Int16", 3: "Int32", 4: "Int64",
    5: "UInt8", 6: "UInt16", 7: "UInt32", 8: "UInt64",
    9: "Float", 10: "Double", 11: "Boolean", 12: "String",
    13: "DateTime", 14: "Text", 15: "UUID", 16: "DataSet",
    17: "Bytes", 18: "File", 19: "Template",
}

_VALUE_FIELDS = (
    "int_value", "long_value", "float_value", "double_value",
    "boolean_value", "string_value", "bytes_value",
)


def _metric_value(metric) -> object:
    if metric.is_null:
        return None
    for field in _VALUE_FIELDS:
        if metric.HasField(field):
            return getattr(metric, field)
    return "<unset>"


def _format_ts(ms: int) -> str:
    if not ms:
        return "-"
    return _dt.datetime.fromtimestamp(ms / 1000.0).strftime("%H:%M:%S.%f")[:-3]


def _on_message(_client, _userdata, msg) -> None:
    payload = sparkplug_b_pb2.Payload()
    try:
        payload.ParseFromString(msg.payload)
    except Exception as exc:  # noqa: BLE001 - want the raw bytes on any failure
        print(f"\n!! {msg.topic}: could not decode ({exc})")
        print(f"   {len(msg.payload)} bytes: {msg.payload[:64].hex(' ')}")
        return

    seq = payload.seq if payload.HasField("seq") else "-"
    print(f"\n{msg.topic}   seq={seq}   ts={_format_ts(payload.timestamp)}   "
          f"({len(msg.payload)} bytes, {len(payload.metrics)} metrics)")

    for metric in payload.metrics:
        alias = metric.alias if metric.HasField("alias") else "-"
        dtype = _DATATYPES.get(metric.datatype, metric.datatype)
        value = _metric_value(metric)
        if isinstance(value, float):
            value = f"{value:.4f}"
        print(f"    [{alias:>3}] {metric.name:<34} {dtype:<8} = {value}")


def _on_connect(client, _userdata, _flags, rc) -> None:
    if rc != 0:
        print(f"connect failed, rc={rc}")
        return
    topic = "spBv1.0/#"
    client.subscribe(topic, qos=0)
    print(f"connected; subscribed to {topic}\nwaiting for traffic (ctrl-c to stop)...")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="localhost")
    parser.add_argument("--port", type=int, default=1883)
    args = parser.parse_args()

    # paho 2.x moved to a callback-API-version kwarg; support both.
    try:
        client = mqtt.Client(
            mqtt.CallbackAPIVersion.VERSION1, client_id="spb_dump"
        )
    except (AttributeError, TypeError):
        client = mqtt.Client(client_id="spb_dump")

    client.on_connect = _on_connect
    client.on_message = _on_message

    print(f"connecting to {args.host}:{args.port} ...")
    client.connect(args.host, args.port, keepalive=60)
    try:
        client.loop_forever()
    except KeyboardInterrupt:
        print("\nstopped")
        client.disconnect()


if __name__ == "__main__":
    main()
