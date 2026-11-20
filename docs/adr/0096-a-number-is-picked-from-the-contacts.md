# 0096 — A number is picked from the contacts

Status: accepted; built after 0.5.74. (Ticket LW-12.) The desktop dials a name from the address book ([ADR 0082](0082-a-name-is-dialled-from-the-address-book.md)): a person's Longwire number is kept as one of their phone numbers.

## Decision

1. **The app has a button, *Add a number from your contacts*,** that opens the system's picker for a contact's phone number (`ACTION_PICK` on the phone numbers of the contacts). The picker hands the app that one number, for this once: **the app asks for no permission to read the contacts**, and never reads the rest.
2. **A number of nine digits** (written as in a contact, `312-884-076` or `312 884 076`) is added to *Numbers to dial*, in the form `312-884-076`, once. Anything else is said not to be a Longwire number, and nothing is added.
3. **It adds a number, not a name:** the list still holds numbers, as the engine dials numbers; the person's name is the contact's.

## Consequences

- No standing access to the contacts, and nothing about them is kept or sent.
- A contact is found by picking it, not by typing its name in the list: looking a typed name up in the list would need the permission to read the contacts, which this declines for now.
- Not tried on a phone: the picker's result on any make of phone.
