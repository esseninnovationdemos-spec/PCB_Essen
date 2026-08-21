# ════════════════════════════════════════════════════════════════════════════
# spb_codec.py
#
#   Thin codec on top of Eclipse Tahu's generated protobuf (sparkplug_b_pb2).
#
#   PROVIDES
#     SpbCodec(group, edge_node)          – per-edge-node encoder
#       .nbirth_payload(metrics)          → bytes
#       .dbirth_payload(device, metrics)  → bytes
#       .ddata_payload(device, metrics)   → bytes
#       .ndeath_payload()                 → bytes
#       .ddeath_payload(device)           → bytes
#       .topic(msg_type, device=None)     → str
#       .reset_seq()                      – on (re)connect after NBIRTH
#
#   SETUP
#     1. pip install paho-mqtt protobuf
#     2. Drop Eclipse Tahu's generated sparkplug_b_pb2.py next to this file.
#        Source: https://github.com/eclipse/tahu/blob/master/python/core/sparkplug_b_pb2.py
#
#   METRIC FORMAT (dicts passed in)
#     {
#       "name":      "motor_temp",
#       "type":      "Float",   # Float | Double | Int32 | Int64 | Boolean | String
#       "value":     67.3,
#       "alias":     12,        # optional, recommended for DDATA bandwidth
#       "timestamp": 1719327600000   # optional ms epoch, default=now
#     }
# ════════════════════════════════════════════════════════════════════════════

import time
import logging

try:
    import sparkplug_b_pb2 as spb_pb2
except ImportError as e:
    raise ImportError(
        "Missing sparkplug_b_pb2.py – download it from Eclipse Tahu:\n"
        "  https://github.com/eclipse/tahu/blob/master/python/core/sparkplug_b_pb2.py\n"
        "and place it next to spb_codec.py"
    ) from e

log = logging.getLogger(__name__)

# Sparkplug B metric data type constants (from Tahu spec table)
DTYPE = {
    "Int8":      1,  "Int16":     2,  "Int32":     3,  "Int64":     4,
    "UInt8":     5,  "UInt16":    6,  "UInt32":    7,  "UInt64":    8,
    "Float":     9,  "Double":   10,  "Boolean":  11,  "String":   12,
    "DateTime": 13,  "Text":     14,  "UUID":     15,  "Bytes":    17,
}

# Sparkplug B NCMD-defined "bd_seq" (birth-death sequence) – Tahu convention
_BD_SEQ_NAME = "bdSeq"


def _now_ms():
    return int(time.time() * 1000)


def _set_metric_value(m, dtype_str, value):
    """Assign value to the correct typed field on a protobuf Metric."""
    if value is None:
        m.is_null = True
        return
    if dtype_str == "Float":
        m.float_value = float(value)
    elif dtype_str == "Double":
        m.double_value = float(value)
    elif dtype_str in ("Int8","Int16","Int32"):
        m.int_value = int(value)
    elif dtype_str == "Int64":
        m.long_value = int(value)
    elif dtype_str in ("UInt8","UInt16","UInt32"):
        m.int_value = int(value) & 0xFFFFFFFF
    elif dtype_str == "UInt64":
        m.long_value = int(value) & 0xFFFFFFFFFFFFFFFF
    elif dtype_str == "Boolean":
        m.boolean_value = bool(value)
    elif dtype_str == "String" or dtype_str == "Text":
        m.string_value = str(value)
    elif dtype_str == "DateTime":
        m.long_value = int(value)
    elif dtype_str == "Bytes":
        m.bytes_value = bytes(value)
    else:
        raise ValueError(f"Unsupported Sparkplug B datatype: {dtype_str}")


def _add_metric(payload, m_dict):
    """
    Append one metric to a Payload protobuf.

    Sparkplug B allows DDATA to reference metrics by alias only – the
    consumer resolves the alias against the DBIRTH-cached name. To use
    that, omit the "name" key when calling ddata_payload().  BIRTH
    messages must always carry name + alias so consumers can build the map.
    """
    m = payload.metrics.add()
    if m_dict.get("name"):
        m.name = m_dict["name"]
    m.timestamp = m_dict.get("timestamp", _now_ms())
    dtype_str = m_dict["type"]
    m.datatype = DTYPE[dtype_str]
    if "alias" in m_dict:
        m.alias = m_dict["alias"]
    _set_metric_value(m, dtype_str, m_dict.get("value"))


