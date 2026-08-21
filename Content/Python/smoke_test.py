# Headless smoke test: runs factory_manager against the real broker for a
# short window, ticks CONVEYOR + REFLOW_OVEN, and dumps state transitions
# and metric samples so we can see the WARMUP / RUNNING / COOLDOWN ramps
# behave as designed.
#
# Run on sim-pc:  python3 -m smoke_test
import sys, os, time, logging
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
logging.basicConfig(level=logging.INFO, format="%(asctime)s %(levelname)s %(name)s: %(message)s")

import factory_manager as fm
from sim_engine import STATE_CODE

# Speed things up: shorten the reflow oven warmup so the test finishes fast.
# We patch the loaded TOML after init() by reaching into the running sim.
fm.init()

# Force reflow oven to WARMUP so we can watch a full ramp
if "REFLOW_OVEN" in fm._simulators:
    sim = fm._simulators["REFLOW_OVEN"]
    # Shrink for the test:
    sim._trans_cfg["warmup_duration_sec"]   = 20
    sim._trans_cfg["cooldown_duration_sec"] = 15
    sim._enter_state("WARMUP", time.time())

if "CONVEYOR" in fm._simulators:
    sim = fm._simulators["CONVEYOR"]
    sim._trans_cfg["warmup_duration_sec"]   = 5
    sim._trans_cfg["cooldown_duration_sec"] = 8
    sim._enter_state("WARMUP", time.time())

print(fm.get_status())

last_state = {}
for i in range(60):        # 60 ticks × 1s = 60s
    for m in ("CONVEYOR", "REFLOW_OVEN"):
        if m not in fm._simulators:
            continue
        sim = fm._simulators[m]
        sample = sim.tick()
        # Publish through factory_manager so the broker sees real DDATA.
        fm._publish_ddata(m, sim, sample)
        if last_state.get(m) != sample["state"]:
            print(f"[{i:2d}s] {m:12s} state={sample['state']}")
            last_state[m] = sample["state"]
        # Print a compact metrics line for the oven so we can see the ramp
        if m == "REFLOW_OVEN":
            metrics = sample["metrics"]
            print(f"      REFLOW_OVEN oven_temp_c={metrics.get('oven_temp_c'):>6}  "
                  f"cooling_temp_c={metrics.get('cooling_temp_c'):>6}  "
                  f"state={sample['state']}")

    # After 25s force cooldown to see the ramp back down
    if i == 25 and "REFLOW_OVEN" in fm._simulators:
        fm._simulators["REFLOW_OVEN"]._enter_state("COOLDOWN", time.time())
    if i == 12 and "CONVEYOR" in fm._simulators:
        fm._simulators["CONVEYOR"]._enter_state("COOLDOWN", time.time())

    time.sleep(1.0)

fm.shutdown()
print("done")
