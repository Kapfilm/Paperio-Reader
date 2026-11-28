# Delve for Paperio

C++ button adaptation of **Delve**, Copyright (c) 2026 Omer Faruk Inel,
from https://github.com/omer-faruq-xteink/touch-licorice-apps/tree/main/apps/delve.
The original game and pixel art are MIT licensed; the full notice is in
`LICENSE-Delve.txt`. Original main.lua file SHA: `5261e98e9eff301a79baae65effb3c9b7437f706`.

This adaptation keeps the ten-floor expedition, randomized rooms and corridors,
closed doors, fog of war, shrinking torch light, turn-based movement/combat,
nine monster kinds and their behaviour, experience levels, five potion effects,
six scroll effects, equipment swaps, a twelve-slot bag, gold, the final Amulet,
permadeath and daily seed. All interface, help, item and event text has Russian
and English translations.

The directional buttons move/attack/open doors. Back opens the pause menu; Confirm opens the bag directly; wait, rest, stairs and enemy inspection are available in the pause menu.
In the bag, Confirm opens Use / Drop / Back. No action requires the power button,
which can remain mapped to system sleep.

The port deliberately does not claim binary/save or seed compatibility with Lua:
room generation is native C++, daily runs are shared only between copies of this
Paperio implementation, and tap-to-pathfind was replaced with button movement.
Viewport size adapts to available screen space. The final Amulet can be collected
without fighting the dragon if the hero finds a way past it, like the original.
Unknown scroll labels use stable per-run letters rather than generated syllables.

Two alternating SD save slots store a versioned, bounded, checksummed explicit
little-endian format. A partial write leaves the older slot available. A newer
finished-run record prevents resurrecting a dead hero from an older active slot.
Saves occur every ten turns, on descent, opening the menu and leaving the activity.
A full SD card is reported on the play screen. Host tests exercise connectivity,
turn semantics, items, finished-run persistence, save roundtrips and corruption.

Game settings are available from the title and pause menus. Normal view keeps 40px tiles and the original compact HUD; Large uses 48px tiles and larger status text. The camera follows the hero in both modes. View size is saved in the dedicated `delve-ui` NVS namespace, independently of run saves.
