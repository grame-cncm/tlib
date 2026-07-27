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
// 6. Lists, sets and environments
//-----------------------------------------------------------------------------

bool tourLists()
{
    bool ok = true;

    // A list is a TERM, so everything true of terms is true of lists : equal
    // lists are one object, and tails are shared.
    Tree l1 = list3(tree(1), tree(2), tree(3));
    Tree l2 = list3(tree(1), tree(2), tree(3));
    CHECK(l1 == l2);
    CHECK(len(l1) == 3 && hd(l1) == tree(1));
    CHECK(tl(l1) == tl(l2));
    CHECK(cons(tree(0), l1) != l1 && tl(cons(tree(0), l1)) == l1);  // tail shared

    // Sets are canonical ordered lists : the same elements give the same
    // pointer whatever order they were inserted in.
    Tree a = tree(symbol("elemA"));
    Tree b = tree(symbol("elemB"));
    Tree c = tree(symbol("elemC"));
    Tree s1 = list2set(list3(a, b, c));
    Tree s2 = list2set(list3(c, a, b));
    CHECK(s1 == s2);
    CHECK(list2set(list2set(list3(a, b, c))) == s1);  // idempotent
    CHECK(isElement(b, s1));
    CHECK(setUnion(s1, singleton(b)) == s1);  // b already in : union changes nothing
    CHECK(addElement(b, s1) == s1);           // no duplicate

    // Environments : a stack of bindings, where pushing shares the old one and
    // shadowing covers rather than removes.
    Tree v   = nullptr;
    Tree env = pushEnv(a, tree(1), pushEnv(b, tree(2), nil()));
    CHECK(searchEnv(a, v, env) && v == tree(1));
    CHECK(searchEnv(b, v, env) && v == tree(2));
    Tree shadowed = pushEnv(a, tree(9), env);
    CHECK(searchEnv(a, v, shadowed) && v == tree(9));  // the top binding wins
    CHECK(searchEnv(b, v, shadowed) && v == tree(2));  // the outer one still reachable

    return ok;
}

//-----------------------------------------------------------------------------
// 7. Signatures and opcodes
//-----------------------------------------------------------------------------

bool tourSignatures()
{
    bool ok = true;

    Signature sigA = signature("TourAlpha");
    Signature sigB = signature("TourBeta");

    Sym add = sigA.add("TourAlpha.Add");
    Sym sub = sigA.add("TourAlpha.Sub");

    // Local opcodes are DENSE : 0, 1, ... in order of first addition, which is
    // what lets a fold dispatch through a jump table.
    SymbolTag tAdd, tSub;
    CHECK(getSymbolTag(add, tAdd) && tAdd.localOpcode() == 0);
    CHECK(getSymbolTag(sub, tSub) && tSub.localOpcode() == 1);
    CHECK(tAdd.signature == sigA.identity());

    // add() is idempotent : declaring a language twice is harmless.
    CHECK(sigA.add("TourAlpha.Add") == add);

    // An ordinary symbol is simply not a constructor -- not an error.
    SymbolTag ordinary;
    CHECK(!getSymbolTag(symbol("TourOrdinarySymbol"), ordinary));

    // Ranges are DISJOINT : another signature restarts at local 0, but its
    // global opcode differs.
    Sym       otherAdd = sigB.add("TourBeta.Add");
    SymbolTag tOther;
    CHECK(getSymbolTag(otherAdd, tOther));
    CHECK(tOther.localOpcode() == 0);
    CHECK(tOther.signature != tAdd.signature);
    CHECK(tOther.opcode != tAdd.opcode);

    return ok;
}

//-----------------------------------------------------------------------------
// 8. Recursive terms
//-----------------------------------------------------------------------------

bool tourRecursion()
{
    bool ok = true;

    // de Bruijn form : no names at all, so two recursions that would differ
    // only by their variable name are literally the same term.
    Tree r1 = rec(tree(symbol("+"), tree(1), ref(1)));
    Tree r2 = rec(tree(symbol("+"), tree(1), ref(1)));
    CHECK(r1 == r2);

    // Aperture, the synthesized attribute : a bare reference is open, a binder
    // discharges it.
    CHECK(isOpen(ref(1)));
    CHECK(isClosed(r1));

    // deBruijn2Sym is CANONICAL : names are derived from the content, so the
    // same term converts to the same pointer every time.
    Tree s1 = deBruijn2Sym(r1);
    Tree s2 = deBruijn2Sym(r2);
    CHECK(s1 == s2);

    // In the symbolic form, rec and ref are THE SAME NODE : the definition is a
    // property, not a branch. This is what keeps the branches acyclic.
    Tree var = nullptr, body = nullptr;
    CHECK(isRec(s1, var, body));
    CHECK(body != nullptr);
    CHECK(ref(var) == s1);

    // A traversal that follows branches does NOT enter the recursion : the
    // symbolic node's only branch is its variable.
    CHECK(s1->arity() == 1 && s1->branch(0) == var);

    return ok;
}

//-----------------------------------------------------------------------------
// 9. Rewriting
//-----------------------------------------------------------------------------

bool tourRewriting()
{
    bool ok = true;

    auto identity = [](Tree n) { return n; };

    // Minimal reconstruction : when nothing changes, the SAME pointer comes
    // back -- no node is rebuilt.
    Tree t = tree(symbol("+"), tree(1), tree(2));
    CHECK(treeRewrite(t, identity) == t);

    // A local rule applied bottom-up : constant folding.
    Tree folded = treeRewrite(t, [](Tree n) {
        Tree x, y;
        int  i = 0, j = 0;
        if (isTree(n, Node(symbol("+")), x, y) && isInt(x->node(), &i) &&
            isInt(y->node(), &j)) {
            return tree(i + j);
        }
        return n;
    });
    CHECK(folded == tree(3));

    // Sharing is preserved : a subterm occurring twice is transformed once and
    // the two occurrences stay the same object.
    Tree shared = tree(symbol("pair"), t, t);
    Tree done   = treeRewrite(shared, [](Tree n) {
        Tree x, y;
        int  i = 0, j = 0;
        if (isTree(n, Node(symbol("+")), x, y) && isInt(x->node(), &i) &&
            isInt(y->node(), &j)) {
            return tree(i + j);
        }
        return n;
    });
    CHECK(done->branch(0) == done->branch(1));

    // THE surprising one : rewriting a recursive term RENAMES it. Under the
    // identity rule the result is alpha-equivalent, never equal -- forced by the
    // immutability of recursive definitions (a reused variable would be a
    // redefinition, which is fatal).
    Tree x = tree(symbol("TourX"));
    Tree r = rec(x, tree(symbol("+"), tree(1), ref(x)));
    Tree r2 = treeRewrite(r, identity);
    CHECK(r2 != r);
    CHECK(alphaEquiv(r2, r));

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
    r &= tourLists();
    r &= tourSignatures();
    r &= tourRecursion();
    r &= tourRewriting();
    // tourSession() ends the session it runs in : keep it last.
    r &= tourSession();

    tlib::cleanup();

    std::cout << (r ? "All tour claims hold\n" : "SOME TOUR CLAIMS FAILED\n");
    return r ? 0 : 1;
}
