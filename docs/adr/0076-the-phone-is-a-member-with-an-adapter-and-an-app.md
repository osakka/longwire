# 0076 — The phone is a member, with an adapter and an app

Status: accepted; built in 0.5.61, **not run on a phone**: no device or emulator was available, so the first run is the owner's. The owner asked for the Android app ("a longwire APK that interfaces with the system VPN").

## Context

Android's `VpnService` gives an app an **IP** tunnel. Longwire carries **Ethernet** frames: a line is a LAN, and what a node does is bridge. The design said so in advance ([ARCHITECTURE §5](../ARCHITECTURE.md#5-member-mode-a-phone-as-an-ethernet-host)):
the phone gets a MAC of its own, asks the far LAN for an address, answers ARP, and wraps and unwraps IP. [ADR 0074](0074-tcc-builds-the-engine-for-android.md) settled that TCC builds the engine for Android.
What was left was the adapter, the way the engine lives inside an app, and the app.

## Decision

1. **The adapter is a state machine in `engine/member.c`**, over frames and a clock. It sends and delivers through two functions it is given and touches no descriptor: a test drives it with a script for the LAN.
   - A MAC of its own: `02:` and five bytes of `H("longwire member mac" | key id)`: stable, locally administered.
   - A **DHCPv4 client** (DISCOVER, REQUEST, renew at half the lease, rebind, give up the address when the lease runs out unrenewed). The far LAN's own DHCP server answers: the phone gets a real address, gateway, resolvers,
     search domain and MTU from home. Every number in a reply is hostile until checked: the transaction id and hardware address must match; a mask must be contiguous and /8 to /30; the address a unicast one (not
     loopback, multicast, link-local, the network or the broadcast address); a gateway on the link; a resolver not loopback or multicast; a search domain only letters, digits, dots and dashes; every option length tested.
   - An **ARP responder** and a small neighbour cache (16 hosts, 5 minutes); one packet waits for the next hop's MAC, with up to four asks.
   - IPv4 wrapped toward the gateway (or the host, on the link; the broadcast MAC for the broadcast address; the multicast MAC for a multicast one) and unwrapped when it is for our MAC and our address.
     What leaves the phone must have **our address as its source** (never another, never loopback, never ourselves); what is delivered to the OS must be for our address, with a sane source and a length that is true.
     Broadcasts and multicast from the line are not delivered: the phone is not on a LAN's chatter.
   - A **generation** counts every change of address, so the app builds its tunnel again when the address moves (a new lease, a NAK, an expiry).
2. **A node with the port `member`** (`engine/port.c`, `engine/node.c`): the port is the OS tunnel, **attached later** (`leaf_attach`), because the address, and so the tunnel, is not known until the line has answered. The
   adapter reads the tunnel itself and puts what it makes into the ordinary switch (`sw_from_local`: MAC learning, the TCP MSS clamp, ARP suppression all apply); the switch's frames for the local port go to the adapter
   (`port_write`). A new control word, `member`, says the state, the lease, the generation and the counters.
3. **The engine lives inside the app** (`android/jni.c`): `embedded` in the configuration says the node runs on a thread of a process that is not its own, so it **closes none of the app's descriptors, takes no signals,
   and puts no filter on the process**. The app starts it, hands it the tunnel, and stops it: three calls. Everything else goes through the engine's **control socket** in the app's private directory, the same words the desktop's
   scripts use (`status`, `member`, `dial`, `hangup`). The tunnel is handed over from the app's thread to the engine's by a lock and an acknowledgement (`leaf_attach` returns when the loop has it), so the app may close the old one
   without a read in flight on a descriptor the system may give to something else.
4. **The app is Java, a `VpnService`** (`android/app/`): it starts the engine; dials the home number when the line has a dial tone, and again if the call is lost; builds the tunnel from the lease (the address, the line's subnet
   or everything, the resolvers and domain, an MTU of at most 1400); builds it again when the generation moves; and shows the safety phrase. The app is **left out of its own tunnel** (`addDisallowedApplication`), so the
   engine's sockets reach the world without `protect()` and the line is not carried through itself. With "send everything through home" the tunnel also takes `::/0` on a made-up address, which **drops IPv6 instead of
   letting it past the line** (IPv6 is not carried).
5. **Signing is the owner's.** `android/build-apk.sh` builds the APK with the SDK's tools (the Java part is not C; the native library is built by TCC) and signs it only with the key the owner points it at (`LW_KEYSTORE`).
   Without one it writes `longwire-VERSION.apk.unsigned`, which Android will not install. No key is made, held or committed here.

## Not done, and what that means

- **Not run on a phone.** Built, linked, packaged, aligned; the adapter is tested against a scripted LAN, and the whole path (an embedded node, handed a tunnel from another thread, dials a home node, gets its lease from a LAN
  behind it, and carries packets both ways, unchanged) is tested on Linux, where the tunnel is a socket pair (`tests/leafnet.sh`). What only a phone can show: that the library loads under Android's loader and C library; that
  the system accepts the tunnel as built; that the foreground service type is right for Android 16; that a mobile network lets the line through. Expect to find things.
- **IPv6** is not carried *(planned: M6)*. **The leaf role** (one session, for the battery) *(planned: M6)*: a phone is a member with a session to each member it talks to, like any node, and keeps the line's keepalives.
- **The seed** is a file in the app's private directory, mode 0600 (the system's sandbox is its protection); **keeping it in the Android Keystore** and **showing a recovery phrase** are *(planned: M6)*.
- **No system call filter** on the phone's engine: the process is the app's, and the system's own sandbox and filter apply to it. The desktop's worker is confined to what it needs (ADR 0062); this is weaker, and says so.
- **A hostile member can answer a phone's DHCP** as it could any host's on a LAN (the same trust as a cable): the checks above bound what it can say, not whether it speaks. It is a line of people one admitted.
- **No duplicate-address check** (a probe before using the lease): the far LAN's server is trusted to hand out free addresses.
- **A kill switch** is the system's: "Block connections without VPN" in the VPN's settings *(planned: M6)* for the app to offer.

## Tests

`tests/leaf.sh` (bounds-checked): the MAC; DISCOVER; offers refused for a mask that is not one, a loopback or link-local address, a gateway off the link, a wrong transaction; REQUEST, ACK, the lease's fields; ARP answered
and learned; a packet waits for ARP and is released; the broadcast and multicast MACs; nothing with another source, loopback, ourselves, IPv6 or a false length leaves; what reaches the OS is exact and only what is ours;
renewal, a moved address, NAK, expiry; four thousand frames of noise and of mangled replies. `tests/leafnet.sh`: a real node and a real home node, a scripted LAN, a socket pair for the tunnel: the lease, a packet to
the gateway and its echo, a packet for the internet through the gateway's MAC, a spoofed source refused, the adapter's counters, a clean stop.
