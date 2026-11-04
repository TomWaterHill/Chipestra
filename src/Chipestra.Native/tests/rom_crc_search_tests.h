// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Tests of the CRC-32 search for ROM files whose names differ from the ones a game definition lists.

void RomCrcSearchFindsRenamedZipEntry();
void RomCrcSearchIsOnlyUsedWhenNoNameMatches();
void RomCrcSearchAcceptsAlternativeCrcValues();
void RomCrcSearchFallsBackToEntrySize();
void RomCrcSearchRefusesSeveralMatches();
void RomCrcSearchReportsMismatches();
void RomCrcSearchDefinitionAttributesReachTheResolver();
void RomCrcSearchRepositoryDefinitionsAreOptedIn();
void RomCrcSearchFindsRenamedRealRoms();
