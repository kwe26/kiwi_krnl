# Kiwi patches to QuickJS 2026-06-04

Upstream: https://bellard.org/quickjs/quickjs-2026-06-04.tar.xz

1. `quickjs.c` line 71: `CONFIG_ATOMICS` (which needs pthreads) is disabled
   when building for Kiwi (`-D__kiwi__`).

Only the engine core is used (`quickjs.c libregexp.c libunicode.c cutils.c
dtoa.c`). `quickjs-libc.c` is replaced by Kiwi's own host in
`programs/kjs/`.
