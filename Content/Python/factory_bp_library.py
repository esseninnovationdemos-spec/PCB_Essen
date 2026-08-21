# Blueprint Function Library for UE 5.3.2 (Python 3.9).
# Put this file in <Project>/Content/Python/

import os
import sys
import unreal

_p = os.path.join(unreal.Paths.project_content_dir(), "Python")
if _p not in sys.path:
    sys.path.append(_p)


@unreal.uclass()
class FactoryBPLibrary(unreal.BlueprintFunctionLibrary):

    # ─── INIT WITH UI VALUES ────────────────────────────────────────────────
    @unreal.ufunction(static=True, ret=bool, params=[str, int, str, str])
    def factory_init(broker_host, broker_port, broker_username, broker_password):
        try:
            import factory_manager
            return bool(factory_manager.init(
                broker_host=broker_host,
                broker_port=int(broker_port),
                broker_username=broker_username,
                broker_password=broker_password,
            ))
        except Exception as e:
            unreal.log_error("factory_init: " + str(e))
            return False

    @unreal.ufunction(static=True, ret=bool, params=[str])
    def factory_tick(machine_name):
        try:
            import factory_manager
            return bool(factory_manager.tick(machine_name))
        except Exception as e:
            unreal.log_error("factory_tick: " + str(e))
            return False

    @unreal.ufunction(static=True, ret=bool, params=[str, str])
    def factory_publish_event(machine_name, event_type):
        try:
            import factory_manager
            return bool(factory_manager.publish_event(machine_name, event_type))
        except Exception as e:
            unreal.log_error("factory_publish_event: " + str(e))
            return False

    @unreal.ufunction(static=True)
    def factory_shutdown():
        try:
            import factory_manager
            factory_manager.shutdown()
        except Exception as e:
            unreal.log_error("factory_shutdown: " + str(e))

    @unreal.ufunction(static=True, ret=str)
    def factory_status():
        try:
            import factory_manager
            return factory_manager.get_status()
        except Exception as e:
            return "ERROR: " + str(e)

    # ─── NEW MATERIAL / LOT COMMANDS ────────────────────────────────────────

    @unreal.ufunction(static=True, ret=bool)
    def factory_check_new_material():
        """Poll for a pending new-material command from Ignition/NCMD.
        Returns True once per event, then resets. Call every tick.
        Use factory_get_lot_id() to retrieve the new lot identifier."""
        try:
            import factory_manager
            triggered, _ = factory_manager.pop_new_material_request()
            return bool(triggered)
        except Exception as e:
            unreal.log_error("factory_check_new_material: " + str(e))
            return False

    @unreal.ufunction(static=True, ret=str)
    def factory_get_lot_id():
        """Return the current active lot/batch identifier."""
        try:
            import factory_manager
            return factory_manager.get_current_lot()
        except Exception as e:
            unreal.log_error("factory_get_lot_id: " + str(e))
            return "ERROR"

    @unreal.ufunction(static=True, ret=bool, params=[str])
    def factory_start_new_lot(lot_id):
        """Programmatically start a new lot from Blueprint (e.g. UI button).
        Publishes NEW_MATERIAL DDATA event to all machines and updates lot ID."""
        try:
            import factory_manager
            factory_manager.start_new_lot(lot_id if lot_id else None)
            return True
        except Exception as e:
            unreal.log_error("factory_start_new_lot: " + str(e))
            return False

    # ─── ANIMATION PHASE SYNC ───────────────────────────────────────────────

    @unreal.ufunction(static=True, ret=bool, params=[str])
    def factory_start_cycle(machine_name):
        """Call when a machine animation starts.  Forces sim to RUNNING and
        publishes PHASE_STARTED so Ignition/ClickHouse record the state change."""
        try:
            import factory_manager
            return bool(factory_manager.machine_start_cycle(machine_name))
        except Exception as e:
            unreal.log_error("factory_start_cycle: " + str(e))
            return False

    @unreal.ufunction(static=True, ret=bool, params=[str])
    def factory_phase_complete(machine_name):
        """Call when a machine animation finishes one board/cycle.
        Publishes CYCLE_COMPLETE metrics then transitions sim to COOLDOWN → IDLE."""
        try:
            import factory_manager
            return bool(factory_manager.machine_cycle_complete(machine_name))
        except Exception as e:
            unreal.log_error("factory_phase_complete: " + str(e))
            return False

    @unreal.ufunction(static=True, ret=str, params=[str])
    def factory_get_state(machine_name):
        """Return current sim state for a machine: IDLE/WARMUP/RUNNING/COOLDOWN/FAULT/BLOCKED."""
        try:
            import factory_manager
            return factory_manager.get_machine_state(machine_name)
        except Exception as e:
            unreal.log_error("factory_get_state: " + str(e))
            return "ERROR"


unreal.log("FactoryBPLibrary registered")
