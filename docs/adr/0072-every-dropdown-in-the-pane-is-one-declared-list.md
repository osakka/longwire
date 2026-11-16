# 0072 — Every dropdown in the pane is one declared list

Status: accepted; built in 0.5.58. The owner's decision, brought by the hibr session: "all of drop downs should be converted without any exception", "every single one, one source of truth, no parallels".

## Context

hibr 0.99.534 gave the Control Panel one way to write a dropdown row: a row declares its list once with `cp_choice` (the value in force, the setter, the values), and the panel opens it on a click or
enter, **on the value in force, ticked**, and steps it with the arrows through the same list. Until then a pane wrote the list twice, once for the click (`dt_droplist`) and once for the arrows (`cp_next`), and
neither showed which value was in force. longwire's pane had six such rows (Profile, Attach, When a call rings in the profile and in the node view, Phone Style, and the Node row, which the arrows stepped and
the click did not open at all) and its own `longwire_drop`.

## Decision

1. **Each of the six rows declares its list with `cp_choice`**: the value in force, its setter (`cp_set_lwprofile`, `cp_set_lwattach`, `cp_set_lwppolicy`, `cp_set_lwpolicy`, `cp_set_lwstyle`,
   `cp_set_lwnode`, each called with the row's id and the value) and the values. The Node row shows a node by its words (`vname`) and holds its socket.
2. **The pane keeps no list of its own**: `longwire_drop`, the `dt_droplist` calls and the arrows' `cp_next` stepping are gone, and a test reads the pane's source for them.
3. **The pane needs hibr 0.99.534**, the first that has `cp_choice`; the catalogue's floor for it says so (`ENTHIBR` in hibr-apps' `mkindex`).

## Tests

`tests/pane.sh`: each row declares its setter and its values (the profiles, jack and splice, the four answer policies, modern and classic, the nodes that answer); the setters change what they should; the pane's
source has no `longwire_drop`, `dt_droplist` or `cp_next`. Not seen on a real desktop.
