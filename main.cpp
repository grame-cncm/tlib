/*
 * TLIB : tree library
 * Copyright (C) 2003-2026 GRAME, Centre National de Creation Musicale
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <iostream>

#include "tests.hh"
#include "tlib.hh"

int main(int, const char**)
{
    std::cout << "Tests tlib library\n";

    tlib::init();

    bool r = true;

    r &= checkSymbols();
    r &= checkArithmeticSignatureFold();
    r &= checkNodes();
    r &= checkHashConsing();
    r &= checkAccessors();
    r &= checkConversions();
    r &= checkProperties();
    r &= checkTypedProperties();
    r &= checkLists();
    r &= checkSets();
    r &= checkEnvironments();
    // Note the single '&' : '&&' would short-circuit, so a failure in checkRecursiveTrees
    // would silently SKIP checkMutualRecursion instead of reporting it too.
    r &= checkRecursiveTrees() & checkMutualRecursion();
    r &= checkNormalizeRecGroups();
    r &= checkRecImmutability();
    r &= checkFixPoint();
    r &= checkFixPointInterval();
    r &= checkRewrite();
    r &= checkGuardedRewrite();
    r &= checkDnfCnf();
    r &= checkDescend();
    r &= checkErrorHandler();
    r &= checkHashTableGrowth();
    r &= checkLifecycle();  // last : it wipes the tree population

    if (r) {
        std::cout << "All tests passed\n";
        return 0;
    }
    std::cout << "SOME TESTS FAILED\n";
    return 1;
}
