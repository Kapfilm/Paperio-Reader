#pragma once
// Host stub for <I18n.h>. tr() yields the key name itself — user-facing strings
// never influence layout in the harness (they only appear in placeholder
// rendering, which the dump does not exercise), but the token keeps dump output
// readable if one ever leaks through.
#define tr(id) #id
