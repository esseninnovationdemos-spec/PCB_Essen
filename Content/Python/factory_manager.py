# ════════════════════════════════════════════════════════════════════════════
# factory_manager.py   (UE 5.3 / Python 3.9 compatible)
# ════════════════════════════════════════════════════════════════════════════

import os
import logging
import threading
import time
import uuid

try:
    import tomllib
except ImportError:
    try:
        import tomli as tomllib
    except ImportError:
        raise ImportError(
            "Install tomli into UE's Python: "
            '"<UE_PATH>\\Engine\\Binaries\\ThirdParty\\Python3\\Win64\\python.exe" '
            "-m pip install tomli"
        )

import paho.mqtt.client as mqtt
import sparkplug_b_pb2 as pb
from spb_codec  import SpbCodec
from sim_engine import MachineSimulator, STATE_CODE

log = logging.getLogger("factory_manager")

# ─── singleton state ────────────────────────────────────────────────────────
_cfg                   = None
_client                = None
_codec                 = None
_simulators            = {}
_aliases               = {}
_initialized           = False
_lock                  = threading.Lock()
_stop_event            = threading.Event()
_auto_tick_next        = {}   # machine_name → next scheduled tick timestamp

# ─── new-material command state ─────────────────────────────────────────────
_new_material_pending  = False          # set True when NCMD arrives
_new_material_callback = None           # optional callable registered by UE5
_current_lot_id        = "LOT-INIT"    # active lot/batch identifier
_nm_lock               = threading.Lock()


# ════════════════════════════════════════════════════════════════════════════
# BACKGROUND TICK
# ════════════════════════════════════════════════════════════════════════════

def _auto_tick_loop():
    """Background thread: autonomously ticks all non-event-based machines
    at their configured tick_interval_sec so DDATA flows without UE5 calling
    factory_tick() every frame."""
    global _auto_tick_next
    while not _stop_event.is_set():
        now = time.time()
        try:
            for m_name, sim in list(_simulators.items()):
                # IDLE: nothing meaningful to publish for any machine type.
                if sim.state == "IDLE":
                    continue
                # tick_dt == 0 → event-only machine (LINE); skip auto-tick.
                if sim.tick_dt <= 0:
                    continue
                if now >= _auto_tick_next.get(m_name, 0):
                    sample = sim.tick(now)
                    if _initialized and _client:
                        # Always stamp event_type with the current state so
                        # Ignition always reflects WARMUP / RUNNING / COOLDOWN
                        # rather than keeping the last explicit event string.
                        _publish_ddata(m_name, sim, sample, event_type=sim.state)
                    _auto_tick_next[m_name] = now + sim.tick_dt
        except Exception as e:
            log.error("_auto_tick_loop error: %s", e)
        _stop_event.wait(timeout=0.05)   # 20 Hz loop, wakes immediately on shutdown


# ════════════════════════════════════════════════════════════════════════════
# PUBLIC API
# ════════════════════════════════════════════════════════════════════════════

