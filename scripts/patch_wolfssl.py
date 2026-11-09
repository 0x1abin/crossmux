from pathlib import Path

Import("env")


PROJECT_DIR = Path(env.subst("$PROJECT_DIR"))
MARKER = "/* CrossPoint wolfSSL compatibility overrides */"
OVERRIDES = f"""

{MARKER}
#undef NO_DH
#ifndef HAVE_FFDHE_2048
#define HAVE_FFDHE_2048
#endif
/* MEMFIX-PORT: 8192 handles up to RSA-4096 keys (the public-CA maximum,
   ISRG Root X1 included) with half the per-bignum heap of 16384: with
   WOLFSSL_SMALL_STACK each fast-math temp is FP_MAX_BITS/8 * 2 bytes on the
   heap, and TLS cert verification allocates dozens at once. */
#undef FP_MAX_BITS
#define FP_MAX_BITS 8192
/* Arduino-wolfSSL always defines DEBUG_WOLFSSL, but SecureClient turns the
   trace on only in FREEINK_WOLFSSL_DEBUG builds. Drop it otherwise so the trace
   code and strings are not linked. Set the flag for all sources (build_flags),
   not just SecureNet, to keep the trace. */
#ifndef FREEINK_WOLFSSL_DEBUG
#undef DEBUG_WOLFSSL
#endif
"""


def patch_user_settings(path: Path) -> None:
    text = path.read_text()
    if MARKER in text:
        text = text.split(MARKER, 1)[0].rstrip()
    path.write_text(text + OVERRIDES + "\n")
    print(f"Patched wolfSSL settings: {path.relative_to(PROJECT_DIR)}")



def patch_input_buffers(path: Path) -> None:
    """Retain each connection's input buffer and avoid empty-buffer double peaks.

    Forced teardown still zeroes/frees through wolfSSL. Never release ciphertext
    or plaintext that is still referenced. Match the pinned functions exactly so
    a dependency update cannot silently enable an unverified patch.
    """
    text = path.read_text()
    patches = [
        ("/* CrossMux retained TLS input */", "void ShrinkInputBuffer(WOLFSSL* ssl, int forcedFree)\n{", """void ShrinkInputBuffer(WOLFSSL* ssl, int forcedFree)
{
#ifdef FREEINK_WOLFSSL_RETAIN_INPUT_BUFFER
    /* CrossMux retained TLS input */
    if (!forcedFree) return;
#endif"""),
        ("/* CrossMux release consumed input before growth */", "    tmp = (byte*)XMALLOC(size + usedLength + align,", """#ifdef FREEINK_WOLFSSL_RELEASE_EMPTY_INPUT_BUFFER
    /* CrossMux release consumed input before growth */
    if (usedLength == 0 && ssl->buffers.inputBuffer.dynamicFlag &&
        ssl->buffers.inputBuffer.length == ssl->buffers.inputBuffer.idx &&
        ssl->buffers.clearOutputBuffer.length == 0) {
        ShrinkInputBuffer(ssl, FORCED_FREE);
    }
#endif
    tmp = (byte*)XMALLOC(size + usedLength + align,"""),
    ]
    for marker, old, new in patches:
        if marker in text:
            continue
        if text.count(old) != 1:
            raise RuntimeError(f"Unrecognized pinned wolfSSL input-buffer source: {path}")
        text = text.replace(old, new)
    path.write_text(text)


for internal in PROJECT_DIR.glob(".pio/libdeps/*/Arduino-wolfSSL/src/src/internal.c"):
    patch_input_buffers(internal)

for settings in PROJECT_DIR.glob(".pio/libdeps/*/Arduino-wolfSSL/src/user_settings.h"):
    patch_user_settings(settings)
