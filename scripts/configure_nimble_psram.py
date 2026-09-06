"""Override NimBLE host configuration without changing installed sources."""

import os

Import("env")  # noqa: F821

config = os.path.join(
    env.subst("$PROJECT_DIR"),
    env.GetProjectOption("custom_nimble_config", "src/platform/NimblePsramConfig.h"),
)


def configure_host(build_env, node):
    compiled = build_env.Object(node, CCFLAGS=build_env.get("CCFLAGS", []) + ["-include", config])[0]
    build_env.Depends(compiled, config)
    return compiled


env.AddBuildMiddleware(configure_host, "*/NimBLE-Arduino/src/*")
