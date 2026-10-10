// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Tom Water Hill
#pragma once

// Shared PASS/FAIL check helper for the native test executable
// (tests/native_lifecycle_tests.cpp and tests/resource_resolver_tests.cpp),
// shared by all test source files. Plain
// assert-style checks, not a general test framework - see
// native_lifecycle_tests.cpp's top-of-file comment for why.

#include <cstdio>

inline int g_failures = 0;

inline void Check(bool condition, const char* description) {
    if (condition) {
        std::printf("  PASS: %s\n", description);
    } else {
        std::printf("  FAIL: %s\n", description);
        ++g_failures;
    }
}
