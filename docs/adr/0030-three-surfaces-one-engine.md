# 0030 — Three surfaces on one engine: the command, a control panel, and a switchboard app

Status: accepted; the pane is built, the switchboard application *(planned: M4)*. Supersedes the "Lines pane" of the
[ROADMAP](../ROADMAP.md) and amends [0029](0029-commands-are-their-own-catalogue-kind.md) (the pane is not a `# part:` of
the command).

## Context

The command line (`longwire`, `longwire-switchboard`) is how services, scripts and people who like a prompt use longwire,
and it stays. hibr's desktop (its text-mode "hui", hibr UI) runs on laptops and servers alike, so a screen can serve both. We
want two things the prompt does badly: setting a client up without remembering verbs, and watching and running a
switchboard at a glance.

## Decision

Three surfaces over the same `lw` verbs and event stream, none holding a secret and none gaining privilege (hibr ADR 0016):

1. **The command** (`cli/longwire`, `cli/longwire-switchboard`): unchanged, the supported way to run a node as a service.
2. **A control panel pane, `control-panel/longwire.hibr`** *(planned: M4)*, for the client: your number, name and caller ID; the switchboard
   to use; attach mode (splice, jack) with the one privileged step shown, never run; the answer policy; dial, the ring
   dialog with the safety phrase, hang up, unpin; lines, their members and path to each; and, for an operator, admit and
   evict. It is the Network pane's Lines section made its own pane, and it is the only place a person is asked.
3. **An application, `apps/switchboard.hibr`**, for the switchboard: registered and online nodes, numbers, a live call
   introduction feed that forgets at `GO` as the switchboard does, rate-limit and handshake-budget gauges, token
   management, and the registry of codes when it is one. It watches and manages; it does not approve calls, because the
   `register` and `dial` policy events give the script two seconds.

Both desktop pieces are catalogue entries of their own (`# modules: longwire`), not parts of the commands, and they use only
what every scope has (hibr ADR 0046). `status` is already JSON and the events are already a stream a window can follow. What the two lacked was small and is
built: the node's name in `status`, and on the switchboard the registry in pages (`lw sb nodes`), the handshake gauges and call
progress events (`tests/panel.sh`).

## What the pane does today

Rows for the node you started: its number, name, switchboard and NAT; its calls, where a ring is a row that names the
caller (with the name marked as a claim), shows the safety phrase, and asks before it picks up; the connected peers, each
of whom can be hung up on; the line and its members, each of whom can be evicted unless the line has an operator; the answer
policy; and Dial. With no node it offers Start for a jack node, and for splice it shows the command to run in a terminal.
It reads the node once a second while it is drawn, and no more. A node it starts is an ordinary `longwire up` child of the desktop and keeps running after the desktop is closed (tried by ending the desktop with TERM; a closed terminal was not tried), so a server's node is stopped from the pane's Stop row or with `longwire stop`. It has no token field, so a switchboard that wants one cannot be joined from here, and it names `longwire@*` services only, not a switchboard service. Left out: a ring that interrupts whatever else you are
doing, and the menu-bar ☎ *(planned: M4)*, because a pane runs only while it is drawn and the desktop has no hook for a background watcher;
that is a request to hibr, not something to work around here. `tests/pane.sh` drives the rows and actions against real nodes with
the desktop's calls stubbed, and loads the file under hibr's real panel code; how it is drawn has not been seen.

## Consequences

- **Open: who may speak to a worker.** The control socket is mode 0600 and checks the caller's uid, and a service node runs as
  a transient user. Until that changes the panel drives the nodes of the user running it, and shows a service node read-only.
  The alternative is a named `longwire` group with a 0660 socket, which makes everyone in the group an operator of the node
  (never a reader of its seed) and amends [0004](0004-a-node-never-runs-as-root.md) and the threat model. Not decided.
- The look is hibr's own widgets and colour schemes, so it follows the user's theme.
- Two more catalogue entries to publish with the `cli` ones, once hibr's release carries the kind.
- Testing a screen is harder than a verb; the data (status, lines, events) is tested as verbs first, the drawing by hand.
