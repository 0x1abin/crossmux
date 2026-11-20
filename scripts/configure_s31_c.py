# GCC 15 defaults to C23; the pinned QRCode library defines its own C boolean type.
Import("env")
env.Append(CFLAGS=["-std=gnu11"])