def init(config_path=None,
         broker_host=None, broker_port=None,
         broker_username=None, broker_password=None):
    """
    Connect, send NBIRTH, register all devices.  UI-supplied broker creds
    override the [broker] section of the TOML.
    """
    global _cfg, _client, _codec, _simulators, _aliases, _initialized
    global _current_lot_id

    with _lock:
        if _initialized:
            log.warning("init() called twice - call shutdown() first")
            return True

        path = config_path or os.path.join(
            os.path.dirname(__file__), "factory_config.toml"
        )
        with open(path, "rb") as f:
            _cfg = tomllib.load(f)

        if broker_host is not None:
            _cfg["broker"]["host"] = broker_host
        if broker_port is not None:
            _cfg["broker"]["port"] = int(broker_port)
        if broker_username is not None:
            _cfg["broker"]["username"] = broker_username
        if broker_password is not None:
            _cfg["broker"]["password"] = broker_password

        logging.basicConfig(
            level=_cfg["simulation"].get("log_level", "INFO"),
            format="%(asctime)s [%(name)s] %(levelname)s: %(message)s",
        )
        log.info("Loaded config from %s", path)

        spb = _cfg["sparkplug"]
        _codec = SpbCodec(
            group_id     = spb["group_id"],
            edge_node_id = spb["edge_node_id"],
            namespace    = spb.get("namespace", "spBv1.0"),
        )

        _simulators = {}
        _aliases    = {}
        next_alias  = 1
        for m_name, m_cfg in _cfg["machines"].items():
            if not m_cfg.get("enabled", True):
                continue
            _simulators[m_name] = MachineSimulator(m_name, m_cfg)
            _aliases[m_cfg["device_id"]] = {}
            for metric_name in m_cfg["metrics"]:
                _aliases[m_cfg["device_id"]][metric_name] = next_alias
                next_alias += 1
            for extra in ("state_code", "inspection_result", "fail_counter",
                          "conveyor_id", "current_part_id", "part_id", "event_type"):
                _aliases[m_cfg["device_id"]][extra] = next_alias
                next_alias += 1

        bd_seq  = _codec.next_bd_seq()
        broker  = _cfg["broker"]
        _client = mqtt.Client(
            client_id     = broker.get("client_id", "ue5_smt_simulator"),
            protocol      = mqtt.MQTTv311,
            clean_session = True,
        )
        if broker.get("username"):
            _client.username_pw_set(broker["username"], broker.get("password", ""))

        ndeath_topic   = _codec.topic("NDEATH")
        ndeath_payload = _codec.ndeath_payload(bd_seq=bd_seq)
        _client.will_set(ndeath_topic, ndeath_payload, qos=1, retain=False)

        _client.on_connect    = _on_connect
        _client.on_disconnect = _on_disconnect
        _client.on_message    = _on_message

        _client.connect(broker["host"], broker["port"],
                        keepalive=broker.get("keepalive_sec", 60))
        _client.loop_start()

        _codec.reset_seq()
        nbirth_metrics = [
            {"name": "Properties/SimulatorVersion", "type": "String", "value": "1.0.0"},
            {"name": "Properties/UE5_Project",      "type": "String", "value": "SMT_Cluj"},
        ]
        _client.publish(
            _codec.topic("NBIRTH"),
            _codec.nbirth_payload(nbirth_metrics, bd_seq=bd_seq),
            qos=1, retain=False,
        )
        log.info("NBIRTH sent (bdSeq=%d)", bd_seq)

        for m_name, sim in _simulators.items():
            _publish_dbirth(m_name, sim)

        _current_lot_id = "LOT-%s" % time.strftime("%Y%m%d-%H%M%S")
        _initialized = True

        _stop_event.clear()
        t = threading.Thread(target=_auto_tick_loop, daemon=True, name="factory-auto-tick")
        t.start()

        log.info("Factory manager initialised: %d devices  lot=%s",
                 len(_simulators), _current_lot_id)
        return True


def tick(machine_name):
    """No-op: state machine and publishing are driven solely by the
    auto-tick background thread.  UE5 can call this at 60 fps without
    any side-effects — it exists only for Blueprint compatibility."""
    return _initialized and machine_name in _simulators