class SpbCodec:
    """One codec per edge node.  Owns the spB seq + bdSeq counters."""

    def __init__(self, group_id, edge_node_id, namespace="spBv1.0"):
        self.group_id     = group_id
        self.edge_node_id = edge_node_id
        self.namespace    = namespace
        self._seq         = 0          # 0..255, increments on every Payload
        self._bd_seq      = 0          # 0..255, increments on every (re)connect

    # ─── seq management ────────────────────────────────────────────────────
    def _next_seq(self):
        s = self._seq
        self._seq = (self._seq + 1) % 256
        return s

    def reset_seq(self):
        """Call this immediately before sending NBIRTH on (re)connect."""
        self._seq = 0

    def next_bd_seq(self):
        """Bump the bdSeq for the NEXT NBIRTH (and matching NDEATH LWT)."""
        s = self._bd_seq
        self._bd_seq = (self._bd_seq + 1) % 256
        return s

    # ─── topic builder ─────────────────────────────────────────────────────
    def topic(self, msg_type, device=None):
        """
        msg_type: one of NBIRTH | NDEATH | DBIRTH | DDATA | DDEATH | NCMD | DCMD
        device:   required for D* messages, ignored for N* messages
        """
        base = f"{self.namespace}/{self.group_id}/{msg_type}/{self.edge_node_id}"
        if msg_type.startswith("D"):
            if not device:
                raise ValueError(f"{msg_type} requires a device name")
            return f"{base}/{device}"
        return base

    # ─── payload builders ──────────────────────────────────────────────────
    def nbirth_payload(self, metrics, bd_seq):
        """
        NBIRTH – sent ONCE per session, immediately after (re)connect.
        Must include the bdSeq metric matching the LWT NDEATH.
        """
        p = spb_pb2.Payload()
        p.timestamp = _now_ms()
        p.seq = self._next_seq()

        # bdSeq must come first (Sparkplug B spec)
        _add_metric(p, {
            "name": _BD_SEQ_NAME, "type": "Int64", "value": bd_seq,
        })
        # Optional Node Control/Rebirth metric – lets SCADA force a rebirth
        _add_metric(p, {
            "name": "Node Control/Rebirth", "type": "Boolean", "value": False,
        })
        for m in metrics:
            _add_metric(p, m)
        return p.SerializeToString()

    def ndeath_payload(self, bd_seq):
        """
        NDEATH – registered as the MQTT Last Will, fires automatically when
        the client disconnects ungracefully.  Must carry the same bdSeq as
        the NBIRTH it pairs with.
        """
        p = spb_pb2.Payload()
        p.timestamp = _now_ms()
        # NOTE: NDEATH does NOT carry seq (Tahu spec § 5.3)
        _add_metric(p, {
            "name": _BD_SEQ_NAME, "type": "Int64", "value": bd_seq,
        })
        return p.SerializeToString()

    def dbirth_payload(self, device, metrics):
        """
        DBIRTH – sent once per device after NBIRTH.  Every metric that will
        ever appear in DDATA must be declared here with its initial value.
        Aliases assigned here are reused in DDATA.
        """
        p = spb_pb2.Payload()
        p.timestamp = _now_ms()
        p.seq = self._next_seq()
        for m in metrics:
            _add_metric(p, m)
        return p.SerializeToString()

    def ddata_payload(self, device, metrics):
        """
        DDATA – the hot-path payload.  Use aliases (declared in DBIRTH)
        to keep payload size minimal.
        """
        p = spb_pb2.Payload()
        p.timestamp = _now_ms()
        p.seq = self._next_seq()
        for m in metrics:
            _add_metric(p, m)
        return p.SerializeToString()

    def ddeath_payload(self, device):
        """DDEATH – clean shutdown of a single device."""
        p = spb_pb2.Payload()
        p.timestamp = _now_ms()
        p.seq = self._next_seq()
        return p.SerializeToString()