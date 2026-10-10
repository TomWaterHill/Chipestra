// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// XML definition parser + game/track model native tests. Declared
// here so native_lifecycle_tests.cpp's main() can call them - see that
// file's top-of-file comment for why this project uses one shared test
// executable with a single main() instead of a full test framework.

void ParseGameDefinitionValidXml();
void ParseGameDefinitionMissingGameAttribute();
void ParseGameDefinitionMissingRomAttribute();
void ParseGameDefinitionMissingTrackAttribute();
void ParseGameDefinitionMissingTrackIdAttribute();
void ParseGameDefinitionManufacturerIsOptional();
void ParseGameDefinitionOptionalRomAttributesAreOptional();
void ParseGameDefinitionCrc32IsParsedAsHex();
void ParseGameDefinitionNoGameElement();
void ParseGameDefinitionRejectsOldRootElement();
void ParseGameDefinitionMalformedXml();
void ParseGameDefinitionRejectsDoctype();
void ParseGameDefinitionRejectsOversizedInput();
void ParseGameDefinitionDeterministicAcrossRepeatedParses();
void GameDefinitionBuildsRequiredResourcesForResolver();

// Archive attribute.
void ParseGameDefinitionArchiveIsOptional();
void ParseGameDefinitionArchiveIsParsedWhenPresent();
void GameDefinitionBuildsRequiredResourcesWithArchiveScope();

// Control-command / loops track attributes.
void ParseGameDefinitionTrackTypeIsOptional();
void ParseGameDefinitionTrackTypeControlWhenPresent();
void ParseGameDefinitionTrackTypeWsgWhenPresent();
void ParseGameDefinitionTrackTypeUnknownValueIsRejected();
void ParseGameDefinitionTrackLoopsIsOptional();
void ParseGameDefinitionTrackLoopsFalseWhenPresent();
