# Longwire on Android

A phone joins a line as a **member**: it appears on the home network the way a laptop plugged into the router does. It gets an address from the home router's own DHCP server, and from then on reaches the printer, the NAS and
the internet through home. It is Android's own VPN (`VpnService`), so the system shows the key icon and its consent dialog, and "Block connections without VPN" in Settings is the kill switch.
How it is built: [ADR 0076](adr/0076-the-phone-is-a-member-with-an-adapter-and-an-app.md); the design: [ARCHITECTURE §5](ARCHITECTURE.md#5-member-mode-a-phone-as-an-ethernet-host).

**Status: built, and run on the owner's phone** (connected through the line). Its backup numbers, give-up limits and battery saving, and a change from wifi to mobile data, have not been tried or measured on a phone: expect to find things.

## What it needs

- An Android 7 (API 24) or newer phone with a 64-bit ARM processor (arm64-v8a). Written against Android 16.
- A **switchboard** it can reach (`HOST:PORT/lw1…`, the address `longwire-switchboard` prints), and a **home node** on the line: a machine on the home network running `longwire` with `--splice` (or in jack mode), registered
  on the same switchboard. A token, if the switchboard asks for one.
- The **number** to dial (`312-884-076`, or its full `lw1…` address): the node on your home network that the phone joins.

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

## Enrolling a phone

Typing a switchboard's 55-character address on a phone is the manual way, and nobody should. On the machine that runs the switchboard:

```sh
please longwire-switchboard token invite --host sb.example.org:6741 --dial 312-884-076 --name "Omar's phone"
```

(`--host` is where the phone reaches the switchboard; `--dial` is the number of the node the phone is to call, or several, a comma apart (`312-884-076,447-120-933`: the first is tried first, the others are its backups); `--name` is optional.) It makes **one token**, good once, and prints a `longwire://enroll?...` link and, if
`qrencode` is installed, the same as a QR code in the terminal. Send the link to the phone in a message or an email, or point the phone's camera at the QR code: the phone offers to open it in **Longwire**, which fills in
the form and says so. Nothing is turned on until you press **Turn on**. The link is a secret between you and that phone: anyone who opens it first uses the token. The app refuses anything that is not exactly what
such a link carries.

## Using it

Open **Longwire** (or the invitation link). The screen is a status card (a coloured dot and one word: **Connected** in green, **Connecting** in amber, **Needs attention** in red, **Off**, then what it is doing
and the **safety phrase** once there is a call), one big button, a **Line** card (the switchboard and the number it dials, with **Edit** behind it), and two switches. Fill in the switchboard, the number to dial, a token if needed, and a
name for the phone if the link did not, and press **Turn on**. The system asks once to allow the VPN. Compare the safety phrase with the one on the home node's screen, once, when the call is first made (the same as between two
desktops; [PROTOCOL §3](PROTOCOL.md)). The home node answers by its own policy (`longwire policy`, or `longwire answer auto`).

**The address** is not typed: it comes from DHCP, from the home router, and the router is told the phone's name (the one you set, or the phone's model if you set none, made into a host name: lower case, hyphens, 32 characters at most), so its list says whose phone it is and not "longwire", as it would for a laptop on the home network. The phone's hardware address is made from its key, so it is always the same one, and the router
tends to give it the same address; a reservation in the router for that address pins it.

By default the tunnel carries only the home network's subnet: the printer and the NAS, not the whole internet. "Send everything through the line" adds the default route; the home node then needs `--exit` sharing for
the internet to work ([ADR 0056](adr/0056-a-member-can-share-its-internet.md)).

## When the other end does not answer

The app calls the number, and if there is no answer it tries again after 5 s, then waits longer each time, up to a **longest wait** you choose (30 s, 2 min, 10 min; **When the other end is not answering** in the app, taking effect
the next time you turn it on). When the phone's network changes or comes back (wifi to mobile data, out of a tunnel) it forgets that wait, tells the line the road changed, and tries at once
([ADR 0081](adr/0081-the-phone-tries-again-when-the-network-comes-back.md)). The address and the tunnel are kept meanwhile.

## Backup numbers, and turning itself off

**Numbers to dial, in order** takes more than one: `312-884-076, 447-120-933`. The phone calls the first; after two tries without an answer it moves to the next, and so on round the list, showing which (`calling 312-884-076 (2 of 3)`).
The one that answers is the first next time. This is how a second server is used: two boxes are two numbers, and the list is on the phone ([ADR 0084](adr/0084-the-phone-has-backup-numbers-and-gives-up-by-its-own-limits.md)). Every number in the list may answer
the phone's DHCP, dialled or not, so the box that is active carries its lease whichever it is.

**Turn off by itself after** puts a limit on a line that does not come back: a number of failed tries in a row (never, 5, 10 or 20) and a time without a line (never, 5 minutes, 30 minutes, 2 hours), whichever comes first. When it
is reached the VPN is turned off, and a notification says so (`Longwire turned itself off: no answer after 10 tries`): a VPN that stops itself must not do it silently. The default is never. The count is of calls made, which the
longest-wait setting spaces out, so the time limit is the one that also covers a switchboard that cannot be reached.

## Always on, and the battery

A phone that is connected all day should cost little, and what costs is waking up. Three things are done and one is yours:

- **The engine sleeps** a second at a time when nothing is happening (a call or a handshake makes it wake as fast as ever, and a packet wakes it at once), where it used to wake ten times a second.
- **The app looks at the engine** every second with the screen on and every five with it off.
- **The keys are renewed every five minutes**, not two: a renewal is a key exchange.
- **Battery saver** (a switch): the phone sends its keep-alive every 55 s instead of 25 s. Keep it off if the line drops on mobile data: mobile networks forget idle UDP flows at times of their own choosing, and this has not been
  tried on one. The home node sends its own keep-alives at its own rate (`longwire up --keepalive`); the phone's radio wakes for those too, so the saving is real only when home is slowed to match.
- **Yours:** Android pauses background apps to save battery, and a paused Longwire is a dropped line. The app shows **Let it run in the background** until you have allowed it (one tap, one system dialog). With it, the
  system keeps the line up while the phone sleeps.

## What it does not do yet

- **IPv6** is not carried *(planned: M6)*. With "send everything through the line" it is dropped, not let past the line; without it, the phone's IPv6 goes out the normal way.
- **The identity** is kept in the Android Keystore ([ADR 0091](adr/0091-the-phones-seed-lives-in-the-keystore.md)): the app's storage holds only the encrypted seed. The **Identity** card shows its 33 words, to write down, and restores a phone from them, with the line off. If the phone is lost and the words were not written down, the new phone is a new member and the home node must be told its new number. The keystore code has not been run on a phone.
- **The phone is a leaf** ([ADR 0087](adr/0087-a-leaf-keeps-only-the-sessions-it-asked-for.md)): it dials only the numbers in its list, and the other box of a pair when one it dialled says it is on its own segment ([ADR 0092](adr/0092-a-leaf-keeps-the-other-box-of-a-pair-too.md)), and refuses a ring from any other member. It also learns the other box of a pair from the box it dials and keeps it ([ADR 0094](adr/0094-a-phone-learns-the-other-box-of-a-pair.md)): the Line card shows it as *learned*, with **Forget learned numbers**. The other members still ring it, with the backoff of ADR 0085, and it refuses them; telling them to stop is the keep-quiet message *(planned: M6)*, ticket LW-13.
- **No dialling by name** on the phone: Android's contacts are not used (ticket LW-12).
- **No system call filter** on the engine in the phone (the process is the app's, under Android's own sandbox).
