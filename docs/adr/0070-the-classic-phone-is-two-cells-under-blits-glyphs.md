# 0070 — The classic phone is two cells under Blit's glyphs

Status: accepted; built in 0.5.56. The owner's decision, brought by the Blit session after Blit 0.3.57 drew the glyphs.

## Context

Under Blit's glyphs the menu bar item and the Control Strip module draw a telephone in one cell, in a modern and a classic (rotary) style ([ADR 0035](0035-longwire-on-the-menu-bar.md)). A two-cell
choice (*Phone Icon Size*) existed in 0.5.14 and was removed in 0.5.22 as a setting. The owner now wants the classic rotary phone to be two cells wide; Blit 0.3.57 draws it as four pairs,
`phone-classic-wide-off`, `-wait`, `-on` and `-ring` (U+1002EC to U+1002F3, each a left and a right codepoint), and the one-column ones stay for one-cell places.

## Decision

1. **Phone Style: Classic is two cells where the glyph set has them**: the menu bar item and the strip module read `GL[phoneoffcw]`, `GL[phonewaitcw]`, `GL[phoneoncw]` and `GL[phoneringcw]` (two codepoints in
   one string, as hibr's glyph table maps Blit's pairs; a ring with no ringing picture is the "on" one, as before). Where the set has none the one-cell classic one is drawn, then the modern one, then the characters.
2. **The strip module is two cells wide where the set has the wide telephone, and one cell where it has not** (a module's width is fixed when it registers, so it is the width of its widest picture).
   Modern style stays one cell, in the first of the two.
3. **No setting.** The picture follows Phone Style, and the owner's decision of 0.5.22 stands: there is no size to choose.

## Tests

`tests/bar.sh`: the wide classic telephone in each of the three states, with a caller on a ring; modern style draws the one-cell modern one though the wide one is there; classic without the wide glyphs draws
the one-cell classic one; the strip draws it and registers two cells where the set has it and one where it has not.