def publish_event(machine_name, event_type="CYCLE_COMPLETE"):
    if not _initialized:
        log.error("publish_event called before init()")
        return False
    sim = _simulators.get(machine_name)
    if not sim:
        log.error("Unknown machine: %s", machine_name)
        return False
    if event_type == "PHASE_STARTED":
        # Force WARMUP — sim ramps up metrics, auto-transitions to RUNNING
        sim.force_state("WARMUP")
        _publish_ddata(machine_name, sim, {"metrics": {}, "extras": {}},
                       event_type=event_type)
    elif event_type in ("CYCLE_STARTED", "PHASE_RUNNING"):
        # Force RUNNING so auto-tick generates and publishes real metric values
        sim.force_state("RUNNING")
        sample = sim.tick()
        _publish_ddata(machine_name, sim, sample, event_type=event_type)
    elif event_type == "CYCLE_COMPLETE":
        # Publish cycle metrics then ramp down — auto-transitions to IDLE
        evt = sim.cycle_complete()
        _publish_ddata(machine_name, sim, evt, event_type=event_type)
        sim.force_state("COOLDOWN")
    else:
        _publish_ddata(machine_name, sim, {"metrics": {}, "extras": {}},
                       event_type=event_type)
    return True


def set_new_material_callback(fn):
    """Register a callable invoked when an NCMD new_material=True arrives.
    fn receives one positional argument: the new lot_id string.
    Call with fn=None to clear."""
    global _new_material_callback
    with _nm_lock:
        _new_material_callback = fn
    log.info("new_material callback %s", "registered" if fn else "cleared")


def pop_new_material_request():
    """Return (True, lot_id) once if a new-material NCMD arrived, then reset.
    UE5 tick can poll this instead of registering a callback."""
    global _new_material_pending
    with _nm_lock:
        if _new_material_pending:
            _new_material_pending = False
            return True, _current_lot_id
    return False, _current_lot_id


def start_new_lot(lot_id=None):
    """Programmatically start a new lot (same action as NCMD new_material).
    Can be called directly from UE5 Blueprint via factory_bp_library."""
    global _current_lot_id
    with _nm_lock:
        _current_lot_id = lot_id or ("LOT-%s" % time.strftime("%Y%m%d-%H%M%S"))
    _do_new_material(_current_lot_id)


def get_current_lot():
    """Return the active lot/batch identifier."""
    return _current_lot_id


def machine_start_cycle(machine_name):
    """UE5 calls when a machine animation begins.  Forces sim to RUNNING and
    publishes a PHASE_STARTED DDATA so Ignition/ClickHouse see the state change."""
    if not _initialized:
        return False
    sim = _simulators.get(machine_name)
    if not sim:
        log.error("machine_start_cycle: unknown machine %s", machine_name)
        return False
    sim.force_state("RUNNING")
    sample = sim.tick()
    _publish_ddata(machine_name, sim, sample, event_type="PHASE_STARTED")
    log.info("%s → RUNNING (UE5 animation start)", machine_name)
    return True


def machine_cycle_complete(machine_name):
    """UE5 calls when a machine animation finishes one board/cycle.
    Publishes CYCLE_COMPLETE metrics then sends sim into COOLDOWN → IDLE."""
    if not _initialized:
        return False
    sim = _simulators.get(machine_name)
    if not sim:
        log.error("machine_cycle_complete: unknown machine %s", machine_name)
        return False
    evt = sim.cycle_complete()
    _publish_ddata(machine_name, sim, evt, event_type="CYCLE_COMPLETE")
    sim.force_state("COOLDOWN")
    log.info("%s CYCLE_COMPLETE → COOLDOWN (UE5 animation end)", machine_name)
    return True


def get_machine_state(machine_name):
    """Return the current state string for a machine (IDLE/WARMUP/RUNNING/…)."""
    if not _initialized:
        return "NOT_INITIALIZED"
    sim = _simulators.get(machine_name)
    return sim.state if sim else "UNKNOWN"


def shutdown():
    global _initialized
    with _lock:
        if not _initialized:
            return
        for m_name, sim in _simulators.items():
            try:
                _client.publish(
                    _codec.topic("DDEATH", device=sim.device_id),
                    _codec.ddeath_payload(sim.device_id),
                    qos=1,
                )
            except Exception as e:
                log.warning("DDEATH failed for %s: %s", m_name, e)
        try:
            _client.publish(
                _codec.topic("NDEATH"),
                _codec.ndeath_payload(bd_seq=_codec._bd_seq - 1),
                qos=1,
            )
        except Exception:
            pass
        _stop_event.set()
        _client.loop_stop()
        _client.disconnect()
        _initialized = False
        log.info("Shutdown complete")


