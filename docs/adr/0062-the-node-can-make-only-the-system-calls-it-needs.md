# 0062 — The node can make only the system calls it needs

Status: accepted; built in 0.5.49. Extends [0004](0004-a-node-never-runs-as-root.md) (no privilege) from what the node holds to what it can do.

## Context

The node has no capability and cannot gain one (0004), but a bug in the engine, were one turned into code, could still start a program, make a process, trace another, mount, or make
memory executable: everything an unprivileged process may do. The owner's standard is that a class of attack is made impossible by how the code is built, not caught by a check on
top. What the worker does after it is up is small and known: poll, read and write its sockets, send and receive datagrams, ask the kernel for its routes and addresses, and keep a few
files in its state folder.

## Decision

1. **A seccomp allowlist, installed once the loop is set up** (`engine/sandbox.c`, called from `lw_node_serve` after everything the loop needs is open). Anything not on the list ends the
   process (`SECCOMP_RET_KILL_PROCESS`); `no_new_privs` is set, so it cannot be lifted. A worker that cannot be confined does not run (`worker.err` in its state folder says why).
2. **By construction, not by list alone:** no `execve`, `fork`, `clone`, `ptrace`, `mount`, `unshare`, `bpf`, `memfd_create`; `mmap` and `mprotect` never with the exec bit, so no memory
   is made executable; `socket` only for UNIX, IPv4, IPv6 and netlink; `ioctl` only to read an interface's hardware address and set non-blocking; `fcntl` only the five commands it uses;
   `prctl` only to ask what it is.
3. **The status says what is in force**, asked of the kernel: `sandbox` (`seccomp`, `no_new_privs`, `capabilities` held, `uid`, `installed`). A kernel without seccomp runs without it and
   the status says so; nothing claims what is not there. It is for x86-64 and arm64; elsewhere nothing is installed and the status says that.
4. **Confining the files the node can reach (Landlock) is not built** *(planned: M8)*: the kernel of the machine this was written on has none, so it could not be tried, and untried
   enforcement that can break a node is not shipped.
5. There is no switch to turn it off, in the options, the profile or the pane: the pane shows it.

## Consequences

A bug in the engine that gave an attacker code execution would still not get a shell, a new process or executable memory. It could still read and write what the node's user can (the state
folder, the seed) until Landlock is built; that is what the list of what is not checked says. Every test that starts a node runs it under the filter, so a call the loop needs and the list
lacks shows as a killed worker in `make check`. Tested: the status reports it (`tests/sandbox.sh`), and a worker is killed by the first call off the list.
