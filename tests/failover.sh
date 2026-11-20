#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-or-later
# The phone's choice of number and its giving up (ADR 0084, 0092, 0094), driven on a JVM: the decisions are in org.longwire.Failover, which has nothing of Android in it.
# Needs a JDK (the one the Android build uses: ~/.cache/longwire-android/jdk, or LW_JAVA_HOME / JAVA_HOME); without one it says so and passes.
set -e
cd "$(dirname "$0")/.."
J=${LW_JAVA_HOME:-${JAVA_HOME:-${LW_ANDROID_CACHE:-$HOME/.cache/longwire-android}/jdk}}
if [ ! -x "$J/bin/javac" ]; then
	echo "skip: no JDK (LW_JAVA_HOME): the failover decisions are not tested here"
	exit 0
fi
O=build/failover
rm -rf "$O"
mkdir -p "$O"
"$J/bin/javac" -d "$O" android/app/java/org/longwire/Failover.java tests/failover/FailoverTest.java
"$J/bin/java" -cp "$O" org.longwire.FailoverTest
