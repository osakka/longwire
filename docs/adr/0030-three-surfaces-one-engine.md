# 0030 — Three surfaces on one engine: the command, a control panel, and a switchboard app

Status: accepted; the design is settled and the code is *(planned: M4)*. Supersedes the "Lines pane" of the
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
what every scope has (hibr ADR 0046). They need three verbs the module lacks: a machine-readable `status` and `lines`, and an
event stream a window can follow *(planned: M4)*; any of that is C under TCC.

## Consequences

- **Open: who may speak to a worker.** The control socket is mode 0600 and checks the caller's uid, and a service node runs as
  a transient user. Until that changes the panel drives the nodes of the user running it, and shows a service node read-only.
  The alternative is a named `longwire` group with a 0660 socket, which makes everyone in the group an operator of the node
  (never a reader of its seed) and amends [0004](0004-a-node-never-runs-as-root.md) and the threat model. Not decided.
- The look is hibr's own widgets and colour schemes, so it follows the user's theme.
- Two more catalogue entries to publish with the `cli` ones, once hibr's release carries the kind.
- Testing a screen is harder than a verb; the data (status, lines, events) is tested as verbs first, the drawing by hand.
