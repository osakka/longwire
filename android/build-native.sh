#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The engine as an Android library, built by TCC like everything else (docs/adr/0021, 0074): liblongwire.so for arm64-v8a in build/android/. Fetches, into a cache that is not the source tree,
# TinyCC at the revision the Debian package is built from (compiled by the system's TCC, to cross-compile for arm64) and Google's NDK r27c (checked against Google's published SHA-1;
# only its sysroot is used: the headers and the stub libraries, no compiler). About 660 MB the first time.
#   sh android/build-native.sh        LW_ANDROID_API=24 (the oldest Android this targets: 7.0)   LW_ANDROID_CACHE=DIR (default ~/.cache/longwire-android)
set -e
cd "$(dirname "$0")/.."
API=${LW_ANDROID_API:-24}
C=${LW_ANDROID_CACHE:-$HOME/.cache/longwire-android}
NDK_ZIP=android-ndk-r27c-linux.zip
NDK_SHA1=090e8083a715fdb1a3e402d0763c388abb03fb4e
TINYCC_REV=62c30a4a
mkdir -p "$C" build/android
command -v tcc > /dev/null || { echo "android: TCC is the compiler and it is not installed" >&2; exit 1; }
if [ ! -x "$C/tinycc/arm64-tcc" ]; then
	[ -d "$C/tinycc" ] || git clone -q https://github.com/TinyCC/tinycc.git "$C/tinycc"
	(cd "$C/tinycc" && git checkout -q "$TINYCC_REV" && ./configure --cc=tcc > /dev/null && make cross-arm64 > /dev/null)
fi
SR=$C/android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/sysroot
if [ ! -d "$SR" ]; then
	[ -f "$C/$NDK_ZIP" ] && [ "$(sha1sum "$C/$NDK_ZIP" | cut -c1-40)" = "$NDK_SHA1" ] || {
		curl -f -C - -o "$C/$NDK_ZIP" "https://dl.google.com/android/repository/$NDK_ZIP"
		[ "$(sha1sum "$C/$NDK_ZIP" | cut -c1-40)" = "$NDK_SHA1" ] || { echo "android: the NDK download does not match Google's checksum: not used" >&2; exit 1; }
	}
	(cd "$C" && unzip -q -o "$NDK_ZIP" 'android-ndk-r27c/toolchains/llvm/prebuilt/linux-x86_64/sysroot/*')
fi
T=$C/tinycc
VEND="vendor/monocypher/monocypher.c vendor/monocypher/monocypher-ed25519.c $(ls vendor/mlkem768/*.c | grep -v compat.h | tr '\n' ' ')"
# Bionic's headers use clang's nullability and feature tests, and its ioctl overload and the kernel's byte-swap builtins are what TCC 0.9.27 does not have: said here.
BSW16='-D__builtin_bswap16(x)=((unsigned short)((((x)&0xffu)<<8)|(((x)>>8)&0xffu)))'
BSW32='-D__builtin_bswap32(x)=((((x)&0xffu)<<24)|(((x)&0xff00u)<<8)|(((x)>>8)&0xff00u)|(((x)>>24)&0xffu))'
BSW64='-D__builtin_bswap64(x)=((unsigned long long)((((x)&0xffull)<<56)|(((x)&0xff00ull)<<40)|(((x)&0xff0000ull)<<24)|(((x)&0xff000000ull)<<8)|(((x)>>8)&0xff000000ull)|(((x)>>24)&0xff0000ull)|(((x)>>40)&0xff00ull)|(((x)>>56)&0xffull)))'
# shellcheck disable=SC2086
"$T/arm64-tcc" -B"$T" -nostdinc -nostdlib -I"$T/include" -I"$SR/usr/include/aarch64-linux-android" -I"$SR/usr/include" -Ivendor/mlkem768 \
	-D__ANDROID__ -D__ANDROID_API__="$API" -DBIONIC_IOCTL_NO_SIGNEDNESS_OVERLOAD -D_Nonnull= -D_Nullable= -D_Null_unspecified= \
	'-D__has_feature(x)=0' '-D__has_builtin(x)=0' '-D__has_attribute(x)=0' '-D__has_include(x)=0' '-D__has_include_next(x)=0' "$BSW16" "$BSW32" "$BSW64" \
	-D__PRETTY_FUNCTION__=__func__ -include android/shim.h -DLW_VERSION="\"$(cat VERSION)\"" -Wall -shared \
	-o build/android/liblongwire.so android/jni.c $VEND -L"$SR/usr/lib/aarch64-linux-android/$API" -lc -ldl
echo "android: build/android/liblongwire.so ($(wc -c < build/android/liblongwire.so) bytes, arm64-v8a, API $API)"
