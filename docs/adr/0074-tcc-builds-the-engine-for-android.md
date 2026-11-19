# 0074 — TCC builds the engine for Android

Status: accepted; the build is made (0.5.60). The app around it *(planned: M6)*. The owner asked for the Android app ("a longwire APK that interfaces with the system VPN"); [ADR 0021](0021-tcc-is-the-compiler-without-exception.md) said the
first thing to settle was whether TCC can build the engine as an Android library, since the rule leaves no other compiler.

## Context

The engine is plain C that never includes hibr's header, so an app can use it. Android's `VpnService` is a Java class: the app has a Java (or Kotlin) part, which is not C and not the compiler rule's concern, and a
native library, which is. The question was only the library: can `tcc` produce one that Android's loader takes, and does it compute the right answers?

## Decision and result

1. **TCC builds it.** `sh android/build-native.sh` makes `build/android/liblongwire.so` for arm64-v8a, API 24 (Android 7) and up. It builds a cross TCC for arm64 from TinyCC's own source (at the revision Debian's
   `tcc` package is made from; compiled by the system's TCC, not by any other compiler), takes only the sysroot of Google's Android native toolkit r27c (headers and stub libraries, checked against Google's published SHA-1; none of its
   compilers is used), and compiles the engine and the vendored crypto in one step. The cache is outside the source tree; the first run downloads about 660 MB.
2. **What it took** (all in `android/shim.h` and the build script, none in the engine's logic): bionic's headers use the other compiler family's nullability keywords and feature tests, which are defined away; TCC 0.9.27 lacks
   `__builtin_bswap16/32/64`, which the kernel's headers use, so they are defined; bionic has `getrandom` from API 28 (a call to the kernel instead below that, in `engine/id.c`), `explicit_bzero` from 28 and
   `getdtablesize` from 29 (small inline versions); and the engine's own `LINE_MAX` is undefined first, where `<limits.h>` has one.
3. **The library is well formed**: ELF64 AArch64 shared object; needs only `libc.so` and `libdl.so`; no text relocations; segments aligned to 64 KB (which also meets Android 15's 16 KB page rule); every one of its
   imports exists in Android's C library at API 24 (checked against the toolkit's stub libraries).
4. **TCC's arm64 code is correct where it was tried**: built by the same cross TCC for aarch64 glibc and run under QEMU's user emulation, the crypto known-answer test (X25519, BLAKE2b, ChaCha20-Poly1305, Ed25519;
   2,100 cases against Python's `hashlib` and `cryptography`) has **0 mismatches**, and the engine's unit tests (`tests/switch.c`, which includes the ML-KEM key generation, signing and verification, the line, the
   admits and the exit logic: 70 checks) all pass.

## Not done, and what that means

- **Not run on Android.** There is no phone or emulator here. The library links and its imports resolve in the toolkit's stubs; that it loads and runs on a device, with bionic's real libc, is **not shown**.
  The first device test is the owner's.
- **The rest of the engine's tests on arm64** (the networking tests need Linux namespaces and TCC's x86-64 build) are not run under emulation; the two above are.
- **Android restricts what an app may do** (netlink for interface lists, raw sockets, `/proc` and `/sys` files, its own seccomp filter): the engine's node loop needs an Android port: no jack or tap, no netlink road
  watcher (Android tells the app about its networks through `ConnectivityManager`), the sandbox of [0062](0062-the-node-can-make-only-the-system-calls-it-needs.md) not installed on Android *(planned: M6)*.
- **The packet shim** (an IP tunnel into Ethernet frames: a virtual MAC, a DHCP client on the line, ARP and neighbour answers; [ARCHITECTURE](../ARCHITECTURE.md) §5) and the **leaf role** *(planned: M6)*.
- **The app**: the Java `VpnService`, the screens, the signing, the build around `aapt2`, `d8` and `apksigner` (a JDK and the Android SDK, not installed here) *(planned: M6)*. The signing key is the owner's: it is
  never made, kept or committed here.

## Tests

`tests/kat.py` and `tests/switch.sh` on arm64 are the checks above, run by hand with `qemu-aarch64` and an aarch64 glibc (downloaded, not installed); they are not in `make check`, which needs neither the emulator nor
the toolkit. The Linux build and its unit tests are unchanged by this (the `engine/id.c` and `engine/proto.h` changes are inert there).
