/*
 * TLIB : tree library
 * Copyright (C) 2003-2026 GRAME, Centre National de Creation Musicale
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#pragma once

bool checkSymbols();
bool checkArithmeticSignatureFold();
bool checkNodes();
bool checkHashConsing();
bool checkAccessors();
bool checkConversions();
bool checkProperties();
bool checkTypedProperties();
bool checkLists();
bool checkSets();
bool checkEnvironments();
bool checkRecursiveTrees();
bool checkMutualRecursion();
bool checkNormalizeRecGroups();
bool checkRecImmutability();
bool checkFixPoint();
bool checkFixPointInterval();
bool checkRewrite();
bool checkGuardedRewrite();
bool checkDnfCnf();
bool checkErrorHandler();
bool checkLifecycle();
bool checkHashTableGrowth();

bool checkDescend();
