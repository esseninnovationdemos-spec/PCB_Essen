# ════════════════════════════════════════════════════════════════════════════
# sim_engine.py
#
#   Per-machine simulation engine.  Reads config, drives a state graph, and
#   generates realistic samples every tick().
#
#   STATE GRAPH (all machines follow this pattern):
#     IDLE     → WARMUP    (start cycle)
#     WARMUP   → RUNNING   (deterministic, after warmup_duration_sec)
#     RUNNING  → BLOCKED | COOLDOWN | FAULT   (weighted by state_distribution)
#     BLOCKED  → RUNNING (95%) | FAULT (5%)
#     COOLDOWN → IDLE      (deterministic, after cooldown_duration_sec)
#     FAULT    → COOLDOWN  (recovery)
#
#   Values are interpolated during WARMUP and COOLDOWN so temperatures /
#   RPMs / positions ramp instead of snapping between plateaus.
#
#   STATE CODES (matching the SMT line UNS legend)
#     0 = IDLE, 1 = RUNNING, 2 = FAULT, 3 = BLOCKED, 4 = WARMUP, 5 = COOLDOWN
#
#   DEGRADATION MODES (all independent, can compose)
#     fluctuating – gaussian noise around generated value
#     drift       – persistent linear drift of one metric's mean over hours
#     spike       – Poisson-distributed fault events that snap metric(s)
#                   to fault_value and optionally force a state change
# ════════════════════════════════════════════════════════════════════════════

import math
import random
import time
import logging

log = logging.getLogger(__name__)

STATE_CODE = {
    "IDLE":     0,
    "RUNNING":  1,
    "FAULT":    2,
    "BLOCKED":  3,
    "WARMUP":   4,
    "COOLDOWN": 5,
}

# Mean dwell (seconds) for states whose duration isn't fixed by config.
# RUNNING is intentionally huge: it exits only via force_state("COOLDOWN") from
# CYCLE_COMPLETE — never via an auto-timer.  That way the sim follows UE5.
_DEFAULT_DWELL = {"IDLE": 86400, "RUNNING": 86400, "BLOCKED": 12, "FAULT": 25}


