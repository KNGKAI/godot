def can_build(env, platform):
    env.module_add_dependencies("sandbox_editor", ["sandbox_terminal", "sandbox_bubble", "sandbox_streaming"])
    return env["target"] == "editor"


def configure(env):
    pass
