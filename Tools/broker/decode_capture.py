"""Decode a captured Sparkplug B payload with the Eclipse Tahu reference schema.

Cross-check for the hand-rolled C++ encoder in Plugins/FactoryTwin/Source/SparkplugB:
if this decodes cleanly and every field matches, the encoder is wire-compatible
with anything that speaks Sparkplug (Ignition, the ClickHouse bridge).

Runs in a container -- see Tools/broker/README.md for the exact invocation.
Expects the schema mounted at /spb and the capture at /data/nbirth.bin.
"""

import sys
sys.path.insert(0, "/spb")
import sparkplug_b_pb2

DT = {1:"Int8",2:"Int16",3:"Int32",4:"Int64",5:"UInt8",6:"UInt16",7:"UInt32",
      8:"UInt64",9:"Float",10:"Double",11:"Boolean",12:"String",13:"DateTime",
      14:"Text",15:"UUID",16:"DataSet",17:"Bytes",18:"File",19:"Template"}
VF = ("int_value","long_value","float_value","double_value",
      "boolean_value","string_value","bytes_value")

raw = open("/data/nbirth.bin","rb").read()
p = sparkplug_b_pb2.Payload()
n = p.ParseFromString(raw)   # raises on malformed input

print(f"DECODED OK: {len(raw)} bytes consumed by Eclipse Tahu schema")
print(f"  timestamp = {p.timestamp}")
print(f"  has seq   = {p.HasField('seq')}   seq = {p.seq}")
print(f"  metrics   = {len(p.metrics)}")
print()
for m in p.metrics:
    val = "<null>" if m.is_null else next(
        (getattr(m, f) for f in VF if m.HasField(f)), "<unset>")
    alias = m.alias if m.HasField("alias") else "-"
    print(f"  [{str(alias):>3}] {m.name:<30} {DT.get(m.datatype,m.datatype):<8} = {val!r}")