class MachineSimulator:
    """One instance per machine.  Stateless callers; all state lives here."""

    def __init__(self, name, cfg):
        self.name        = name
        self.cfg         = cfg
        self.device_id   = cfg["device_id"]
        self.tick_dt     = float(cfg.get("tick_interval_sec", 1.0))
        self._trans_cfg  = cfg.get("transitions", {})

        # ─── state machine ────────────────────────────────────────────────
        now = time.time()
        self.state             = "IDLE"
        self.state_since       = now
        self._state_ends_at    = now + self._compute_dwell("IDLE")
        self._last_running_values = {}   # metric → last RUNNING sample (for COOLDOWN start)

        # ─── degradation ──────────────────────────────────────────────────
        self._drift_offset = 0.0
        self._last_tick    = now
        self._spike_until    = 0.0
        self._next_spike_at  = self._schedule_next_spike()

        # ─── cycle-based bookkeeping ──────────────────────────────────────
        self._cycle_started_at = None
        self._fail_counter     = 0

        # ─── motion / rotation / part-id state ────────────────────────────
        self._motion_t          = 0.0
        self._carrier_idx       = 0
        self._carrier_next_swap = now + cfg.get("carrier_dwell_sec", 12)
        self._part_idx          = 0

    # ══════════════════════════════════════════════════════════════════════
    # STATE GRAPH
    # ══════════════════════════════════════════════════════════════════════
    def _compute_dwell(self, state):
        """Time the machine stays in `state` before transitioning."""
        if state == "WARMUP":
            return float(self._trans_cfg.get("warmup_duration_sec", 15))
        if state == "COOLDOWN":
            return float(self._trans_cfg.get("cooldown_duration_sec", 15))
        if state == "RUNNING":
            fixed = self._trans_cfg.get("running_duration_sec")
            if fixed is not None:
                return float(fixed)
        mean = _DEFAULT_DWELL.get(state, 30)
        return random.expovariate(1.0 / mean)

    def _choose_next_from_running(self):
        """
        Pick the next state when leaving RUNNING. Uses the machine's
        state_distribution weights (excluding RUNNING itself) as a prior for
        BLOCKED / FAULT / COOLDOWN. IDLE weight rolls into COOLDOWN because
        the physical machine cannot skip the cool-down phase.
        """
        dist = dict(self.cfg.get("state_distribution", {}))
        w_cool  = dist.get("IDLE", 0.05) + dist.get("COOLDOWN", 0.0)
        w_block = dist.get("BLOCKED", 0.02)
        w_fault = dist.get("FAULT", 0.01)
        return self._weighted_choice(
            {"COOLDOWN": w_cool, "BLOCKED": w_block, "FAULT": w_fault}
        )

    def _enter_state(self, new_state, now):
        # Snapshot current running values so COOLDOWN can interpolate down.
        if self.state == "RUNNING" and new_state in ("COOLDOWN", "FAULT"):
            # values are captured by the outer tick() in _last_running_values
            pass
        self.state          = new_state
        self.state_since    = now
        self._state_ends_at = now + self._compute_dwell(new_state)
        log.debug("%s → state=%s (dwell=%.1fs)",
                  self.name, new_state,
                  self._state_ends_at - now)

    def _maybe_change_state(self, forced=None, now=None):
        now = now if now is not None else time.time()
        if forced is not None:
            self._enter_state(forced, now)
            return True
        # IDLE and RUNNING exit ONLY via force_state() from UE5 events.
        # Never auto-transition them — prevents phantom restarts from random dwell.
        if self.state in ("IDLE", "RUNNING"):
            return False
        if now < self._state_ends_at:
            return False

        # Timer expired – follow the graph
        curr = self.state
        if   curr == "WARMUP":   nxt = "RUNNING"
        elif curr == "BLOCKED":
            nxt = "FAULT" if random.random() < 0.05 else "RUNNING"
        elif curr == "COOLDOWN": nxt = "IDLE"
        elif curr == "FAULT":    nxt = "COOLDOWN"
        else:                    nxt = "IDLE"
        self._enter_state(nxt, now)
        return True

    def force_state(self, new_state, now=None):
        """Immediately transition to new_state, bypassing the dwell timer.
        Called by factory_manager when UE5 animation signals phase start/end."""
        now = now or time.time()
        if new_state == "RUNNING":
            self._last_running_values = {}
            self._cycle_started_at = now
        self._enter_state(new_state, now)
        log.debug("%s forced → %s", self.name, new_state)

    @staticmethod
    def _weighted_choice(weights_dict):
        items = [(k, w) for k, w in weights_dict.items() if w > 0]
        if not items:
            return "COOLDOWN"
        total = sum(w for _, w in items)
        r = random.uniform(0, total)
        acc = 0.0
        for name, w in items:
            acc += w
            if r <= acc:
                return name
        return items[-1][0]

    # ══════════════════════════════════════════════════════════════════════
    # VALUE GENERATION
    # ══════════════════════════════════════════════════════════════════════
    def _running_target(self, m_name, m_cfg):
        """Value a metric would take in steady RUNNING (before noise/drift/spike)."""
        nom_lo, nom_hi = m_cfg["nominal"]
        profile = m_cfg.get("motion_profile")
        if profile == "sine":
            mid = (nom_lo + nom_hi) / 2.0
            amp = (nom_hi - nom_lo) / 2.0
            phase = hash(m_name) % 100 / 100.0 * math.tau
            return mid + amp * math.sin(self._motion_t + phase)
        if profile == "sawtooth":
            cycle_t = (self._motion_t % math.tau) / math.tau
            if cycle_t < 0.3:
                return nom_hi
            if cycle_t < 0.5:
                return nom_lo + (nom_hi - nom_lo) * (1 - (cycle_t - 0.3) / 0.2)
            if cycle_t < 0.7:
                return nom_lo
            return nom_lo + (nom_hi - nom_lo) * ((cycle_t - 0.7) / 0.3)
        return random.uniform(nom_lo, nom_hi)

    def _ramp_progress(self, now, duration):
        if duration <= 0:
            return 1.0
        return max(0.0, min(1.0, (now - self.state_since) / duration))

    def _interpolate(self, start, end, progress, shape):
        if shape == "exponential":
            # Newton-of-cooling / heating: 95 % of the way there at progress=1
            k = 3.0
            return end - (end - start) * math.exp(-k * progress)
        return start + (end - start) * progress   # linear

    def _generate_metric_base(self, m_name, m_cfg, now):
        """Generate the 'physical' value for one metric based on current state."""
        state    = self.state
        idle     = m_cfg.get("idle_value", 0.0)
        fault    = m_cfg.get("fault_value", idle)
        nom_lo, nom_hi = m_cfg["nominal"]
        nom_mid  = (nom_lo + nom_hi) / 2.0

        # Which metrics ramp during transitions? Default: all thermal-shaped
        # metrics (those with distinct idle vs nominal). ramp_metrics wins.
        ramp_metrics    = self._trans_cfg.get("ramp_metrics")
        thermal_metrics = set(self._trans_cfg.get("thermal_metrics", []))
        ramps           = (ramp_metrics is None) or (m_name in ramp_metrics)
        shape = "exponential" if m_name in thermal_metrics \
                else self._trans_cfg.get("ramp_shape", "linear")

        if state == "IDLE":
            return idle
        if state == "BLOCKED":
            # Blocked = stopped but still warm / loaded → freeze at last running
            return self._last_running_values.get(m_name, idle)
        if state == "FAULT":
            return fault
        if state == "WARMUP":
            if not ramps:
                return idle
            target = m_cfg.get("warmup_value", nom_mid)
            # Use warmup_value as the plateau this ramp is aiming for.
            # If the metric has no warmup_value we head straight for nominal.
            end = target if "warmup_value" in m_cfg else nom_mid
            p = self._ramp_progress(now,
                self._trans_cfg.get("warmup_duration_sec", 15))
            return self._interpolate(idle, end, p, shape)
        if state == "COOLDOWN":
            if not ramps:
                return idle
            start = self._last_running_values.get(m_name, nom_mid)
            p = self._ramp_progress(now,
                self._trans_cfg.get("cooldown_duration_sec", 15))
            return self._interpolate(start, idle, p, shape)

        # RUNNING (default)
        return self._running_target(m_name, m_cfg)

    # ══════════════════════════════════════════════════════════════════════
    # DEGRADATION
    # ══════════════════════════════════════════════════════════════════════
    def _apply_fluctuating(self, metric_name, value):
        deg = self.cfg.get("degradation", {}).get("fluctuating", {})
        if not deg.get("enabled") or metric_name not in deg.get("applies_to", []):
            return value
        noise_pct = deg.get("noise_pct", 0.05)
        return value + random.gauss(0, abs(value) * noise_pct)

    def _apply_drift(self, metric_name, value):
        deg = self.cfg.get("degradation", {}).get("drift", {})
        if not deg.get("enabled") or metric_name != deg.get("metric"):
            return value
        return value + self._drift_offset

    def _accumulate_drift(self, dt_sec):
        deg = self.cfg.get("degradation", {}).get("drift", {})
        if not deg.get("enabled"):
            return
        if self.state != "RUNNING" and not deg.get("accumulate_when_idle"):
            return
        rate_per_sec = deg.get("rate_per_hour", 0.0) / 3600.0
        self._drift_offset += rate_per_sec * dt_sec
        cap = deg.get("max_drift")
        if cap is not None:
            self._drift_offset = min(self._drift_offset, cap) if cap > 0 \
                                 else max(self._drift_offset, cap)

    def _schedule_next_spike(self):
        deg = self.cfg.get("degradation", {}).get("spike", {})
        if not deg.get("enabled"):
            return float("inf")
        mean_sec = deg.get("mean_minutes_between", 60) * 60.0
        return time.time() + random.expovariate(1.0 / mean_sec)

    def _check_spike(self, now):
        deg = self.cfg.get("degradation", {}).get("spike", {})
        if not deg.get("enabled"):
            return None
        if now < self._spike_until:
            return deg
        if self._spike_until and now >= self._spike_until:
            self._spike_until   = 0.0
            self._next_spike_at = self._schedule_next_spike()
            log.info("%s spike ended", self.name)
        if now >= self._next_spike_at:
            duration = deg.get("duration_sec", 5)
            self._spike_until = now + duration
            forced = deg.get("forces_state")
            if forced:
                self._maybe_change_state(forced=forced, now=now)
            log.info("%s spike triggered (%.1fs)", self.name, duration)
            return deg
        return None

    def _apply_spike(self, metric_name, value, now):
        if now >= self._spike_until:
            return value
        deg = self.cfg["degradation"]["spike"]
        if metric_name not in deg.get("metrics", []):
            return value
        m_cfg = self.cfg["metrics"][metric_name]
        target = deg.get("spike_to", "fault_value")
        if target == "fault_value":
            return m_cfg.get("fault_value", value)
        if target == "absolute_max":
            return m_cfg["absolute"][1]
        if target == "absolute_min":
            return m_cfg["absolute"][0]
        return value

    # ══════════════════════════════════════════════════════════════════════
    # PUBLIC: tick / cycle_complete
    # ══════════════════════════════════════════════════════════════════════
    def tick(self, current_time=None):
        """
        One simulation step.  Returns a dict:
          { "state": ..., "state_code": ..., "metrics": {...},
            "event": None, "extras": {...}, "fail_counter": ... }
        """
        now = current_time or time.time()
        dt  = now - self._last_tick
        self._last_tick = now

        # Advance motion clock (RUNNING only – frozen otherwise)
        if self.state == "RUNNING":
            self._motion_t += max(dt, self.tick_dt) * 0.6

        prev_state = self.state
        self._maybe_change_state(now=now)
        self._accumulate_drift(dt)
        self._check_spike(now)

        out_metrics = {}
        for m_name, m_cfg in self.cfg["metrics"].items():
            if m_cfg.get("publish_on") == "cycle_complete":
                continue
            base = self._generate_metric_base(m_name, m_cfg, now)
            v = self._apply_drift(m_name, base)
            v = self._apply_fluctuating(m_name, v)
            v = self._apply_spike(m_name, v, now)
            abs_lo, abs_hi = m_cfg["absolute"]
            v = max(abs_lo, min(abs_hi, v))
            out_metrics[m_name] = round(v, 3)
            # Remember RUNNING values so a subsequent COOLDOWN can ramp down
            # from the actual last value, not a random point in the band.
            if self.state == "RUNNING":
                self._last_running_values[m_name] = v

        # Extras
        extras = {}
        if "carrier_ids" in self.cfg:
            if now >= self._carrier_next_swap:
                self._carrier_idx = (self._carrier_idx + 1) % len(self.cfg["carrier_ids"])
                self._carrier_next_swap = now + self.cfg.get("carrier_dwell_sec", 12)
            extras["conveyor_id"] = self.cfg["carrier_ids"][self._carrier_idx]

        if "part_ids" in self.cfg and self.state == "RUNNING":
            extras["current_part_id"] = self.cfg["part_ids"][self._part_idx]

        event = "STATE_CHANGED" if self.state != prev_state else None

        return {
            "state":         self.state,
            "state_code":    STATE_CODE[self.state],
            "metrics":       out_metrics,
            "event":         event,
            "extras":        extras,
            "fail_counter":  self._fail_counter,
        }

    def cycle_complete(self):
        """
        Emitted by the game loop when a board finishes a cycle-based process.
        Generates cycle metrics regardless of current state so CYCLE_COMPLETE
        always publishes meaningful data even if timing is slightly off.
        """
        out = {"metrics": {}, "extras": {}, "event": "CYCLE_COMPLETE"}

        for m_name, m_cfg in self.cfg["metrics"].items():
            if m_cfg.get("publish_on") != "cycle_complete":
                continue
            nom_lo, nom_hi = m_cfg["nominal"]
            v = random.uniform(nom_lo, nom_hi)
            v = self._apply_drift(m_name, v)
            v = self._apply_fluctuating(m_name, v)
            abs_lo, abs_hi = m_cfg["absolute"]
            v = max(abs_lo, min(abs_hi, v))
            out["metrics"][m_name] = round(v, 3)

        if "fail_rate" in self.cfg:
            failed = random.random() < self.cfg["fail_rate"]
            out["extras"]["inspection_result"] = "FAIL" if failed else "PASS"
            if failed:
                self._fail_counter += 1
            out["extras"]["fail_counter"] = self._fail_counter

        if "part_ids" in self.cfg:
            self._part_idx = (self._part_idx + 1) % len(self.cfg["part_ids"])
            out["extras"]["part_id"] = self.cfg["part_ids"][self._part_idx]

        return out
