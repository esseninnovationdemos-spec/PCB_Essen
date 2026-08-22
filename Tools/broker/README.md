# Local development broker

Stands in for the production broker (`192.168.100.102:31883`) until it is available.
Phases 1-4 of the FactoryTwin work test against this.

Everything here runs in Docker. This machine has no host Python and no host
mosquitto client, and none are needed.

## Start / stop

```bash
cd Tools/broker
docker compose up -d
docker compose logs -f
docker compose down
```

Listens on `localhost:1883`, anonymous, no TLS.

## Running the test suite against it

```bash
"C:/Program Files/Epic Games/UE_5.8/Engine/Binaries/Win64/UnrealEditor-Cmd.exe" AutoMotion_PCB.uproject -ExecCmds="Automation RunTests FactoryTwin" -unattended -nopause -nosplash -nullrhi -testexit="Automation Test Queue Empty" -log
```

The `FactoryTwin.SparkplugB.Live.*` tests skip with a warning rather than failing
when no broker is reachable, so the suite stays green without one.

## Watching traffic

The mosquitto client tools live inside the running container:

```bash
docker exec factorytwin-mosquitto mosquitto_sub -h localhost -t 'spBv1.0/#' -v
```

## Decoding a payload with the Eclipse Tahu reference implementation

This is the cross-check that proves the C++ encoder is wire-compatible: capture
what Unreal publishes, then decode it with the project's own
`Content/Python/sparkplug_b_pb2.py`.

```bash
# 1. Arm a one-shot capture (-N suppresses the newline mosquitto_sub would append)
docker exec -d factorytwin-mosquitto sh -c \
  "mosquitto_sub -h localhost -t 'spBv1.0/SMT_Line/NBIRTH/#' -C 1 -N > /tmp/cap.bin"

# 2. Publish from Unreal (run the live test, or PIE)

# 3. Pull the capture out
mkdir -p Tools/broker/_capture
docker cp factorytwin-mosquitto:/tmp/cap.bin Tools/broker/_capture/nbirth.bin

# 4. Decode
docker run --rm \
  -e PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python \
  -v "$PWD/Content/Python:/spb:ro" \
  -v "$PWD/Tools/broker/_capture:/data:ro" \
  -v "$PWD/Tools/broker/decode_capture.py:/decode.py:ro" \
  python:3.11-slim sh -c "pip install --quiet 'protobuf==3.20.3' && python /decode.py"
```

Two gotchas worth knowing:

- **`mosquitto_sub` appends a newline** unless you pass `-N`, which makes the
  payload one byte too long and protobuf reports "Truncated message".
- **`sparkplug_b_pb2.py` is pre-3.19 generated code.** Modern protobuf refuses to
  load it (`Descriptors cannot be created directly`). Pin `protobuf==3.20.3` and
  set `PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python`, or regenerate it with a
  current protoc.

## Verifying Last Will (NDEATH)

Covered automatically by `FactoryTwin.SparkplugB.Live.WillFiresOnUngracefulDisconnect`,
which connects a watcher, drops an edge node without sending DISCONNECT, and
asserts the broker synthesises the NDEATH publish with the right `bdSeq`. Its
companion test asserts a *clean* DISCONNECT does **not** fire the will.

To watch it by hand:

```bash
docker exec factorytwin-mosquitto mosquitto_sub -h localhost -t 'spBv1.0/SMT_Line/NDEATH/#' -v
```

then start PIE and kill the editor from Task Manager rather than closing it.

## `spb_dump.py`

A live subscriber that pretty-prints decoded payloads as they arrive. Needs
`paho-mqtt` as well, so run it the same containerised way:

```bash
docker run --rm --network host \
  -e PROTOCOL_BUFFERS_PYTHON_IMPLEMENTATION=python \
  -v "$PWD/Content/Python:/spb:ro" \
  -v "$PWD/Tools/broker/spb_dump.py:/spb_dump.py:ro" \
  python:3.11-slim sh -c "pip install --quiet 'protobuf==3.20.3' paho-mqtt && python /spb_dump.py --host host.docker.internal"
```
