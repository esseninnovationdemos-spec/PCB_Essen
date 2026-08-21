# ════════════════════════════════════════════════════════════════════════════
# refresh_factory_nodes.py
#
#   Fixes the editor-startup race where Blueprints that call FactoryBPLibrary
#   nodes (exposed from Python in factory_bp_library.py) come up flagged with
#   "node cannot be compiled / function not found" errors — the thing you were
#   fixing by opening each Blueprint and hitting "Refresh All Nodes".
#
#   Root cause
#   ----------
#   FactoryBPLibrary is a UBlueprintFunctionLibrary *generated at runtime* by
#   Python (@unreal.uclass in factory_bp_library.py). Its UFunctions
#   (factory_init, factory_tick, factory_phase_complete, ...) only exist in
#   Unreal's reflection system AFTER init_unreal.py imports factory_bp_library.
#   But at editor launch the startup map (level2) and the Blueprints placed in
#   it are loaded/compiled BEFORE that Python registration finishes, so their
#   K2Node_CallFunction nodes cannot resolve the factory_* functions and are
#   marked as errors. Doing "Refresh All Nodes" by hand only works because by
#   then the class is registered.
#
#   Fix
#   ---
#   Once the editor and Python have BOTH finished starting up, reload the
#   affected Blueprint packages from disk. FactoryBPLibrary is registered by
#   then, so every node re-resolves correctly on reload — the automated,
#   non-dirtying equivalent of the manual "Refresh All Nodes + Compile".
#   Runs exactly once per editor session, then unregisters itself.
#
#   To disable: remove the "import refresh_factory_nodes" line from
#   init_unreal.py.
# ════════════════════════════════════════════════════════════════════════════

import unreal

# Blueprints that reference FactoryBPLibrary python nodes (package paths, no
# extension / no _C). Keep this list in sync with factory_bp_library.py's
# consumers — a quick way to regenerate it:
#   grep -rla factory_ Content --include=*.uasset
_TARGET_BLUEPRINTS = (
    "/Game/BP_MQTT_Manager",
    "/Game/Spawner_PCB_BP",
    "/Game/UI/W_UI",
    "/Game/SMT-Workcenter/AutomaticOpticalInspection/AutomaticOpticalInspection_BP",
    "/Game/SMT-Workcenter/ComponentPlacer/Components_Placer_BP",
    "/Game/SMT-Workcenter/LaserMarking/Machine_LaserMarking_BP",
    "/Game/SMT-Workcenter/ReflowOven/ReflowOven_BP",
    "/Game/SMT-Workcenter/SolderPasteInspection/Solder_Inspection_BP",
)

_SETTLE_TICKS = 8       # wait at least this many editor frames before reloading
_TIMEOUT_TICKS = 600    # ...but never wait forever
_state = {"frame": 0, "handle": None}


def _editor_ready():
    """True once the startup map has finished loading."""
    try:
        return unreal.EditorLevelLibrary.get_editor_world() is not None
    except Exception:
        return False


def _reload_targets():
    packages = []
    for path in _TARGET_BLUEPRINTS:
        try:
            pkg = unreal.load_package(path)
            if pkg is not None:
                packages.append(pkg)
            else:
                unreal.log_warning("[refresh_factory_nodes] package not found: %s" % path)
        except Exception as e:
            unreal.log_error("[refresh_factory_nodes] load %s -> %s" % (path, e))

    if not packages:
        unreal.log_warning("[refresh_factory_nodes] nothing to reload")
        return

    try:
        # ASSUME_POSITIVE = non-interactive; reload re-reads each asset from disk
        # with FactoryBPLibrary now registered, so the factory_* nodes resolve.
        reloaded, err = unreal.EditorLoadingAndSavingUtils.reload_packages(
            packages,
            unreal.ReloadPackagesInteractionMode.ASSUME_POSITIVE,
        )
        detail = (" | " + str(err)) if err and str(err) else ""
        unreal.log("[refresh_factory_nodes] reloaded %d Blueprint package(s) "
                   "(changed=%s)%s" % (len(packages), reloaded, detail))
    except Exception as e:
        unreal.log_error("[refresh_factory_nodes] reload_packages failed: %s" % e)

    # No-op unless a Blueprint tab is already open, but harmless and cheap.
    try:
        unreal.BlueprintEditorLibrary.refresh_all_open_blueprint_editors()
    except Exception:
        pass


def _on_tick(delta_seconds):
    _state["frame"] += 1
    ready = _state["frame"] >= _SETTLE_TICKS and _editor_ready()
    timed_out = _state["frame"] >= _TIMEOUT_TICKS
    if not (ready or timed_out):
        return

    # Unregister FIRST so this runs exactly once, even if the reload throws.
    handle = _state["handle"]
    _state["handle"] = None
    if handle is not None:
        try:
            unreal.unregister_slate_post_tick_callback(handle)
        except Exception:
            pass

    unreal.log("[refresh_factory_nodes] refreshing %d Blueprint(s) that use "
               "Python nodes" % len(_TARGET_BLUEPRINTS))
    _reload_targets()


_state["handle"] = unreal.register_slate_post_tick_callback(_on_tick)
unreal.log("[refresh_factory_nodes] scheduled (waiting for editor + Python to finish startup)")
