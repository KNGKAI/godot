def can_build(env, platform):
    env.module_add_dependencies("sandbox_bubble", ["sandbox_core", "sandbox_sim", "sandbox_streaming"])
    return not env["disable_3d"] and not env["disable_physics_3d"]


def configure(env):
    pass
