/*
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at
 * http://mozilla.org/MPL/2.0/.
 */
// sims app — TS1(sims) language: text of the UI the port adds itself.
//
// Translations for all strings used throughout the UI that are not in the game data.
//
// Since this game engine only has support for Latin-1, languages that cannot be used with this encoding are left out.
// One row per text, one column per language, with the following order: US English, UK English, French, German, Italian,
// Spanish, Dutch, Danish, Swedish, Norwegian, Finnish, Portuguese.
#pragma once

#include <string>

namespace simapp {

/// en must be the exact English literal at the call site.
std::string LangText(const std::string& en);

}
