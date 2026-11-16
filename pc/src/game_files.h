// Game files the PC build replaces without touching the player's copy (a
// folder or a disc image): built at start into the cache folder and linked in
// the game's file system where the game looks for them.
//
// - Always: the language files (data\loc\locale.big) in the PC's words
//   (game_text.h).
// - With PlayStation buttons: the button icons, the Side Select pads, the
//   controller pictures and the controller's name. The icons are the player's
//   own files with the PS3 version's pictures in (data\playstation_buttons.bin,
//   embedded in the exe); the controller pictures are the PS3 ones the Xbox
//   data already has. The game reads loose files before its archives when
//   d:\loose_files_first exists, so that one is linked too.
//
// button_icons: "auto" gives PlayStation buttons when a PlayStation controller
// is connected as the game starts (the icons can't change in a running game),
// or "xbox" / "playstation".

#pragma once

#include <rex/runtime.h>

void SetUpGameFiles(rex::Runtime* runtime);

// Whether this session has PlayStation buttons (decided once; the quit
// shortcut's icons follow it).
bool PlayStationButtons();
