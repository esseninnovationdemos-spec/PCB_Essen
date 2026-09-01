"""
Just enough MQTT and Sparkplug B to stand in for a PLC and to watch a line.

Dependency-free on purpose: these scripts run on a commissioning laptop, on the
EPIC itself, or in a container, and `pip install` is one more thing to fail at
the wrong moment. Field numbers match SparkplugProto.h, which is the encoder the
twin actually uses -- if the two ever disagree, that file is right and this one
is wrong.

Not a general Sparkplug library. No aliases, no templates, no datasets, no QoS 2,
no reconnect. It covers the metric shapes a PLC publishes and nothing else.
"""

import select
import socket
import struct
import time

# Tahu datatype codes.
DT_INT8 = 1
DT_INT16 = 2
DT_INT32 = 3
DT_INT64 = 4
DT_UINT32 = 7
DT_UINT64 = 8
DT_FLOAT = 9
DT_DOUBLE = 10
DT_BOOLEAN = 11
DT_STRING = 12

# Payload field numbers.
_P_TIMESTAMP, _P_METRICS, _P_SEQ = 1, 2, 3

# Metric field numbers.
_M_NAME, _M_ALIAS, _M_TIMESTAMP, _M_DATATYPE = 1, 2, 3, 4
_M_INT, _M_LONG, _M_FLOAT, _M_DOUBLE, _M_BOOL, _M_STRING = 10, 11, 12, 13, 14, 15


# ---------------------------------------------------------------------------
# protobuf
# ---------------------------------------------------------------------------

def _varint(value):
    out = bytearray()
    while True:
        byte = value & 0x7F
        value >>= 7
        if value:
            out.append(byte | 0x80)
        else:
            out.append(byte)
            return bytes(out)


def _tag(field, wire):
    return _varint((field << 3) | wire)


def _len_field(field, payload):
    return _tag(field, 2) + _varint(len(payload)) + payload


def _varint_field(field, value):
    return _tag(field, 0) + _varint(value)


def encode_metric(name, datatype, value, timestamp_ms):
    out = bytearray()
    out += _len_field(_M_NAME, name.encode("utf-8"))
    out += _varint_field(_M_TIMESTAMP, timestamp_ms)
    out += _varint_field(_M_DATATYPE, datatype)
    if datatype == DT_STRING:
        out += _len_field(_M_STRING, str(value).encode("utf-8"))
    elif datatype == DT_BOOLEAN:
        out += _varint_field(_M_BOOL, 1 if value else 0)
    else:
        # Two's complement into the unsigned field, as Tahu does for int32.
        out += _varint_field(_M_INT, int(value) & 0xFFFFFFFF)
    return bytes(out)


def encode_payload(metrics, seq, timestamp_ms=None):
    """metrics is a list of (name, datatype, value). seq=None omits it, for DEATH."""
    if timestamp_ms is None:
        timestamp_ms = int(time.time() * 1000)
    out = bytearray()
    out += _varint_field(_P_TIMESTAMP, timestamp_ms)
    for name, datatype, value in metrics:
        out += _len_field(_P_METRICS, encode_metric(name, datatype, value, timestamp_ms))
    if seq is not None:
        out += _varint_field(_P_SEQ, seq)
    return bytes(out)


def _read_varint(buf, pos):
    result = 0
    shift = 0
    while pos < len(buf):
        byte = buf[pos]
        pos += 1
        result |= (byte & 0x7F) << shift
        if not byte & 0x80:
            return result, pos
        shift += 7
    raise ValueError("truncated varint")


def _skip(buf, pos, wire):
    if wire == 0:
        _value, pos = _read_varint(buf, pos)
        return pos
    if wire == 1:
        return pos + 8
    if wire == 2:
        length, pos = _read_varint(buf, pos)
        return pos + length
    if wire == 5:
        return pos + 4
    raise ValueError("unsupported wire type %d" % wire)


def decode_metric(buf):
    metric = {"name": None, "datatype": None, "value": None}
    pos = 0
    while pos < len(buf):
        key, pos = _read_varint(buf, pos)
        field, wire = key >> 3, key & 7
        if field == _M_NAME and wire == 2:
            length, pos = _read_varint(buf, pos)
            metric["name"] = buf[pos:pos + length].decode("utf-8", "replace")
            pos += length
        elif field == _M_DATATYPE and wire == 0:
            metric["datatype"], pos = _read_varint(buf, pos)
        elif field in (_M_INT, _M_LONG, _M_BOOL) and wire == 0:
            metric["value"], pos = _read_varint(buf, pos)
        elif field == _M_DOUBLE and wire == 1:
            metric["value"] = struct.unpack("<d", buf[pos:pos + 8])[0]
            pos += 8
        elif field == _M_FLOAT and wire == 5:
            metric["value"] = struct.unpack("<f", buf[pos:pos + 4])[0]
            pos += 4
        elif field == _M_STRING and wire == 2:
            length, pos = _read_varint(buf, pos)
            metric["value"] = buf[pos:pos + length].decode("utf-8", "replace")
            pos += length
        else:
            pos = _skip(buf, pos, wire)
    if metric["datatype"] == DT_BOOLEAN and metric["value"] is not None:
        metric["value"] = bool(metric["value"])
    return metric


