# 0082 — A name is dialled from the address book

Status: accepted; built in 0.5.67. The owner's option A for dialling by name: the local address book, which is the system's (hibr's Contacts, synced from the owner's CardDAV server), not a list of longwire's own.

## Decision

1. **`longwire dial NAME`**: a number or a full address is dialled as before; anything else is a name, looked up with hibr's `contacts find` (0.99.553 or later), which answers from the copy kept on this machine and never waits on the network.
2. **What a card must have to be dialled**: a phone like `312-884-076`, or a note line `longwire lw1…` (a full address). The address wins when both are there: it names the key, not a number a switchboard hands out. Cards without either are
   not offered.
3. **One person**: the call is made and says to whom (`calling Bo Berg (312-884-076)`). **Nobody**: it says so and how to add one. **More than one**: it lists them with their numbers and dials none: a name that could be two people is never guessed.
4. **`longwire contact find NAME`** shows who has one; **`contact add NAME NUMBER|lw1…`** gives someone one, through `contacts put`, so the card is the address book's own, in the Contacts app and on the server. A number is kept as a phone and an address as a note line.
5. **A ring shows your own name for the caller**: `in your address book: Bo Berg`, beside the caller's own claim (`says it is "…"`, which is only ever a claim). A name in your book is yours.
6. **No `contacts`, no names.** Without the command, or with the book never synced (status 1) or none chosen (status 2), the dial says so in `contacts`' own words and does nothing; a number or an address dials without the book as ever. The scripts
   declare no minimum hibr version for this: the rest of longwire does not need one.

## Not done

- The ring's lookup is by the number (a phone), so a caller you saved by full address only is not named; a match by key would need the key's address on the ring event.
- The phone app does not dial by name: Android's own contacts are a different mechanism.
- Matching is `contacts find`'s: a part of a name, a phone or a note line, in any case.

## Tests

`tests/book.sh`, with a stand-in for `contacts`: find gives a person with an address, not one with only a phone number; a phone like 312-884-076 is a number; a note that is not a full address is not; the address wins over the number; the book never synced is
`contacts`' own status and words; a name that is nobody, two people, one person; a number is dialled without asking the book; `contact add` keeps a number as a phone and an address as a note line and refuses junk. `tests/events.sh`: a ring says the book's name.
