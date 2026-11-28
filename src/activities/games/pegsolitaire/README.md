# Yoga / Йога (Peg Solitaire)

Native button adaptation of the MIT-licensed Peg Solitaire by Omer Faruk Inel.
See LICENSE.txt. No Lua runtime is required.

Entry point: `PegSolitaireActivity(renderer, mappedInput)`.
Translation additions are supplied in `translations.json` (`ru` and `en`).

- English cross: 33 holes, centre empty, straight orthogonal jumps.
- Triangle: 15 holes, apex empty, six legal jump directions.
- Four directions move the cursor. Select picks a movable peg, then a marked
  target. Back clears the selection; another Back opens the pause menu.
- Undo is available in the menu (also on the mapped Power button).
- Each board saves its own move sequence; switching boards resumes that board.
- Restart requires confirmation; back cancels. Help scrolls with Up/Down.
- A terminal board shows the remaining pegs, best result and whether one peg
  remains (perfect when it is in the initial hole). Undo still works.
- A save is written to `.tmp`, reread, checked, then installed using a `.bak`
  recovery file. Both backup and validated temporary files are considered on
  load. Invalid move sequences never overwrite the live board.
- SD failures are shown on the board; an unsuccessful explicit save prevents
  switching boards or exiting through the game's menu.

Tests cover all legal opening directions, diagonal restrictions, malformed
saves, complete one-peg wins and full undo on both boards, replay, and reachability
of every hole with the four direction buttons.
