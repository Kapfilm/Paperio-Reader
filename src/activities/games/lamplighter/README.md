# Электросеть / Lamplighter for Paperio

Native C++ button adaptation of Lamplighter by Omer Faruk Inel, MIT ©2026.
Original: https://github.com/omer-faruq-xteink/touch-licorice-apps/tree/main/apps/lamplighter

All 600 original puzzles are included in flash (200 each: 5×5, 7×7, 9×9). Packed data contains the solution/start masks in the high/low nibbles and a source index. Regenerate with `python3 scripts/convert_lamplighter_data.py BANK_DIRECTORY`; expected input names are `lamplighter-easy.txt`, `lamplighter-medium.txt`, `lamplighter-hard.txt`. SHA256 values are embedded beside the banks.

Directions move the cursor. Select rotates clockwise, or toggles a manual lock while Lock mode is enabled. Back opens the menu: resume, lock mode, undo, hint, next/previous puzzle, confirmed restart, difficulty, rules and exit. A solved board opens the next puzzle with Select. Hints stay correctly locked; undo skips their earlier turns instead of reverting a hinted tile to an incorrect locked orientation.

The original runtime Lua engine and generator are not needed. BFS computes power; victory requires every cell connected, no loose ends and a tree. Russian and English UI uses the common I18n system. Small explanatory text stays 8pt; board fits available dimensions independently of UI text size.

State, 600 completion bits and bounded undo history are saved in `/.crosspoint/lamplighter.bin`. Versioned checksummed data validates puzzle IDs, masks, hint locks, history bounds and progress before applying. Atomic `.tmp` + `.bak` replacement verifies data before replacing the existing save; load recovers the main, backup or temporary file. Menu exit and puzzle/difficulty changes are blocked on a failed current save. Reopening restarts at the source cursor; undo and game progress remain.

Tests in `test/lamplighter`: all 600 solution trees, valid shape orientations, solving with hints, controls/locks/undo, corrupted/truncated persistence and bounded history.
