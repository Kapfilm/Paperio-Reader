# Дело № / Case Files

Native button-controlled Paperio adaptation of the MIT-licensed Case Files by
Omer Faruk Inel: https://github.com/omer-faruq-xteink/touch-licorice-apps/tree/main/apps/casefiles

All 600 original cases are retained (200 each on Easy, Medium and Hard), including
all six settings: Seaview Hotel, Night Train, Village Bake-off, Old Library,
Harbour Morning and Museum Night. `CaseFilesData.h` records SHA-256 hashes of the
unmodified bank files. Regenerate with `scripts/convert_casefiles_data.py DIR`,
where DIR contains `casefiles-easy.txt`, `casefiles-medium.txt`, and
`casefiles-hard.txt` copied from the original app's `data/` folder.

The puzzle model is native C++; it does not run Lua or require XTOS. Russian and
English clue templates use category/value labels to avoid changing the meaning
through grammatical agreement. Either/or means exactly one; a paired clue links
two distinct people to the other two entities in an unknown order. Order and
numeric differences refer to the original complete category order (in Museum
Night the rounds go 21:00, 22:00, 23:00, 00:00, 01:00, 02:00).

Directions move the cursor. Confirm cycles empty → cross → tick → empty. Back
opens the menu. Tables, clues, undo, check, hint, case navigation, difficulty,
restart and help are accessible there. A clue is shown in full on its own screen;
Confirm crosses it out and the navigation buttons select the previous/next clue.
The selected grid cell's full row and column labels are repeated below the grid.
Ticks imply dots in the other cells of their row/column; dots are not stored marks.
Only the name tables must be correct to finish a case, as in the original.

State is versioned, checksummed, bounds-checked and written to
`/.crosspoint/casefiles.bin` through a verified temporary file and backup. It
includes current marks, protected hint ticks, crossed-out clues, 400 undo actions,
completed cases and the last case in each difficulty. A failed write is reported
and blocks voluntary navigation that would discard the current state.

Tests validate every clue against its hidden answer and enumerate all possible
category permutations to verify one solution for each of the 600 cases. Additional
tests exercise implicit dots, conflicts, hints, undo, completion records, save
round-trips, truncation, byte corruption, random payloads and clue semantics.

See LICENSE.txt for the original author's MIT notice.

Text layout was measured with the real Paperio UiFonts for all 600 cases in both
languages, every interface size and regular/bold faces. At a conservative 440 px
text width, the longest Russian clue takes five lines (170 px). The clue screen
also includes the setting's short story and the goal above the clue.
