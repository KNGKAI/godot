def can_build(env, platform):
    env.module_add_dependencies("sandbox_terminal", ["sandbox_core", "sandbox_narrative", "msdfgen"])
    return not env["disable_3d"] and not env["disable_physics_3d"]


def configure(env):
    pass
