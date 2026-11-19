#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The Android app (docs/adr/0076): build/android/longwire-VERSION.apk. The native engine is built by TCC (android/build-native.sh); only the Java part, which is not C, uses Google's tools: javac (a JDK),
# aapt2, d8, zipalign and apksigner (the SDK's build tools). The SDK is not fetched here (it asks for Google's licence, which is a person's to accept): it is looked for in the cache (LW_ANDROID_CACHE, default
# ~/.cache/longwire-android/sdk; LW_SDK=DIR names another) with platform 36 and build tools, and a JDK 17 or newer in LW_JAVA_HOME, JAVA_HOME or the cache's jdk.
#   LW_KEYSTORE=FILE [LW_KEY_ALIAS=NAME]   sign with the owner's key (apksigner asks for the password); without it the APK is left unsigned, and Android will not install an unsigned APK
set -e
cd "$(dirname "$0")/.."
C=${LW_ANDROID_CACHE:-$HOME/.cache/longwire-android}
LW_SDK=${LW_SDK:-$C/sdk}
[ -d "$LW_SDK/platforms/android-36" ] || { echo "apk: no Android SDK with platform 36 and build tools at $LW_SDK (LW_SDK=DIR)" >&2; exit 1; }
J=${LW_JAVA_HOME:-${JAVA_HOME:-$C/jdk}}
[ -x "$J/bin/javac" ] || { echo "apk: LW_JAVA_HOME must name a JDK" >&2; exit 1; }
PATH=$J/bin:$PATH
BT=$(ls -d "$LW_SDK"/build-tools/* | sort -V | tail -1)
JAR=$LW_SDK/platforms/android-36/android.jar
V=$(cat VERSION)
VC=$(echo "$V" | awk -F. '{ printf "%d", $1 * 1000000 + $2 * 1000 + $3 }')
O=build/android/apk
rm -rf "$O"
mkdir -p "$O/gen" "$O/cls" "$O/stage/lib/arm64-v8a"
[ -f build/android/liblongwire.so ] || sh android/build-native.sh
sed "s/@VERSIONCODE@/$VC/; s/@VERSION@/$V/" android/app/AndroidManifest.xml > "$O/AndroidManifest.xml"
"$BT/aapt2" compile --dir android/app/res -o "$O/res.zip"
"$BT/aapt2" link -o "$O/base.apk" --manifest "$O/AndroidManifest.xml" -I "$JAR" -R "$O/res.zip" --min-sdk-version 24 --target-sdk-version 34
# shellcheck disable=SC2046
"$J/bin/javac" -source 8 -target 8 -Xlint:-options -classpath "$JAR" -d "$O/cls" $(find android/app/java -name '*.java')
"$BT/d8" --lib "$JAR" --min-api 24 --output "$O" $(find "$O/cls" -name '*.class')
cp build/android/liblongwire.so "$O/stage/lib/arm64-v8a/"
cp "$O/base.apk" "$O/unaligned.apk"
(cd "$O" && "$J/bin/jar" --update --file unaligned.apk classes.dex 2> /dev/null || zip -q -u unaligned.apk classes.dex)
(cd "$O/stage" && zip -q -r -D ../unaligned.apk lib)
"$BT/zipalign" -f -p 4 "$O/unaligned.apk" "$O/aligned.apk"
OUT=build/android/longwire-$V.apk
if [ -n "$LW_KEYSTORE" ]; then
	"$BT/apksigner" sign --ks "$LW_KEYSTORE" ${LW_KEY_ALIAS:+--ks-key-alias "$LW_KEY_ALIAS"} --out "$OUT" "$O/aligned.apk"
	"$BT/apksigner" verify "$OUT"
	echo "apk: $OUT (signed with $LW_KEYSTORE)"
else
	cp "$O/aligned.apk" "$OUT.unsigned"
	echo "apk: $OUT.unsigned (not signed: sign it with your key, apksigner sign --ks KEYSTORE --out $OUT $OUT.unsigned)"
fi
