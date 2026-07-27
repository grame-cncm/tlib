/*
 * TLIB : tree library
 * Copyright (C) 2003-2026 GRAME, Centre National de Creation Musicale
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

/**
 * The executable claims of A-GUIDED-TOUR-OF-TLIB.md.
 *
 * Every surprising assertion the guided tour makes about tlib's behaviour lives
 * here as a running check, and the tour quotes THIS file by line reference
 * rather than carrying dead excerpts. A claim that stops being true breaks a
 * test instead of quietly turning the document into fiction -- which is the
 * failure mode the tour's own method note (CONCEPT-TOUR-AUTHORING.md, "Keeping
 * it true") exists to prevent.
 *
 * Kept separate from tests.cpp on purpose : tests.cpp certifies the library,
 * this file certifies the document. They will drift apart -- the tour explains
 * a dozen concepts, the test suite covers everything -- and a claim removed
 * from the tour should be removable from here without touching the suite.
 */

#include <cmath>
#include <iostream>
#include <set>
#include <string>

#include "tlib.hh"

static bool checkAux(bool cond, const char* expr, const char* file, int line)
{
    if (!cond) {
        std::cerr << "FAILED : " << expr << " (" << file << ":" << line << ")\n";
    }
    return cond;
}
#define CHECK(cond) ok &= checkAux((cond), #cond, __FILE__, __LINE__)

//-----------------------------------------------------------------------------
// 2. Hash-consing and maximal sharing
//-----------------------------------------------------------------------------

/// Count the DISTINCT nodes reachable from t : the size of the shared DAG, as
/// opposed to the size of the term it denotes.
static void collect(Tree t, std::set<Tree>& seen)
{
    if (!seen.insert(t).second) {
        return;  // already counted : this is the sharing, made visible
    }
    for (int i = 0; i < t->arity(); i++) {
        collect(t->branch(i), seen);
    }
}

static std::size_t dagSize(Tree t)
{
    std::set<Tree> seen;
    collect(t, seen);
    return seen.size();
}

bool tourSharing()
{
    bool ok = true;

    // "two trees with the same content are never two objects -- they are one
    // object, pointed to twice"
    Tree a = tree(symbol("+"), tree(1), tree(2));
    Tree b = tree(symbol("+"), tree(1), tree(2));
    CHECK(a == b);

    // ... and structural difference is pointer difference, in the other direction
    CHECK(tree(symbol("+"), tree(1), tree(2)) != tree(symbol("+"), tree(2), tree(1)));

    // The tour's headline figure : 30 constructor calls denote a term with 2^30
    // leaves, and occupy 31 nodes.
    Tree t = tree(symbol("x"));
    for (int i = 0; i < 30; i++) {
        t = tree(symbol("Add"), t, t);
    }
    CHECK(dagSize(t) == 31);

    // Sharing is not planned by the caller : an INDEPENDENT reconstruction lands
    // on the very same object, whatever the construction history.
    Tree u = tree(symbol("x"));
    for (int i = 0; i < 30; i++) {
        u = tree(symbol("Add"), u, u);
    }
    CHECK(u == t);

    // Serials are a function of construction ORDER, so the rebuilt term reused
    // the existing nodes rather than allocating fresh ones.
    CHECK(u->serial() == t->serial());

    return ok;
}

//-----------------------------------------------------------------------------
// 3. Nodes and symbols
//-----------------------------------------------------------------------------

bool tourNodes()
{
    bool ok = true;

    // "TLIB compares what is stored, never what it might mean" : the tag is part
    // of the comparison, so 1 and 1.0 are different nodes.
    CHECK(Node(1) != Node(1.0));

    // Bitwise comparison of the payload, consequence 1 : +0.0 and -0.0 have
    // different bit patterns, so they are different nodes -- although IEEE says
    // +0.0 == -0.0.
    CHECK(0.0 == -0.0);
    CHECK(Node(0.0) != Node(-0.0));

    // Consequence 2, the one hash-consing DEPENDS on : IEEE equality is not
    // reflexive, but node equality must be, or the table below would never find
    // a NaN node it had just inserted and would allocate new ones forever.
    const double nan = std::nan("");
    CHECK(!(nan == nan));           // IEEE : a NaN is not equal to itself
    CHECK(Node(nan) == Node(nan));  // TLIB : same bits, same node
    CHECK(tree(Node(nan)) == tree(Node(nan)));

    // Interning : naming and interning are inverse to each other, and two symbols
    // differ exactly when their names differ.
    CHECK(symbol("frequency") == symbol("frequency"));
    CHECK(symbol("frequency") != symbol("gain"));
    CHECK(std::string(name(symbol("frequency"))) == "frequency");

    // ... except that names are NORMALISED on the way in : every character below
    // 32 becomes a space, so these two spellings denote one symbol.
    CHECK(symbol("a\nb") == symbol("a b"));

    // Generated names are fresh, and depend on session history -- which is why
    // nothing canonical can be built on them.
    CHECK(unique("W") != unique("W"));

    // Nothing constrains which node kinds may have children : an integer node
    // with two branches is accepted. Treating numbers as leaves is a convention
    // of every sane client, not a rule tlib enforces.
    Tree oddity = tree(Node(3), tree(1), tree(2));
    CHECK(oddity->arity() == 2);

    return ok;
}

//-----------------------------------------------------------------------------
// 4. The session memory model
//-----------------------------------------------------------------------------

bool tourSession()
{
    bool ok = true;

    // Within a session an address is handed out once and means the same term
    // forever : this is what makes pointer equality a sound test of term
    // equality, and it is stronger than mere injectivity.
    Tree before = tree(symbol("session-witness"), tree(1));
    CHECK(tree(symbol("session-witness"), tree(1)) == before);

    // cleanup() ends the session : every tree and symbol built above is freed in
    // one sweep, and 'before' MUST NOT be dereferenced past this line.
    tlib::cleanup();

    // The library is immediately ready for a new session, with its tables and
    // its internal caches reset (tlib.cpp : resetInternals).
    Tree after = tree(symbol("session-witness"), tree(1));
    CHECK(after != nullptr);
    CHECK(after->arity() == 1);

    // Serial numbers restart with the session, so the new tree is early in the
    // new numbering rather than continuing the old one.
    CHECK(after->serial() < 100);

    return ok;
}

//-----------------------------------------------------------------------------

int main(int, const char**)
{
    std::cout << "Executable claims of A-GUIDED-TOUR-OF-TLIB.md\n";

    tlib::init();

    bool r = true;
    // Single '&' : '&&' would short-circuit and silently SKIP later sections.
    r &= tourSharing();
    r &= tourNodes();
    // tourSession() ends the session it runs in : keep it last.
    r &= tourSession();

    tlib::cleanup();

    std::cout << (r ? "All tour claims hold\n" : "SOME TOUR CLAIMS FAILED\n");
    return r ? 0 : 1;
}
