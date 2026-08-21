# ════════════════════════════════════════════════════════════════════════════
# init_unreal.py
#
#   Unreal automatically runs this file at editor startup if it sits in
#   <Project>/Content/Python/.  We use it to import factory_bp_library,
#   which registers the Blueprint nodes.
#
#   To verify it ran: after the editor finishes loading, open
#   Window → Output Log and look for the line:
#       [FactoryBP] FactoryBPLibrary registered – nodes available …
# ════════════════════════════════════════════════════════════════════════════

import unreal

try:
    import factory_bp_library         # registers the BP nodes
    unreal.log("[init_unreal] Factory simulator Blueprint nodes loaded")
except Exception as e:
    unreal.log_error(f"[init_unreal] Could not load factory_bp_library: {e}")

try:
    import spawn_hook              # bridges NCMD new_material -> Spawner_PCB_BP.Spawn PCB
    unreal.log("[init_unreal] spawn_hook loaded")
except Exception as e:
    unreal.log_error("[init_unreal] Could not load spawn_hook: " + str(e))

try:
    # Must come AFTER factory_bp_library so the class is registered first.
    # Auto-refreshes the Blueprints that use the Python nodes once the editor
    # finishes loading, so they no longer need a manual "Refresh All Nodes".
    import refresh_factory_nodes
    unreal.log("[init_unreal] refresh_factory_nodes loaded")
except Exception as e:
    unreal.log_error("[init_unreal] Could not load refresh_factory_nodes: " + str(e))