def get_status():
    if not _initialized:
        return "NOT INITIALIZED"
    lines = [
        "Broker  : %s:%d" % (_cfg["broker"]["host"], _cfg["broker"]["port"]),
        "Group   : %s"    % _cfg["sparkplug"]["group_id"],
        "Edge    : %s"    % _cfg["sparkplug"]["edge_node_id"],
        "Lot     : %s"    % _current_lot_id,
        "Devices : %d"    % len(_simulators),
    ]
    for m_name, sim in _simulators.items():
        lines.append("  - %-20s state=%s" % (sim.device_id, sim.state))
    return "\n".join(lines)


# ════════════════════════════════════════════════════════════════════════════
# INTERNAL — MQTT callbacks
# ════════════════════════════════════════════════════════════════════════════

def _on_connect(client, userdata, flags, rc):
    if rc == 0:
        # Subscribe to Node Command topic for this edge node
        ncmd_topic = _codec.topic("NCMD")
        client.subscribe(ncmd_topic, qos=1)
        log.info("MQTT connected — subscribed to %s", ncmd_topic)
    else:
        log.error("MQTT connect failed rc=%d", rc)


def _on_disconnect(client, userdata, rc):
    if rc != 0:
        log.warning("MQTT disconnected unexpectedly rc=%d", rc)


def _on_message(client, userdata, msg):
    """Handle incoming NCMD (Node Command) messages."""
    try:
        payload = pb.Payload()
        payload.ParseFromString(msg.payload)
        for metric in payload.metrics:
            name = metric.name
            if name == "new_material" and metric.boolean_value:
                log.info("NCMD: new_material received")
                # Generate new lot ID from timestamp
                new_lot = "LOT-%s" % time.strftime("%Y%m%d-%H%M%S")
                _do_new_material(new_lot)
            elif name == "Node Control/Rebirth":
                # Standard SpB rebirth — re-send NBIRTH + all DBIRTHs
                log.info("NCMD: Rebirth requested")
                _do_rebirth()
    except Exception as e:
        log.error("_on_message error: %s", e)


# ════════════════════════════════════════════════════════════════════════════
# INTERNAL — business logic
# ════════════════════════════════════════════════════════════════════════════

def _do_new_material(lot_id):
    """Apply new-material: update lot ID, notify UE5, publish DDATA event."""
    global _new_material_pending, _current_lot_id
    with _nm_lock:
        _current_lot_id       = lot_id
        _new_material_pending = True
        cb = _new_material_callback

    log.info("New material started: lot=%s", lot_id)

    # Publish DDATA event_type=NEW_MATERIAL on all machines so ClickHouse records it
    if _initialized and _client:
        for m_name, sim in _simulators.items():
            _publish_ddata(m_name, sim,
                           {"metrics": {}, "extras": {"current_part_id": lot_id}},
                           event_type="NEW_MATERIAL")

    # Fire UE5 callback on a daemon thread so MQTT loop isn't blocked
    if cb is not None:
        t = threading.Thread(target=cb, args=(lot_id,), daemon=True)
        t.start()


def _do_rebirth():
    """Re-send NBIRTH and all DBIRTHs."""
    if not _initialized:
        return
    _codec.reset_seq()
    bd_seq = _codec.next_bd_seq()
    nbirth_metrics = [
        {"name": "Properties/SimulatorVersion", "type": "String", "value": "1.0.0"},
        {"name": "Properties/UE5_Project",      "type": "String", "value": "SMT_Cluj"},
    ]
    _client.publish(
        _codec.topic("NBIRTH"),
        _codec.nbirth_payload(nbirth_metrics, bd_seq=bd_seq),
        qos=1, retain=False,
    )
    for m_name, sim in _simulators.items():
        _publish_dbirth(m_name, sim)


