# Kakuro / Какуро for Paperio

Native button adaptation of Kakuro by Omer Faruk Inel, MIT ©2026.
Source: https://github.com/omer-faruq-xteink/touch-licorice-apps/tree/main/apps/kakuro
Upstream revision: `04d46f0ddf56ad869ca05e42d9db7110f2b63533`.
The original license is preserved in `LICENSE`. Paperio keeps its existing Kakuro menu icon.

All 600 original puzzles are stored in firmware: 200 each on 6×6, 7×7 and 8×8 boards.
These replace the earlier independent 3×3 block puzzles. Clues are derived from each
bank's verified solution; run lengths and topology vary. Digits 1–9 must add up to each
across/down clue, with no repeated digit in a run.

The selected cell's across/down sums and possible digit combinations appear below
the grid. Repeated digits and completed runs with an incorrect sum are marked.
Hints fill a cell correctly and protect that digit. Direction buttons move the cursor;
Select opens the digit palette; Back returns to the game menu. The game menu offers
puzzle navigation, difficulty, undo, hint, restart and rules.

Native model tests validate all 600 definitions and the combinations, error detection,
hints and undo. All upstream banks have also been independently checked using the
author's run-reasoning solver. The old save format belongs to a different puzzle bank
and is not interpreted as progress in the replacement game.
