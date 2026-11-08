"""Keep the existing C dependencies on C17 under the S31 GCC15 core."""
Import("env")

# The RC1 core defaults to C23, where the pinned QRCode library's bool/true/
# false declarations are keywords. Scope the language selection to this env;
# C++ retains CrossMux's existing C++20 configuration.
def use_c17(build_env, node):
    # Called after framework flags exist, in each actual library environment.
    # A post script changing the outer env misses already-cloned dependencies.
    build_env.ProcessUnFlags("-std=gnu23")
    build_env.AppendUnique(CFLAGS=["-std=gnu17"])
    return node


env.AddBuildMiddleware(use_c17, "*.c")