def _publish_dbirth(machine_name, sim):
    sample    = sim.tick()
    extras    = sample.get("extras", {})
    device_id = sim.device_id
    metrics   = []

    for m_name, m_cfg in sim.cfg["metrics"].items():
        metrics.append({
            "name":  m_name,
            "type":  m_cfg["type"],
            "alias": _aliases[device_id][m_name],
            "value": sample["metrics"].get(m_name, 0.0),
        })

    metrics.append({
        "name":  "state_code", "type": "Int32",
        "alias": _aliases[device_id]["state_code"],
        "value": sample["state_code"],
    })
    if "fail_rate" in sim.cfg:
        metrics.append({"name": "inspection_result", "type": "String",
                        "alias": _aliases[device_id]["inspection_result"],
                        "value": "PASS"})
        metrics.append({"name": "fail_counter", "type": "Int32",
                        "alias": _aliases[device_id]["fail_counter"],
                        "value": 0})
    if "conveyor_id" in extras:
        metrics.append({"name": "conveyor_id", "type": "String",
                        "alias": _aliases[device_id]["conveyor_id"],
                        "value": extras["conveyor_id"]})
    if "current_part_id" in extras:
        metrics.append({"name": "current_part_id", "type": "String",
                        "alias": _aliases[device_id]["current_part_id"],
                        "value": extras["current_part_id"]})

    for m_name, m_cfg in sim.cfg["metrics"].items():
        if m_cfg.get("publish_on") == "cycle_complete":
            nom_lo, nom_hi = m_cfg["nominal"]
            metrics.append({
                "name":  m_name,
                "type":  m_cfg["type"],
                "alias": _aliases[device_id][m_name],
                "value": (nom_lo + nom_hi) / 2.0,
            })

    metrics.append({"name": "event_type", "type": "String",
                    "alias": _aliases[device_id]["event_type"],
                    "value": "IDLE"})

    _client.publish(
        _codec.topic("DBIRTH", device=device_id),
        _codec.dbirth_payload(device_id, metrics),
        qos=1, retain=False,
    )
    log.info("DBIRTH %s (%d metrics)", device_id, len(metrics))


def _publish_ddata(machine_name, sim, sample, event_type=None):
    # DDATA metrics carry name + alias. The alias alone would satisfy the
    # Sparkplug B spec (§ 5.4) once DBIRTH has established the map, but the
    # in-cluster SpB → ClickHouse bridge fans-out per metric using the
    # payload's name field, so dropping it stops ingest. Send both.
    device_id   = sim.device_id
    metrics_out = []

    for m_name, value in sample.get("metrics", {}).items():
        m_cfg = sim.cfg["metrics"][m_name]
        metrics_out.append({
            "name":  m_name,
            "type":  m_cfg["type"],
            "alias": _aliases[device_id][m_name],
            "value": value,
        })

    if "state_code" in sample:
        metrics_out.append({
            "name":  "state_code", "type": "Int32",
            "alias": _aliases[device_id]["state_code"],
            "value": sample["state_code"],
        })

    extras = sample.get("extras", {})
    for key in ("conveyor_id", "current_part_id", "inspection_result", "part_id"):
        if key in extras:
            metrics_out.append({"name": key, "type": "String",
                                "alias": _aliases[device_id][key],
                                "value": extras[key]})
    if "fail_counter" in extras:
        metrics_out.append({"name": "fail_counter", "type": "Int32",
                            "alias": _aliases[device_id]["fail_counter"],
                            "value": extras["fail_counter"]})

    if event_type:
        metrics_out.append({"name": "event_type", "type": "String",
                            "alias": _aliases[device_id]["event_type"],
                            "value": event_type})

    _client.publish(
        _codec.topic("DDATA", device=device_id),
        _codec.ddata_payload(device_id, metrics_out),
        qos=0, retain=False,
    )
