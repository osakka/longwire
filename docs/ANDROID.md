# Longwire on Android

A phone joins a line as a **member**: it appears on the home network the way a laptop plugged into the router does. It gets an address from the home router's own DHCP server, and from then on reaches the printer, the NAS and
the internet through home. It is Android's own VPN (`VpnService`), so the system shows the key icon and its consent dialog, and "Block connections without VPN" in Settings is the kill switch.
How it is built: [ADR 0076](adr/0076-the-phone-is-a-member-with-an-adapter-and-an-app.md); the design: [ARCHITECTURE §5](ARCHITECTURE.md#5-member-mode-a-phone-as-an-ethernet-host).

**Status: built; its first run on a phone is the owner's.** Expect to find things.

## What it needs

- An Android 7 (API 24) or newer phone with a 64-bit ARM processor (arm64-v8a). Written against Android 16.
- A **switchboard** it can reach (`HOST:PORT/lw1…`, the address `longwire-switchboard` prints), and a **home node** on the line: a machine on the home network running `longwire` with `--splice` (or in jack mode), registered
  on the same switchboard. A token, if the switchboard asks for one.
- The home node's **number** (`312-884-076`, or its full `lw1…` address) to call.

## Building it

```sh
sh android/build-native.sh        # the engine, by TCC, for arm64 (about 660 MB downloaded the first time: TinyCC's source and Google's headers)
sh android/build-apk.sh           # the app around it
```

Both keep what they fetch in `~/.cache/longwire-android` (`LW_ANDROID_CACHE` names another). The Android SDK (platform 36 and build tools; `sdk/` in that folder, or `LW_SDK=DIR`) and a JDK 17 or newer (`jdk/` there, or
`LW_JAVA_HOME`) are not fetched by the scripts: the SDK asks for Google's licence, which is a person's to accept (`sdkmanager "platforms;android-36" "build-tools;36.0.0"`). The Java part of the app is not C and does not fall under the
compiler rule ([ADR 0021](adr/0021-tcc-is-the-compiler-without-exception.md)); the native library is built by TCC like everything else.

**Signing is yours.** Android installs nothing unsigned. Point the script at your key: `LW_KEYSTORE=~/my.jks LW_KEY_ALIAS=longwire sh android/build-apk.sh` (it asks for the password), which writes
`build/android/longwire-VERSION.apk`; or sign the `.unsigned` file yourself with the SDK's `build-tools/36.0.0/apksigner sign --ks KEYSTORE --out longwire.apk longwire-VERSION.apk.unsigned`. Without a key it writes `longwire-VERSION.apk.unsigned`; no key is made or kept for you. Install with `adb install build/android/longwire-VERSION.apk`, or copy the file to the phone.

## Using it

Open **Longwire**, fill in the switchboard, the home number, a token if needed, and a name for the phone, and press **Turn on**. The system asks once to allow the VPN. The screen says where it is: reaching the switchboard,
calling home, connected to home and asking its network for an address, connected with the address and gateway. The **safety phrase** shown is the one the home node's screen shows: compare them, once, when the call
is first made (the same as between two desktops; [PROTOCOL §3](PROTOCOL.md)). The home node answers by its own policy (`longwire policy`).

By default the tunnel carries only the home network's subnet: the printer and the NAS, not the whole internet. "Send everything through home" adds the default route; the home node then needs `--exit` sharing for
the internet to work ([ADR 0056](adr/0056-a-member-can-share-its-internet.md)).

## What it does not do yet

- **IPv6** is not carried *(planned: M6)*. With "send everything through home" it is dropped, not let past the line; without it, the phone's IPv6 goes out the normal way.
- **The identity** is a file in the app's private storage. If the phone is lost, or the app's data is cleared, the phone is a new member and the home node must be told its new number. A recovery phrase and the Android
  Keystore are planned (M6).
- **No battery optimisation** beyond the line's own keepalive (25 s); the leaf role *(planned: M6)* cuts it.
- **One home number**; no list of homes, no switching.
- **No system call filter** on the engine in the phone (the process is the app's, under Android's own sandbox).
