// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Resource Resolver native tests. Declared here (rather than inside
// an anonymous namespace in resource_resolver_tests.cpp) so
// native_lifecycle_tests.cpp's main() can call them - see that file's
// top-of-file comment for why this project uses one shared test executable
// with a single main() instead of a full test framework.

void ResourceResolverLooseFileResolved();
void ResourceResolverMissing();
void ResourceResolverAmbiguous();
void ResourceResolverHashMismatch();
void ResourceResolverUnspecifiedHashNotRequired();
void ResourceResolverCorrectHashAccepted();
void ResourceResolverZipEntryResolved();
void ResourceResolverLooseAndZipBothCandidates();
void ResourceResolverSizeFilterNarrowsCandidates();
void ResourceResolverOptionalResourceMissingDoesNotBlock();
void ResourceResolverRejectsOversizedZipEntry();
void ResourceResolverDeterministicAcrossRepeatedResolves();
void ZipIndexRejectsPathTraversal();
void ZipIndexRejectsUnsupportedCompressionMethod();
void ZipIndexEnforcesUncompressedSizeCap();

// Archive-scoped resolution.
void ResourceResolverArchiveScopeDisambiguatesZips();
void ResourceResolverArchiveScopeDisambiguatesFolders();
void ResourceResolverArchiveScopeExcludesOtherArchives();
void ResourceResolverArchiveScopeWorksWithoutHash();
void ResourceResolverNoArchiveRemainsAmbiguousAcrossArchives();
void ResourceResolverArchiveScopeDeterministicAcrossRepeatedResolves();
void ResourceResolverArchiveScopeSameNameAtDifferentPathsIsAmbiguousWithPathDetail();
