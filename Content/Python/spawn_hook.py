"""spawn_hook.py v4 — reflection-based function discovery."""
import unreal
import factory_manager

_frame = 0
_debug_dumped = False


def _get_world():
    try:
        w = unreal.EditorLevelLibrary.get_game_world()
        if w is not None:
            return w
    except Exception:
        pass
    try:
        return unreal.EditorLevelLibrary.get_editor_world()
    except Exception:
        return None


def _find_target():
    world = _get_world()
    if world is None:
        return None
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor)
    for a in actors:
        try:
            cn = a.get_class().get_name()
            if "Process_Manager" in cn or "Spawner_PCB" in cn:
                return a
        except Exception:
            continue
    return None


def _get_bp_functions(actor):
    """Use UE5 reflection to list ALL UFunctions on the class, including
    Blueprint-defined functions. This bypasses Python's dir()."""
    result = []
    cls = actor.get_class()

    # Iterate the class using reflection
    try:
        # Use unreal.EditorLoadingAndSavingUtils or similar - safer: dir(cls)
        klass_dir = [x for x in dir(cls) if not x.startswith("_")]
        result.append(("class_dir", klass_dir))
    except Exception as e:
        result.append(("class_dir_err", str(e)))

    # Try to enumerate components of the actor
    try:
        comps = actor.get_components_by_class(unreal.ActorComponent)
        for comp in comps:
            comp_cls = comp.get_class().get_name()
            comp_methods = [x for x in dir(comp) if not x.startswith("_") and callable(getattr(comp, x, None)) and x not in dir(unreal.ActorComponent)]
            if comp_methods:
                result.append(("comp:" + comp_cls, comp_methods))
    except Exception as e:
        result.append(("comp_err", str(e)))

    # Try find_function_by_name for common wrapper names
    tried = []
    for candidate in ("RequestSpawnPCB", "Request Spawn PCB", "request_spawn_pcb",
                      "SpawnPCB", "Spawn PCB", "spawn_pcb",
                      "SpawnNewPCB", "Spawn New PCB"):
        try:
            f = cls.find_function_by_name(candidate) if hasattr(cls, "find_function_by_name") else None
            if f is not None:
                tried.append((candidate, "FOUND"))
        except Exception as ex:
            tried.append((candidate, "err:" + str(ex)[:40]))
    result.append(("find_function_by_name", tried))

    return result


def _try_call(actor, method_name):
    """Try to invoke a method by name using getattr or call_method."""
    fn = getattr(actor, method_name, None)
    if callable(fn):
        fn()
        return True
    try:
        actor.call_method(method_name)
        return True
    except Exception:
        return False


def _fire_spawn():
    global _debug_dumped
    a = _find_target()
    if a is None:
        unreal.log_warning("[spawn_hook] no target actor")
        return
    unreal.log("[spawn_hook] target=%s class=%s" % (a.get_name(), a.get_class().get_name()))

    # Try common names directly first
    for name in ("request_spawn_pcb", "RequestSpawnPCB", "Request Spawn PCB",
                 "spawn_pcb", "SpawnPCB", "Spawn PCB",
                 "spawn_new_pcb", "SpawnNewPCB"):
        try:
            if _try_call(a, name):
                unreal.log("[spawn_hook] FIRED via %s()" % name)
                return
        except Exception as e:
            unreal.log_warning("[spawn_hook] %s failed: %s" % (name, e))

    if not _debug_dumped:
        _debug_dumped = True
        unreal.log("[spawn_hook] === DEEP INSPECTION ===")
        info = _get_bp_functions(a)
        for key, val in info:
            unreal.log("[spawn_hook] %s: %s" % (key, val))


def _on_slate_tick(dt):
    global _frame
    _frame += 1
    if _frame % 10 != 0:
        return
    try:
        triggered, _lot = factory_manager.pop_new_material_request()
        if triggered:
            _fire_spawn()
    except Exception as e:
        unreal.log_error("[spawn_hook] tick error: " + str(e))


_tick_handle = unreal.register_slate_post_tick_callback(_on_slate_tick)
unreal.log("[spawn_hook] registered v4 — reflection inspection")
