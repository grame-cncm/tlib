/*
 * TLIB : tree library
 * Copyright (C) 2003-2026 GRAME, Centre National de Creation Musicale
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/*
 * A tour of recursive trees, in both representations :
 *
 *  - de Bruijn form 'rec(body)' / 'ref(n)' : nameless, canonical by
 *    hash-consing -- two alpha-equivalent recursive terms are the same
 *    pointer by construction ;
 *  - symbolic form 'rec(var, body)' / 'ref(var)' : named definitions,
 *    convenient for compilation and printing.
 *
 * deBruijn2Sym is CANONICAL : the symbolic names are derived from the content
 * hash of the de Bruijn tree, so converting the same tree any number of times
 * yields the very same pointer -- no persistent cache involved, and symbolic
 * trees obtained from alpha-equivalent de Bruijn trees fuse by hash-consing.
 * sym2deBruijn erases the names again : the round-trip is the identity.
 */

#include <iostream>
#include <string>

#include "tlib.hh"

static void printCheck(const char* label, bool ok, bool& allOk)
{
    allOk = allOk && ok;
    std::cout << "check " << label << ": " << (ok ? "yes" : "NO") << "\n";
}

int main()
{
    tlib::init();

    bool ok = true;

    Tree E = rec(tree(symbol("mix"), ref(1),
                      rec(tree(symbol("tap"), ref(1), ref(2),
                               rec(tree(symbol("hold"), ref(1), ref(2), ref(3))))),
                      tree(symbol("sum"), ref(1), rec(tree(symbol("echo"), ref(1), ref(2))))));
    Tree G = tree(symbol("foo"), E, E, E);

    std::cout << "E = " << toDeBruijnString(E) << "\n\n";
    std::cout << "G = " << toDeBruijnString(G) << "\n\n";

    // Canonical conversion : converting E three times yields the SAME tree,
    // pointer-equal -- the symbolic names are a pure function of the content.
    Tree S1 = deBruijn2Sym(E);
    Tree S2 = deBruijn2Sym(E);
    Tree S3 = deBruijn2Sym(E);

    std::cout << "S1 = deBruijn2Sym(E)\n" << toSymbolicString(S1) << "\n\n";

    printCheck("S1 == S2 (canonical conversion)", S1 == S2, ok);
    printCheck("S2 == S3 (canonical conversion)", S2 == S3, ok);
    std::cout << "\n";

    // Consequence : a tree built from several conversions of the same term
    // shares ONE definition group -- the fusion is free, by hash-consing.
    Tree C1 = tree(symbol("foo"), S1, S2, S3);
    std::cout << "C1 = foo(S1, S2, S3)\n" << toSymbolicString(C1) << "\n\n";
    printCheck("C1 branches all share the same definition group",
               C1->branch(0) == C1->branch(1) && C1->branch(1) == C1->branch(2), ok);
    std::cout << "\n";

    // The round-trip erases the names again and restores G exactly.
    Tree C1db = sym2deBruijn(C1);
    std::cout << "sym2deBruijn(C1) = " << toDeBruijnString(C1db) << "\n\n";
    printCheck("sym2deBruijn(C1) == G", C1db == G, ok);
    std::cout << "\n";

    // Converting G directly gives the same symbolic tree as C1 : canonicity
    // makes the conversion commute with tree construction.
    Tree GS = deBruijn2Sym(G);
    std::cout << "deBruijn2Sym(G)\n" << toSymbolicString(GS) << "\n\n";
    printCheck("deBruijn2Sym(G) == C1", GS == C1, ok);
    printCheck("deBruijn2Sym(G) preserves G branch sharing",
               GS->arity() == 3 && GS->branch(0) == GS->branch(1) &&
                   GS->branch(1) == GS->branch(2),
               ok);

    tlib::cleanup();
    return ok ? 0 : 1;
}