def decode_payload(buf):
    payload = {"timestamp": None, "seq": None, "metrics": []}
    pos = 0
    while pos < len(buf):
        key, pos = _read_varint(buf, pos)
        field, wire = key >> 3, key & 7
        if field == _P_TIMESTAMP and wire == 0:
            payload["timestamp"], pos = _read_varint(buf, pos)
        elif field == _P_SEQ and wire == 0:
            payload["seq"], pos = _read_varint(buf, pos)
        elif field == _P_METRICS and wire == 2:
            length, pos = _read_varint(buf, pos)
            payload["metrics"].append(decode_metric(buf[pos:pos + length]))
            pos += length
        else:
            pos = _skip(buf, pos, wire)
    return payload


# ---------------------------------------------------------------------------
# MQTT
# ---------------------------------------------------------------------------

class MqttClient:
    """CONNECT, PUBLISH, SUBSCRIBE and PINGREQ. Nothing else."""

    def __init__(self, host, port, client_id, keepalive=60):
        self.host = host
        self.port = port
        self.client_id = client_id
        self.keepalive = keepalive
        self.sock = None
        self.last_ping = 0.0
        self._rx = bytearray()
        self._packet_id = 0

    @staticmethod
    def _remaining_length(n):
        out = bytearray()
        while True:
            byte = n % 128
            n //= 128
            if n:
                out.append(byte | 0x80)
            else:
                out.append(byte)
                return bytes(out)

    @staticmethod
    def _string(text):
        raw = text.encode("utf-8")
        return struct.pack("!H", len(raw)) + raw

    def _next_packet_id(self):
        self._packet_id = (self._packet_id % 65535) + 1
        return self._packet_id

    def _send(self, header, body):
        self.sock.sendall(bytes([header]) + self._remaining_length(len(body)) + body)

    def connect(self, will_topic=None, will_payload=None):
        self.sock = socket.create_connection((self.host, self.port), timeout=10)
        self.sock.settimeout(1.0)

        flags = 0x02                                     # clean session
        payload = self._string(self.client_id)
        if will_topic is not None:
            flags |= 0x04                                # will flag
            flags |= 0x01 << 3                           # will QoS 1
            payload += self._string(will_topic)
            payload += struct.pack("!H", len(will_payload)) + will_payload

        variable = self._string("MQTT") + bytes([4, flags]) + struct.pack("!H", self.keepalive)
        self._send(0x10, variable + payload)

        header = self.sock.recv(4)
        if len(header) < 4 or header[0] != 0x20:
            raise RuntimeError("no CONNACK from %s:%d" % (self.host, self.port))
        if header[3] != 0:
            raise RuntimeError("broker refused the connection, CONNACK code %d" % header[3])
        self.last_ping = time.time()

    def publish(self, topic, payload, qos=0):
        body = self._string(topic)
        if qos:
            body += struct.pack("!H", self._next_packet_id())
        self._send(0x30 | (qos << 1), body + payload)

    def subscribe(self, topic_filter, qos=0):
        body = struct.pack("!H", self._next_packet_id())
        body += self._string(topic_filter) + bytes([qos])
        self._send(0x82, body)

    def _pump(self):
        """Reads whatever is waiting. Never blocks."""
        while select.select([self.sock], [], [], 0)[0]:
            try:
                chunk = self.sock.recv(65536)
            except (socket.timeout, BlockingIOError):
                return
            if not chunk:
                raise ConnectionError("broker closed the connection")
            self._rx += chunk

    def poll(self):
        """Yields (topic, payload_bytes) for every complete PUBLISH received."""
        self._pump()

        now = time.time()
        if now - self.last_ping >= self.keepalive / 2:
            self._send(0xC0, b"")                        # PINGREQ
            self.last_ping = now

        while True:
            if len(self._rx) < 2:
                return

            # Decode the remaining-length field, which is 1-4 bytes.
            length = 0
            multiplier = 1
            index = 1
            while True:
                if index >= len(self._rx):
                    return                               # header still incomplete
                byte = self._rx[index]
                length += (byte & 0x7F) * multiplier
                index += 1
                if not byte & 0x80:
                    break
                multiplier *= 128

            total = index + length
            if len(self._rx) < total:
                return                                   # body still incomplete

            packet = bytes(self._rx[:total])
            del self._rx[:total]

            if packet[0] & 0xF0 != 0x30:                 # not a PUBLISH
                continue

            qos = (packet[0] & 0x06) >> 1
            pos = index
            topic_len = struct.unpack("!H", packet[pos:pos + 2])[0]
            pos += 2
            topic = packet[pos:pos + topic_len].decode("utf-8", "replace")
            pos += topic_len
            if qos:
                pos += 2                                 # packet id
            yield topic, packet[pos:]

    def close(self):
        if self.sock is not None:
            try:
                self._send(0xE0, b"")                    # DISCONNECT
            except OSError:
                pass
            self.sock.close()
            self.sock = None


def split_topic(topic):
    """spBv1.0/<group>/<verb>/<node>[/<device>] -> (group, verb, node, device)."""
    parts = topic.split("/")
    if len(parts) < 4:
        return None, None, None, None
    return parts[1], parts[2], parts[3], (parts[4] if len(parts) > 4 else None)
