/*
 * TLIB : tree library
 * Copyright (C) 2003-2026 GRAME, Centre National de Creation Musicale
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */

#include <cstring>
#include <iostream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "dcond.hh"
#include "fixpoint.hh"
#include "occur.hh"
#include "recursive-print.hh"
#include "tests.hh"
#include "tlib.hh"

// Minimal check helper : prints the failing expression with its location.
static bool checkAux(bool cond, const char* expr, const char* file, int line)
{
    if (!cond) {
        std::cerr << "FAILED : " << expr << " (" << file << ":" << line << ")\n";
    }
    return cond;
}
#define CHECK(cond) ok &= checkAux((cond), #cond, __FILE__, __LINE__)

//-----------------------------------------------------------------------------
// Symbols
//-----------------------------------------------------------------------------

bool checkSymbols()
{
    bool ok = true;

    // interning : same name => same symbol, different name => different symbol
    CHECK(symbol("foo") == symbol("foo"));
    CHECK(symbol("foo") != symbol("bar"));
    CHECK(std::string(name(symbol("foo"))) == "foo");

    // unique() mints fresh names
    Sym u1 = unique("fresh");
    Sym u2 = unique("fresh");
    CHECK(u1 != u2);
    CHECK(std::string(name(u1)) != std::string(name(u2)));

    // user data
    int dummy = 42;
    setUserData(symbol("foo"), &dummy);
    CHECK(getUserData(symbol("foo")) == &dummy);
    setUserData(symbol("foo"), nullptr);

    // Ordinary symbols remain compatible and unsigned. A failed lookup leaves
    // its output untouched so callers can distinguish absence without a
    // sentinel opcode, since zero is valid for the first constructor.
    Sym       ordinary = symbol("ordinary-symbol");
    SymbolTag tag {symbol("untouched-signature"), 99};
    CHECK(!getSymbolTag(ordinary, tag));
    CHECK(tag.signature == symbol("untouched-signature") && tag.opcode == 99);

    // checkSymbols is the first test of a fresh session, so these first three
    // signatures also verify base(S_i) = 256 * i in creation order.
    auto signal     = signature("TestSignal");
    auto sameSignal = signature("TestSignal");
    CHECK(signal.identity() == sameSignal.identity());

    Sym input = signal.add("TestSigInput");
    CHECK(signal.add("TestSigInput") == input);  // idempotent, consumes no opcode
    Sym delay1 = sameSignal.add("TestSigDelay1");
    Sym delay  = signal.add("TestSigDelay");

    SymbolTag inputTag;
    SymbolTag delay1Tag;
    SymbolTag delayTag;
    CHECK(getSymbolTag(input, inputTag));
    CHECK(getSymbolTag(delay1, delay1Tag));
    CHECK(getSymbolTag(delay, delayTag));
    CHECK(inputTag.signature == signal.identity());
    CHECK(kOpcodesPerSignature == 256);
    CHECK(inputTag.opcode == 0);
    CHECK(inputTag.localOpcode() == 0);
    CHECK(delay1Tag.localOpcode() == 1);
    CHECK(delayTag.localOpcode() == 2);
    CHECK(delay1Tag.opcode == inputTag.opcode + 1);
    CHECK(delayTag.opcode == inputTag.opcode + 2);

    // Signature metadata uses dedicated fields and therefore never consumes
    // or changes the legacy user-data slot.
    int taggedData = 17;
    setUserData(input, &taggedData);
    CHECK(getUserData(input) == &taggedData);
    CHECK(getSymbolTag(input, tag));
    CHECK(tag.signature == signal.identity() && tag.opcode == inputTag.opcode);

    // Distinct signatures reserve disjoint ranges. A symbol cannot move from
    // one signature to another, and rejection preserves its original tag.
    auto other      = signature("TestOther");
    Sym  otherFirst = other.add("TestOtherFirst");
    SymbolTag otherTag;
    CHECK(getSymbolTag(otherFirst, otherTag));
    CHECK(otherTag.signature == other.identity());
    CHECK(otherTag.opcode == 256);
    CHECK(otherTag.localOpcode() == 0);
    CHECK(otherTag.opcode - otherTag.localOpcode() !=
          inputTag.opcode - inputTag.localOpcode());

    bool conflictRejected = false;
    try {
        other.add("TestSigInput");
    } catch (const std::runtime_error&) {
        conflictRejected = true;
    }
    CHECK(conflictRejected);
    CHECK(getSymbolTag(input, tag));
    CHECK(tag.signature == signal.identity() && tag.opcode == inputTag.opcode);

    // Signature identities share the historical symbol namespace. Reusing an
    // ordinary symbol as an identity, or using that identity as a constructor,
    // is safe because these two roles are stored independently.
    Sym preexistingIdentity = symbol("TestPreexistingSignature");
    auto preexistingSignature = signature("TestPreexistingSignature");
    CHECK(preexistingSignature.identity() == preexistingIdentity);
    CHECK(signal.add("TestPreexistingSignature") == preexistingIdentity);
    Sym preexistingMember = preexistingSignature.add("TestPreexistingMember");
    CHECK(preexistingMember == symbol("TestPreexistingMember"));
    CHECK(getSymbolTag(preexistingIdentity, tag));
    CHECK(tag.signature == signal.identity() && tag.localOpcode() == 3);
    CHECK(getSymbolTag(preexistingMember, tag));
    CHECK(tag.signature == preexistingSignature.identity());
    CHECK(tag.opcode == 512 && tag.localOpcode() == 0);

    // Capacity is exactly one complete byte of local opcodes. The 257th name
    // is rejected, while an idempotent lookup still succeeds after saturation.
    auto full = signature("TestFullSignature");
    Sym  firstFull = nullptr;
    Sym  lastFull  = nullptr;
    for (int i = 0; i < 256; ++i) {
        Sym constructor = full.add("TestFullConstructor" + std::to_string(i));
        firstFull       = firstFull ? firstFull : constructor;
        lastFull        = constructor;
    }
    SymbolTag firstFullTag;
    SymbolTag lastFullTag;
    CHECK(getSymbolTag(firstFull, firstFullTag));
    CHECK(getSymbolTag(lastFull, lastFullTag));
    CHECK(firstFullTag.localOpcode() == 0);
    CHECK(lastFullTag.localOpcode() == 255);
    CHECK(lastFullTag.opcode == firstFullTag.opcode + 255);
    CHECK(full.add("TestFullConstructor255") == lastFull);

    bool capacityRejected = false;
    try {
        full.add("TestFullConstructor256");
    } catch (const std::runtime_error&) {
        capacityRejected = true;
    }
    CHECK(capacityRejected);
    CHECK(!getSymbolTag(symbol("TestFullConstructor256"), tag));

    return ok;
}

//-----------------------------------------------------------------------------
// Arithmetic signature : executable version of SIGNATURE-SPEC.md
//-----------------------------------------------------------------------------

namespace {

// One algebraic interface fixes the operation names and arities while T
// selects the carrier in which the arithmetic signature is interpreted.
template <typename T>
class ArithmeticAlgebra {
   public:
    using Value = T;

    virtual ~ArithmeticAlgebra() = default;

    virtual T Number(double x)     = 0;
    virtual T Add(T x, T y)        = 0;
    virtual T Sub(T x, T y)        = 0;
    virtual T Mul(T x, T y)        = 0;
    virtual T Div(T x, T y)        = 0;
};

// The primitive algebra owns the registered constructors and builds free
// hash-consed terms. Its fold is the morphism from those terms to any other
// ArithmeticAlgebra carrier.
class ArithmeticTreeAlgebra : public ArithmeticAlgebra<Tree> {
   private:
    Signature fSignature = signature("Arithmetic");
    Sym       fAdd       = fSignature.add("Arithmetic.Add");
    Sym       fSub       = fSignature.add("Arithmetic.Sub");
    Sym       fMul       = fSignature.add("Arithmetic.Mul");
    Sym       fDiv       = fSignature.add("Arithmetic.Div");

   public:
    Tree Number(double x) override { return tree(x); }
    Tree Add(Tree x, Tree y) override { return tree(fAdd, x, y); }
    Tree Sub(Tree x, Tree y) override { return tree(fSub, x, y); }
    Tree Mul(Tree x, Tree y) override { return tree(fMul, x, y); }
    Tree Div(Tree x, Tree y) override { return tree(fDiv, x, y); }

    /**
     * Interpret a valid primitive arithmetic term in \p algebra.
     *
     * Numeric atoms are injected directly; binary nodes are checked against
     * this signature, folded bottom-up, then dispatched by dense local opcode.
     */
    template <typename Algebra>
    typename Algebra::Value fold(Tree expression, Algebra& algebra) const
    {
        double number;
        if (isDouble(expression->node(), &number)) {
            return algebra.Number(number);
        }

        Sym       constructor;
        SymbolTag tag;
        if (!isSym(expression->node(), &constructor) || !getSymbolTag(constructor, tag) ||
            tag.signature != fSignature.identity() || expression->arity() != 2) {
            tlib::error("invalid arithmetic expression");
        }

        auto x = fold(expression->branch(0), algebra);
        auto y = fold(expression->branch(1), algebra);

        switch (tag.localOpcode()) {
            case 0: return algebra.Add(x, y);
            case 1: return algebra.Sub(x, y);
            case 2: return algebra.Mul(x, y);
            case 3: return algebra.Div(x, y);
            default: tlib::error("unknown arithmetic opcode");
        }
    }
};

// This second algebra gives the same operations their usual numeric meaning,
// demonstrating that the fold changes interpretation without changing syntax.
class ArithmeticEvalAlgebra : public ArithmeticAlgebra<double> {
   public:
    double Number(double x) override { return x; }
    double Add(double x, double y) override { return x + y; }
    double Sub(double x, double y) override { return x - y; }
    double Mul(double x, double y) override { return x * y; }
    double Div(double x, double y) override { return x / y; }
};

}  // namespace

bool checkArithmeticSignatureFold()
{
    bool ok = true;

    ArithmeticTreeAlgebra syntax;
    ArithmeticEvalAlgebra evaluation;

    // This is the exact example from the specification: the primitive algebra
    // reconstructs the same hash-consed term, while the numeric algebra yields 20.
    Tree expression =
        syntax.Mul(syntax.Add(syntax.Number(2), syntax.Number(3)), syntax.Number(4));
    CHECK(syntax.fold(expression, syntax) == expression);
    CHECK(syntax.fold(expression, evaluation) == 20);

    // Exercise the two remaining constructors so every registered opcode is
    // covered by both reconstruction and evaluation.
    Tree allOperations = syntax.Div(
        syntax.Mul(syntax.Add(syntax.Number(2), syntax.Number(3)),
                   syntax.Sub(syntax.Number(10), syntax.Number(2))),
        syntax.Number(2));
    CHECK(syntax.fold(allOperations, syntax) == allOperations);
    CHECK(syntax.fold(allOperations, evaluation) == 20);

    return ok;
}

//-----------------------------------------------------------------------------
// Nodes
//-----------------------------------------------------------------------------

bool checkNodes()
{
    bool ok = true;

    int    i = 0;
    double d = 0;
    Sym    s = nullptr;
    void*  p = nullptr;

    CHECK(isInt(Node(5), &i) && i == 5);
    CHECK(isDouble(Node(2.5), &d) && d == 2.5);
    CHECK(isSym(Node(symbol("n")), &s) && s == symbol("n"));
    CHECK(isPointer(Node((void*)&i), &p) && p == &i);

    CHECK(Node(5) == Node(5));
    CHECK(Node(5) != Node(6));
    CHECK(Node(5) != Node(5.0));  // int and double are distinct node types

    // arithmetic on nodes, with int/double promotion
    CHECK(addNode(Node(2), Node(3)) == Node(5));
    CHECK(addNode(Node(2), Node(0.5)) == Node(2.5));
    CHECK(mulNode(Node(4), Node(4)) == Node(16));
    CHECK(isZero(Node(0)) && isZero(Node(0.0)) && !isZero(Node(1)));
    CHECK(isOne(Node(1)) && isMinusOne(Node(-1)));
    CHECK(sameMagnitude(Node(-3), Node(3.0)));

    return ok;
}

//-----------------------------------------------------------------------------
// Hash-consing : the central invariant, p != q <=> *p != *q
//-----------------------------------------------------------------------------

bool checkHashConsing()
{
    bool ok = true;

    // same content => same pointer
    CHECK(tree(1) == tree(1));
    CHECK(tree(symbol("x")) == tree(symbol("x")));
    CHECK(tree(symbol("+"), tree(1), tree(2)) == tree(symbol("+"), tree(1), tree(2)));

    // different content => different pointer
    CHECK(tree(1) != tree(2));
    CHECK(tree(symbol("+"), tree(1), tree(2)) != tree(symbol("+"), tree(2), tree(1)));
    CHECK(tree(symbol("+"), tree(1), tree(2)) != tree(symbol("*"), tree(1), tree(2)));

    // shared subtrees are physically shared
    Tree a = tree(symbol("+"), tree(1), tree(2));
    Tree b = tree(symbol("*"), a, a);
    CHECK(b->branch(0) == b->branch(1));

    // serial numbers give a stable deterministic order
    Tree t1 = tree(symbol("first"), tree(101));
    Tree t2 = tree(symbol("second"), tree(102));
    CHECK(t1->serial() < t2->serial());
    CHECK(std::less<CTree*>()(t1, t2));

    return ok;
}

//-----------------------------------------------------------------------------
// Accessors and pattern matching
//-----------------------------------------------------------------------------

bool checkAccessors()
{
    bool ok = true;

    Tree t = tree(symbol("op"), tree(1), tree(2), tree(3));
    CHECK(t->arity() == 3);
    CHECK(t->branch(0) == tree(1));
    CHECK(t->branch(2) == tree(3));
    CHECK(t->node() == Node(symbol("op")));

    Tree x = nullptr, y = nullptr;
    CHECK(isTree(tree(symbol("pair"), tree(7), tree(8)), symbol("pair"), x, y));
    CHECK(x == tree(7) && y == tree(8));
    CHECK(!isTree(tree(symbol("pair"), tree(7), tree(8)), symbol("other"), x, y));

    return ok;
}

//-----------------------------------------------------------------------------
// Conversions
//-----------------------------------------------------------------------------

bool checkConversions()
{
    bool ok = true;

    CHECK(tree2int(tree(42)) == 42);
    CHECK(tree2int(tree(42.0)) == 42);  // double casted to int
    CHECK(tree2double(tree(2.5)) == 2.5);
    CHECK(tree2double(tree(2)) == 2.0);  // int promoted to double
    CHECK(strcmp(tree2str(tree(symbol("hello"))), "hello") == 0);

    int dummy = 0;
    CHECK(tree2ptr(tree(Node((void*)&dummy))) == &dummy);

    return ok;
}

//-----------------------------------------------------------------------------
// Raw properties on trees
//-----------------------------------------------------------------------------

bool checkProperties()
{
    bool ok = true;

    Tree t   = tree(symbol("host"), tree(1));
    Tree key = tree(symbol("mykey"));

    CHECK(t->getProperty(key) == nullptr);
    t->setProperty(key, tree(99));
    CHECK(t->getProperty(key) == tree(99));

    // hash-consing means the property is visible through any alias of t
    CHECK(tree(symbol("host"), tree(1))->getProperty(key) == tree(99));

    t->clearProperty(key);
    CHECK(t->getProperty(key) == nullptr);

    // the fast-property slot is independent from the property list
    t->setFastProperty(tree(7));
    CHECK(t->getFastProperty() == tree(7));
    CHECK(t->getProperty(key) == nullptr);

    return ok;
}

//-----------------------------------------------------------------------------
// Typed property<P> and binary property2<Tree>
//-----------------------------------------------------------------------------

bool checkTypedProperties()
{
    bool ok = true;

    property<int> counts;
    Tree          t = tree(symbol("node"), tree(3));
    int           v = 0;
    CHECK(!counts.get(t, v));
    counts.set(t, 5);
    CHECK(counts.get(t, v) && v == 5);
    counts.clear(t);
    CHECK(!counts.get(t, v));

    // property2 : memoize f(a, b) with 'a' revisited under several 'b'
    property2<Tree> memo;
    Tree            a  = tree(symbol("stable"));
    Tree            b1 = tree(symbol("env1"));
    Tree            b2 = tree(symbol("env2"));
    Tree            r  = nullptr;

    CHECK(!memo.get(a, b1, r));
    memo.set(a, b1, tree(10));
    memo.set(a, b2, tree(20));  // second distinct 'b' : inline slot promoted to map
    CHECK(memo.get(a, b1, r) && r == tree(10));
    CHECK(memo.get(a, b2, r) && r == tree(20));
    CHECK(!memo.get(a, tree(symbol("env3")), r));
    memo.set(a, b1, tree(11));  // overwrite
    CHECK(memo.get(a, b1, r) && r == tree(11));
    memo.clear(a);
    CHECK(!memo.get(a, b1, r));

    return ok;
}

//-----------------------------------------------------------------------------
// Lists
//-----------------------------------------------------------------------------

static Tree double_(Tree t)
{
    return tree(t->node().getInt() * 2);
}

bool checkLists()
{
    bool ok = true;

    CHECK(isNil(nil()));
    CHECK(!isList(nil()));
    CHECK(len(nil()) == 0);

    Tree l = list3(tree(1), tree(2), tree(3));
    CHECK(isList(l) && !isNil(l));
    CHECK(len(l) == 3);
    CHECK(hd(l) == tree(1));
    CHECK(hd(tl(l)) == tree(2));
    CHECK(nth(l, 2) == tree(3));
    CHECK(isNil(nth(l, 10)));

    CHECK(reverse(l) == list3(tree(3), tree(2), tree(1)));
    CHECK(concat(list2(tree(1), tree(2)), list1(tree(3))) == l);
    CHECK(replace(l, 1, tree(9)) == list3(tree(1), tree(9), tree(3)));
    CHECK(lmap(double_, l) == list3(tree(2), tree(4), tree(6)));
    CHECK(lrange(l, 1, 3) == list2(tree(2), tree(3)));

    return ok;
}

//-----------------------------------------------------------------------------
// Sets (ordered lists without duplicates)
//-----------------------------------------------------------------------------

bool checkSets()
{
    bool ok = true;

    Tree s = list2set(list3(tree(1), tree(2), tree(1)));
    CHECK(len(s) == 2);
    CHECK(isElement(tree(1), s) && isElement(tree(2), s) && !isElement(tree(3), s));

    Tree s1 = addElement(tree(1), addElement(tree(2), nil()));
    Tree s2 = addElement(tree(2), addElement(tree(3), nil()));

    // sets are canonical : same elements => same tree, whatever insertion order
    CHECK(s1 == addElement(tree(2), addElement(tree(1), nil())));

    Tree u = setUnion(s1, s2);
    CHECK(len(u) == 3);
    Tree i = setIntersection(s1, s2);
    CHECK(len(i) == 1 && isElement(tree(2), i));
    Tree d = setDifference(s1, s2);
    CHECK(len(d) == 1 && isElement(tree(1), d));

    return ok;
}

//-----------------------------------------------------------------------------
// Environments
//-----------------------------------------------------------------------------

bool checkEnvironments()
{
    bool ok = true;

    Tree env = pushEnv(tree(symbol("x")), tree(1), nil());
    env      = pushEnv(tree(symbol("y")), tree(2), env);
    env      = pushEnv(tree(symbol("x")), tree(3), env);  // shadows the first x

    Tree v = nullptr;
    CHECK(searchEnv(tree(symbol("x")), v, env) && v == tree(3));
    CHECK(searchEnv(tree(symbol("y")), v, env) && v == tree(2));
    CHECK(!searchEnv(tree(symbol("z")), v, env));

    return ok;
}

//-----------------------------------------------------------------------------
// Recursive trees : de Bruijn <-> symbolic
//-----------------------------------------------------------------------------

bool checkRecursiveTrees()
{
    bool ok = true;

    // alpha-equivalence for free : two identical de Bruijn recursions share
    Tree r1 = rec(tree(symbol("f"), ref(1)));
    Tree r2 = rec(tree(symbol("f"), ref(1)));
    CHECK(r1 == r2);

    // aperture : a lone reference is open, the enclosing rec closes it
    CHECK(isOpen(ref(1)));
    CHECK(isClosed(r1));

    Tree body = nullptr;
    CHECK(isRec(r1, body));
    int level = 0;
    CHECK(isRef(ref(2), level) && level == 2);

    // lift increments free references only
    Tree l = lift(ref(1));
    CHECK(isRef(l, level) && level == 2);
    CHECK(lift(r1) == r1);  // closed tree : nothing to lift

    // conversion to symbolic representation
    Tree s = deBruijn2Sym(r1);
    Tree var = nullptr, sbody = nullptr;
    CHECK(isRec(s, var, sbody));
    CHECK(isTree(sbody, symbol("f")));
    CHECK(toDeBruijnString(r1) == "rec(f(ref(1)))");
    Tree back = sym2deBruijn(s);
    CHECK(back == r1);
    CHECK(sym2deBruijn(s) == back);  // deterministic : hash-consing gives the same tree
    CHECK(areEquiv(r1, s));

    // symbolic references
    Tree id = tree(unique("R"));
    CHECK(isRef(ref(id), var) && var == id);

    Tree x = tree(unique("X"));
    Tree y = tree(unique("Y"));
    Tree z = tree(unique("Z"));
    Tree sx = rec(x, tree(symbol("f"), ref(x)));
    Tree sy = rec(y, tree(symbol("f"), ref(y)));
    Tree sz = rec(z, tree(symbol("g"), ref(z)));
    CHECK(sx != sy);
    CHECK(areEquiv(sx, sy));
    CHECK(!areEquiv(sx, sz));

    Tree px = tree(symbol("x"));
    Tree py = tree(symbol("y"));
    Tree psx = rec(px, tree(symbol("f"), ref(px)));
    Tree psy = rec(py, tree(symbol("f"), ref(py)));
    (void)psy;
    CHECK(toSymbolicString(psx) == "x\nwith {\n  x := f(x)\n}");

    Tree sharedRoot = tree(symbol("h"), ref(px), ref(py), ref(px));
    CHECK(toSymbolicString(sharedRoot) ==
          "h(x, y, x)\nwith {\n  x := f(x)\n  y := f(y)\n}");

    // Custom recursive pretty-printer: recursion management is supplied by
    // tlib while the caller remains responsible for ordinary node syntax.
    Tree customInner = rec(py, tree(symbol("inner"), ref(px), ref(py)));
    Tree customOuter = rec(px, tree(symbol("outer"), ref(px), customInner));
    std::ostringstream customOut;
    {
        RecursivePrintSession session;
        std::function<void(std::ostream&, Tree)> printNode;
        printNode = [&printNode](std::ostream& out, Tree t) {
            Tree var, body;
            if (isRec(t, var, body)) {
                RecursivePrintSession::reference(out, var, body);
                return;
            }
            out << t->node();
            if (t->arity() > 0) {
                out << "[";
                for (int i = 0; i < t->arity(); ++i) {
                    if (i > 0) out << ";";
                    printNode(out, t->branch(i));
                }
                out << "]";
            }
        };
        printNode(customOut, customOuter);
        session.finish(customOut, [&printNode](std::ostream& out, Tree, Tree body) {
            printNode(out, body);
        });
    }
    CHECK(customOut.str() ==
          "x\nwith {\n  x := outer[x;y]\n  y := inner[x;y]\n}");

    // A completed outer session must not leak definitions into the next one.
    std::ostringstream isolatedOut;
    {
        RecursivePrintSession session;
        isolatedOut << "plain";
        session.finish(isolatedOut, [](std::ostream&, Tree, Tree) {});
    }
    CHECK(isolatedOut.str() == "plain");

    // nested recursion : ref(1) points to the inner rec, ref(2) to the outer rec
    Tree nested = rec(tree(symbol("outer"), ref(1), rec(tree(symbol("inner"), ref(1), ref(2)))));
    Tree snested = deBruijn2Sym(nested);
    CHECK(sym2deBruijn(snested) == nested);
    CHECK(areEquiv(nested, snested));

    // Symbolic conversion demo:
    // E is a closed de Bruijn recursion with two recursive nesting levels.
    // G shares the same E three times, while C1 uses three symbolic copies of E
    // produced by separate deBruijn2Sym calls.
    Tree E  = rec(tree(symbol("mix"), ref(1),
                       rec(tree(symbol("tap"), ref(1), ref(2),
                                rec(tree(symbol("hold"), ref(1), ref(2), ref(3))))),
                       tree(symbol("sum"), ref(1), rec(tree(symbol("echo"), ref(1), ref(2))))));
    Tree G  = tree(symbol("foo"), E, E, E);
    Tree S1 = deBruijn2Sym(E);
    Tree S2 = deBruijn2Sym(E);
    Tree S3 = deBruijn2Sym(E);
    CHECK(S1 != S2 && S1 != S3 && S2 != S3);
    CHECK(isClosed(E));
    CHECK(toDeBruijnString(E).find("ref(3)") != std::string::npos);

    Tree C1 = tree(symbol("foo"), S1, S2, S3);
    CHECK(sym2deBruijn(C1) == G);
    CHECK(areEquiv(deBruijn2Sym(G), C1));

    Tree sharedSym = deBruijn2Sym(G);
    CHECK(sharedSym->arity() == 3);
    CHECK(sharedSym->branch(0) == sharedSym->branch(1));
    CHECK(sharedSym->branch(1) == sharedSym->branch(2));
    CHECK(areEquiv(sharedSym, C1));

    return ok;
}

//-----------------------------------------------------------------------------
// Bottom-up rewriting (treeRewrite / treeRewriteInPlace, see REWRITE-SPEC.md)
//-----------------------------------------------------------------------------

bool checkMutualRecursion()
{
    bool ok = true;

    // Two mutually recursive singletons A = f(B, A), B = g(A) referencing a
    // shared closed subtree, plus an independent lower component C = h(C)
    // used by both. Exercises the component-based sym2deBruijn: C converts
    // once (closed memo), A and B inline each other (same component).
    Tree c  = tree(unique("C"));
    Tree rc = rec(c, tree(symbol("h"), ref(c)));

    Tree shared = tree(symbol("s"), rc);  // closed subtree shared by A and B

    Tree a = tree(unique("A"));
    Tree b = tree(unique("B"));
    // build the mutual knot: refs first, definitions attached afterwards
    Tree ra = rec(a, tree(symbol("f"), ref(b), ref(a), shared));
    Tree rb = rec(b, tree(symbol("g"), ref(a), shared));

    Tree root = tree(symbol("top"), ra, rb, shared);

    // conversion terminates and the result is closed
    Tree d = sym2deBruijn(root);
    CHECK(isClosed(d));

    // deterministic and idempotent through the round-trip: the de Bruijn
    // form is canonical, so converting the round-tripped symbolic form
    // gives the same hash-consed tree
    Tree s2 = deBruijn2Sym(d);
    CHECK(sym2deBruijn(s2) == d);

    // alpha-equivalence: same knot built with fresh variable names
    Tree c2  = tree(unique("C"));
    Tree rc2 = rec(c2, tree(symbol("h"), ref(c2)));
    Tree shared2 = tree(symbol("s"), rc2);
    Tree a2 = tree(unique("A"));
    Tree b2 = tree(unique("B"));
    Tree ra2 = rec(a2, tree(symbol("f"), ref(b2), ref(a2), shared2));
    Tree rb2 = rec(b2, tree(symbol("g"), ref(a2), shared2));
    Tree root2 = tree(symbol("top"), ra2, rb2, shared2);
    CHECK(root2 != root);              // different names, different symbolic trees
    CHECK(sym2deBruijn(root2) == d);   // same canonical de Bruijn form

    // Synthesized kContainsRec bit : true iff a recursive node occurs, in EITHER notation
    CHECK(tree(symbol("k"), tree(1))->isRecFree());  // plain tree
    CHECK(!ra->isRecFree());                         // a symbolic rec itself
    CHECK(!tree(symbol("w"), ref(a))->isRecFree());  // contains a symbolic ref
    CHECK(!d->isRecFree());                          // deBruijn recs count too
    CHECK(ra->containsRec() != ra->isRecFree());     // the two accessors are dual

    // Invariance by sym2deBruijn is now a COROLLARY of that bit, hence coarser : a
    // canonical deBruijn form is still a fixed point of the conversion, it merely no
    // longer takes the shortcut. The theorem that matters is the second line, and it holds.
    CHECK(!isSym2deBruijnInvariant(d));  // coarser than before : d holds deBruijn recs
    CHECK(sym2deBruijn(d) == d);         // yet still a fixed point of the conversion

    // The synthesized attribute must agree with an independent traversal on EVERY tree
    // the whole test suite has built so far.
    CHECK(CTree::checkContainsInvariant() == 0);

    // RecPlan : the component partition, exercised directly. A and B are mutually
    // recursive (one component), C = h(C) is an independent lower component, and a plain
    // node is not registered at all.
    RecPlan plan(root);
    CHECK(plan.sccOf(ra) == plan.sccOf(rb));  // A and B : same component
    CHECK(plan.sccOf(rc) != plan.sccOf(ra));  // C : a different component
    CHECK(plan.sccOf(rc) >= 0);               // but still registered
    CHECK(plan.sccOf(shared) == -1);          // a non-recursive node : unregistered

    // components() are dependencies-first : {A,B} reference C (through shared), so C's
    // component must come before theirs, hence a strictly smaller id.
    CHECK(plan.sccOf(rc) < plan.sccOf(ra));
    CHECK(plan.components().size() == 2);                        // {C} and {A,B}
    CHECK(plan.components()[plan.sccOf(rc)].size() == 1);        // C alone
    CHECK(plan.components()[plan.sccOf(ra)].size() == 2);        // A and B together

    // proj : project a component out of a recursive group, and read it back
    Tree p = proj(1, ra);
    int  pi = -1;
    Tree pg = nullptr;
    CHECK(isProj(p, pi, pg));            // recognised
    CHECK(pi == 1 && pg == ra);          // index and group recovered
    CHECK(!isProj(ra, pi, pg));          // a rec node is not a projection
    CHECK(proj(1, ra) == p);             // hash-consed : same projection, same tree

    return ok;
}

//-----------------------------------------------------------------------------
// Immutability of recursive definitions (transition regime, see tree.hh) :
// same-body redefinition is an idempotent no-op ; different-body redefinition
// and erasure (rec(id, nil)) feed the census, and are fatal under
// TLIB_REC_STRICT.
//-----------------------------------------------------------------------------

bool checkRecImmutability()
{
    bool ok = true;

    const int before = recRedefinitionCount();

    // a) ref(id) creates the node with a virgin definition group
    Tree id = tree(unique("IMM"));
    Tree r  = ref(id);
    {
        Tree v = nullptr, b = nullptr;
        CHECK(isRec(r, v, b) && b == nullptr);  // virgin : no RECDEF yet
    }

    // b) first definition, then the SAME body again : idempotent, census untouched
    Tree body = list1(tree(symbol("f"), ref(id)));
    CHECK(rec(id, body) == r);  // same node (hash-consed by the name)
    CHECK(rec(id, body) == r);
    CHECK(recRedefinitionCount() == before);

    // c) a DIFFERENT body is a redefinition : counted
    Tree body2 = list1(tree(symbol("g"), ref(id)));
    rec(id, body2);
    CHECK(recRedefinitionCount() == before + 1);

    // c') erasing a definition group is always a redefinition : counted
    rec(id, nil());
    CHECK(recRedefinitionCount() == before + 2);

    // under TLIB_REC_STRICT, a redefinition is fatal
    setenv("TLIB_REC_STRICT", "1", 1);
    bool caught = false;
    try {
        rec(id, body);  // id currently holds nil : this is a redefinition
    } catch (std::runtime_error& e) {
        caught = true;
        CHECK(std::string(e.what()).find("immutable") != std::string::npos);
    }
    unsetenv("TLIB_REC_STRICT");
    CHECK(caught);

    // alphaEquiv : the direct pair-memoized alpha-equivalence
    {
        Tree ia = tree(unique("AEA")), ib = tree(unique("AEB")), ic = tree(unique("AEC"));
        Tree ra = rec(ia, list1(tree(symbol("f"), ref(ia))));
        Tree rb = rec(ib, list1(tree(symbol("f"), ref(ib))));
        Tree rc = rec(ic, list1(tree(symbol("g"), ref(ic))));
        CHECK(alphaEquiv(ra, rb));            // same shape, different names
        CHECK(!alphaEquiv(ra, rc));           // different bodies
        CHECK(alphaEquiv(ra, ra));            // reflexive, cyclic reference terminates
        CHECK(alphaEquiv(proj(0, ra), proj(0, rb)));
        // agreement with the de Bruijn theorem form
        CHECK(areEquiv(ra, rb) == alphaEquiv(ra, rb));
        CHECK(areEquiv(ra, rc) == alphaEquiv(ra, rc));
        // the bijection is injective : one variable cannot match two partners.
        // h(x, x) vs h(y, z) with x = f(x), y = f(y), z = f(z) : shapes agree
        // pairwise, but x would need to bind BOTH y and z.
        Tree iy = tree(unique("AEY")), iz = tree(unique("AEZ"));
        Tree ry = rec(iy, list1(tree(symbol("f"), ref(iy))));
        Tree rz = rec(iz, list1(tree(symbol("f"), ref(iz))));
        CHECK(!alphaEquiv(tree(symbol("h"), ra, ra), tree(symbol("h"), ry, rz)));
        CHECK(alphaEquiv(tree(symbol("h"), ra, rb), tree(symbol("h"), ry, rz)));
    }

    return ok;
}

bool checkRewrite()
{
    bool ok = true;

    auto id     = [](Tree t) { return t; };
    auto negate = [](Tree t) {
        int i;
        return isInt(t->node(), &i) ? tree(-i) : t;
    };

    // identity on an ordinary tree : pointer equality for both functions
    Tree a  = tree(symbol("a"));
    Tree b  = tree(symbol("b"));
    Tree t1 = tree(symbol("foo"), tree(symbol("g"), a, tree(1)), b);
    CHECK(treeRewrite(t1, id) == t1);
    CHECK(treeRewriteInPlace(t1, id) == t1);

    // leaf change : only the ancestors of the changed leaf are rebuilt
    Tree left = tree(symbol("g"), a, tree(1));
    Tree t2   = tree(symbol("foo"), left, b);
    Tree r2   = treeRewrite(t2, negate);
    CHECK(r2 != t2);
    CHECK(r2->branch(0) != left);          // rebuilt : contained the 1
    CHECK(r2->branch(0)->branch(0) == a);  // untouched leaf kept
    CHECK(r2->branch(1) == b);             // untouched subtree kept
    CHECK(tree2int(r2->branch(0)->branch(1)) == -1);

    // sharing : foo(s, s) transforms s once, result branches stay shared
    Tree s  = tree(symbol("h"), tree(2));
    Tree t3 = tree(symbol("foo"), s, s);
    Tree r3 = treeRewrite(t3, negate);
    CHECK(r3->branch(0) == r3->branch(1));
    CHECK(tree2int(r3->branch(0)->branch(0)) == -2);

    // hash-consing : double negation restores the initial pointer
    CHECK(treeRewrite(treeRewrite(t2, negate), negate) == t2);

    // identity on a recursive tree : treeRewriteInPlace is pointer-stable,
    // treeRewrite mints a fresh variable and is only alpha-equivalent
    Tree x  = tree(unique("X"));
    Tree rx = rec(x, tree(symbol("f"), tree(3), ref(x)));
    CHECK(treeRewriteInPlace(rx, id) == rx);
    Tree fx = treeRewrite(rx, id);
    CHECK(fx != rx);
    CHECK(areEquiv(fx, rx));

    // separate calls do not share a memo : each call mints its own variable
    CHECK(treeRewrite(rx, id) != treeRewrite(rx, id));

    // treeRewrite on a recursive tree : old RECDEF untouched, new body
    // transformed, self-reference remapped to the new definition
    Tree var0 = nullptr, body0 = nullptr;
    CHECK(isRec(rx, var0, body0));
    Tree rr   = treeRewrite(rx, negate);
    Tree var1 = nullptr, body1 = nullptr;
    CHECK(isRec(rx, var1, body1) && body1 == body0);  // old def intact
    Tree var2 = nullptr, body2 = nullptr;
    CHECK(isRec(rr, var2, body2));
    CHECK(var2 != var0);
    CHECK(tree2int(body2->branch(0)) == -3);
    CHECK(body2->branch(1) == rr);   // self-reference follows the new var
    CHECK(body0->branch(1) == rx);   // old self-reference intact

    // treeRewriteInPlace on a recursive tree : same pointer no matter what, so
    // the verification must look at the body content (see the spec warning)
    Tree y  = tree(unique("Y"));
    Tree ry = rec(y, tree(symbol("f"), tree(4), ref(y)));
    Tree rp = treeRewriteInPlace(ry, negate);
    CHECK(rp == ry);
    Tree var3 = nullptr, body3 = nullptr;
    CHECK(isRec(ry, var3, body3));
    CHECK(tree2int(body3->branch(0)) == -4);  // body replaced in place
    CHECK(body3->branch(1) == ry);            // self-reference preserved

    return ok;
}

//-----------------------------------------------------------------------------
// Annotation-guarded rewriting (pre/post variants, see REWRITE-SPEC.md)
//-----------------------------------------------------------------------------

bool checkGuardedRewrite()
{
    bool ok = true;

    auto negatePost = [](Tree r) {
        int i;
        return isInt(r->node(), &i) ? tree(-i) : r;
    };

    // The "judgment" : a property set on ORIGINAL nodes by an external
    // analysis, consulted by the guard. Nodes carrying it are replaced
    // wholesale, their children never visited.
    Tree judgment = tree(symbol("GUARD-JUDGMENT"));
    Tree g        = tree(symbol("g"), tree(1), tree(2));
    Tree h        = tree(symbol("h"), tree(3));
    Tree f        = tree(symbol("f"), g, h);
    g->setProperty(judgment, tree(42));

    std::vector<Tree> preSeen;
    auto              guard = [&](Tree t) -> std::optional<Tree> {
        preSeen.push_back(t);
        if (Tree v = t->getProperty(judgment)) {
            return v;
        }
        return std::nullopt;
    };

    Tree r = treeRewrite(f, guard, negatePost);
    // g is cut to 42, its children 1 and 2 are never visited, and post is not
    // applied to a guarded replacement; h is descended into, its leaf 3 negated.
    CHECK(r == tree(symbol("f"), tree(42), tree(symbol("h"), tree(-3))));
    bool visited1 = false, visited2 = false;
    for (Tree t : preSeen) {
        visited1 = visited1 || (t == tree(1));
        visited2 = visited2 || (t == tree(2));
    }
    CHECK(!visited1 && !visited2);   // R1 pruned the subtree under g
    CHECK(preSeen.size() == 4);      // f, g, h, 3 : once per visited node
    g->clearProperty(judgment);

    // pre returning t itself = opaque subtree : kept verbatim even though
    // post would have rewritten its leaves
    auto opaqueH = [](Tree t) -> std::optional<Tree> {
        Tree x1;
        if (isTree(t, symbol("h"), x1)) {
            return t;
        }
        return std::nullopt;
    };
    Tree r2 = treeRewrite(f, opaqueH, negatePost);
    CHECK(r2 == tree(symbol("f"),
                     tree(symbol("g"), tree(-1), tree(-2)),  // descended
                     h));                                    // opaque, kept as-is
    CHECK(r2->branch(1) == h);  // same pointer : not even reconstructed

    auto nullGuard = [](Tree) -> std::optional<Tree> { return std::nullopt; };

    // post is only a bottom-up rule on rebuilt nodes. It is not applied to a
    // guarded replacement, which makes guarded cuts semantically opaque.
    Tree r3 = treeRewrite(f, guard, negatePost);
    CHECK(r3->branch(0) == tree(symbol("g"), tree(-1), tree(-2)));
    g->setProperty(judgment, tree(42));
    Tree r4 = treeRewrite(f, guard, negatePost);
    CHECK(r4->branch(0) == tree(42));
    g->clearProperty(judgment);

    // recursive trees : the guard is never consulted on SYMREC nodes, and
    // the in-place variant stays pointer-stable under the identity pair
    Tree z  = tree(unique("Z"));
    Tree rz = rec(z, tree(symbol("f"), tree(5), ref(z)));
    preSeen.clear();
    Tree rzr = treeRewriteInPlace(rz, guard, negatePost);
    CHECK(rzr == rz);  // pointer-stable (same variable reused)
    for (Tree t : preSeen) {
        CHECK(t != rz);  // never called on the SYMREC node
    }
    Tree varz = nullptr, bodyz = nullptr;
    CHECK(isRec(rz, varz, bodyz));
    CHECK(tree2int(bodyz->branch(0)) == -5);  // body rewritten through the rec

    // equivalence with the single-rule form
    Tree t4 = tree(symbol("foo"), tree(symbol("g"), tree(8), tree(9)));
    auto negate1 = [](Tree t) {
        int i;
        return isInt(t->node(), &i) ? tree(-i) : t;
    };
    CHECK(treeRewrite(t4, negate1) == treeRewrite(t4, nullGuard, negatePost));

    return ok;
}

//-----------------------------------------------------------------------------
// DNF/CNF conditions
//-----------------------------------------------------------------------------

bool checkDnfCnf()
{
    bool ok = true;

    Tree a = tree(symbol("condA"));
    Tree b = tree(symbol("condB"));

    Tree da = dnfCond(a);
    Tree db = dnfCond(b);

    // a AND a = a ; a OR a = a
    CHECK(dnfAnd(da, da) == da);
    CHECK(dnfOr(da, da) == da);

    // a is less specific than a AND b
    Tree dab = dnfAnd(da, db);
    CHECK(dnfLess(da, dab));
    CHECK(dnfAnd(da, db) == dnfAnd(db, da));  // commutativity

    Tree ca = cnfCond(a);
    Tree cb = cnfCond(b);
    CHECK(cnfAnd(ca, ca) == ca);
    CHECK(cnfOr(ca, ca) == ca);
    CHECK(cnfLess(ca, ca));
    CHECK(cnfAnd(ca, cb) == cnfAnd(cb, ca));  // commutativity
    CHECK(cnfOr(ca, cb) == cnfOr(cb, ca));

    return ok;
}

//-----------------------------------------------------------------------------
// Occurrences counting
//-----------------------------------------------------------------------------

bool checkOccurrences()
{
    bool ok = true;

    Tree x    = tree(symbol("shared"));
    Tree root = tree(symbol("op"), tree(symbol("f"), x), tree(symbol("g"), x, x));

    Occur occ(root);
    CHECK(occ.getCount(x) == 3);
    CHECK(occ.getCount(root) == 0);  // by convention the root doesn't count itself
    CHECK(occ.getCount(tree(symbol("unrelated#"))) == 0);

    return ok;
}

//-----------------------------------------------------------------------------
// Error handler hook
//-----------------------------------------------------------------------------

static std::string gLastError;
[[noreturn]] static void recordingHandler(const std::string& msg)
{
    gLastError = msg;
    throw std::runtime_error(msg);
}

bool checkErrorHandler()
{
    bool ok = true;

    // default handler : std::runtime_error
    bool caught = false;
    try {
        tree2int(tree(symbol("not_a_number")));
    } catch (std::runtime_error& e) {
        caught = true;
        CHECK(std::string(e.what()).find("integer") != std::string::npos);
    }
    CHECK(caught);

    // custom handler : sees the message, previous handler is returned
    tlib::ErrorHandler previous = tlib::setErrorHandler(recordingHandler);
    caught                      = false;
    try {
        tree2ptr(tree(1));
    } catch (std::runtime_error&) {
        caught = true;
    }
    CHECK(caught);
    CHECK(gLastError.find("pointer") != std::string::npos);
    tlib::setErrorHandler(previous);

    return ok;
}

//-----------------------------------------------------------------------------
// FixPointIterator : exercise the generic engine end to end on a toy EXACT domain,
// with no signals and no FaustAlgebra. The domain computes the set of ordinary
// (non-proj) nodes reachable from a variable -- a finite lattice, so the fixpoint
// converges by equality. This tests RecPlan consumption, the proj bridge, the Jacobi
// solve, and mutual-recursion components.
//-----------------------------------------------------------------------------

namespace {

using NodeSet = std::set<Tree>;

// combine(node, kids) = {node} ∪ ⋃ kids. proj/rec are handled by the iterator, so
// combine only ever sees ordinary nodes ; adding {node} captures each head it meets.
struct ReachDomain : FixPointDomain<NodeSet> {
    NodeSet bottom(Tree) const override { return {}; }
    NodeSet top(Tree) const override { return {}; }  // unused : exact domain, never hits the guard
    bool    lessEqual(const NodeSet& x, const NodeSet& y) const override
    {
        for (Tree t : x) {
            if (y.find(t) == y.end()) return false;
        }
        return true;
    }
    mutable int fCombineCalls = 0;  // to check the memo prevents exponential recompute
    NodeSet     combine(Tree node, const std::vector<NodeSet>& kids,
                        FixPointEvaluator<NodeSet>& /*ev*/) const override
    {
        ++fCombineCalls;
        NodeSet r;
        r.insert(node);
        for (const NodeSet& k : kids) r.insert(k.begin(), k.end());
        return r;
    }
};

}  // namespace

bool checkFixPoint()
{
    bool       ok = true;
    ReachDomain dom;

    // --- Self recursion, signals form : X = rec(a, [ g(proj0(X)) ]) ---
    // Its reachable set is the single head g, reached once (the recursion adds nothing new).
    Tree a    = tree(unique("A"));
    Tree gArg = tree(symbol("g"), proj(0, ref(a)));  // proj0(ref(a)) == proj0(X) by hash-consing
    Tree X    = rec(a, list1(gArg));
    Tree rootX = proj(0, X);

    RecPlan                    planX(rootX);
    FixPointIterator<NodeSet>  itX(planX, dom);
    NodeSet                    vx = itX.value(rootX);
    CHECK(vx == NodeSet{gArg});
    CHECK(itX.variableValue(X)[0] == NodeSet{gArg});

    // --- Mutual recursion, signals form : X = rec(a, [ g(proj0(Y)) ]),
    //     Y = rec(b, [ h(proj0(X)) ]). Both reach {g, h} : the Jacobi solve of the
    //     two-member component must propagate each into the other. ---
    Tree b  = tree(unique("B"));
    Tree gX = tree(symbol("g"), proj(0, ref(b)));  // depends on Y
    Tree hY = tree(symbol("h"), proj(0, ref(a)));  // depends on X
    Tree X2 = rec(a, list1(gX));
    Tree Y2 = rec(b, list1(hY));
    Tree root2 = tree(symbol("top"), proj(0, X2), proj(0, Y2));

    RecPlan planM(root2);
    CHECK(planM.sccOf(X2) == planM.sccOf(Y2));  // one mutual component
    FixPointIterator<NodeSet> itM(planM, dom);
    NodeSet                   both{gX, hY};
    CHECK(itM.variableValue(X2)[0] == both);
    CHECK(itM.variableValue(Y2)[0] == both);

    // --- A group of two branches in one variable : X = rec(a, [ g(proj0(X)), h(proj1(X)) ]).
    //     Branch 0 reaches {g}, branch 1 reaches {h} -- distinct rows, no cross-talk. ---
    Tree c   = tree(unique("C"));
    Tree e0  = tree(symbol("g"), proj(0, ref(c)));
    Tree e1  = tree(symbol("h"), proj(1, ref(c)));
    Tree Xg  = rec(c, list2(e0, e1));

    RecPlan planG(tree(symbol("top"), proj(0, Xg), proj(1, Xg)));
    FixPointIterator<NodeSet> itG(planG, dom);
    CHECK(itG.variableValue(Xg)[0] == NodeSet{e0});
    CHECK(itG.variableValue(Xg)[1] == NodeSet{e1});

    // --- Memo : a heavily-shared rec-free DAG must be combined ONCE per distinct node,
    //     not exponentially. 30 levels, each referencing the level below twice : 2^30
    //     evaluations if recomputed, 31 distinct hash-consed nodes if memoized. This is
    //     the isRecFree / invariant fast path (the kContainsRec bit) at work. ---
    ReachDomain dagDom;
    Tree        dag = tree(symbol("leaf"));
    for (int i = 0; i < 30; ++i) {
        dag = tree(symbol("f"), dag, dag);  // hash-consed : one node per level
    }
    RecPlan                   planD(dag);   // no recursion : the plan is empty
    FixPointIterator<NodeSet> itD(planD, dagDom);
    (void)itD.value(dag);
    CHECK(dagDom.fCombineCalls == 31);  // 30 f-levels + 1 leaf, each combined exactly once

    return ok;
}

//-----------------------------------------------------------------------------
// FixPointIterator on a toy APPROXIMATE domain : intervals. Exercises the parts the
// exact domain doesn't -- widening, narrowing, and the descending probe -- still in
// tlib, with no signals. combine interprets a tiny language (int constant, add, sub,
// mod, delay) over saturating intervals. This is the shape of the real interval domain.
//-----------------------------------------------------------------------------

namespace {

constexpr long IBIG = 1L << 30;  // stands for infinity ; arithmetic saturates here

struct Itv {
    long lo    = 0;
    long hi    = 0;
    bool empty = true;
    bool operator==(const Itv& o) const
    {
        return empty == o.empty && (empty || (lo == o.lo && hi == o.hi));
    }
};

static long sat(long v)
{
    return v < -IBIG ? -IBIG : (v > IBIG ? IBIG : v);
}
static Itv finite(long lo, long hi) { return Itv{sat(lo), sat(hi), false}; }
static Itv empty() { return Itv{}; }

static Itv iadd(const Itv& a, const Itv& b)
{
    if (a.empty || b.empty) return empty();
    return finite(a.lo + b.lo, a.hi + b.hi);
}
static Itv isub(const Itv& a, const Itv& b)
{
    if (a.empty || b.empty) return empty();
    return finite(a.lo - b.hi, a.hi - b.lo);
}
static Itv ijoin(const Itv& a, const Itv& b)
{
    if (a.empty) return b;
    if (b.empty) return a;
    return finite(std::min(a.lo, b.lo), std::max(a.hi, b.hi));
}
// a mod M for a POSITIVE constant modulus [M,M]. Sound interval semantics of C's %.
static Itv imod(const Itv& a, const Itv& m)
{
    if (a.empty) return empty();
    if (m.empty || m.lo != m.hi || m.lo <= 0) return finite(-IBIG, IBIG);
    long M = m.lo;
    if (a.lo >= 0) {
        if (a.hi >= IBIG || (a.hi - a.lo) >= M) return finite(0, M - 1);
        long rl = a.lo % M, rh = a.hi % M;
        return (rl <= rh) ? finite(rl, rh) : finite(0, M - 1);
    }
    if (a.hi <= 0) {
        if (a.lo <= -IBIG || (a.hi - a.lo) >= M) return finite(-(M - 1), 0);
        long rl = a.lo % M, rh = a.hi % M;  // C : sign of dividend
        return (rl <= rh) ? finite(rl, rh) : finite(-(M - 1), 0);
    }
    return finite(-(M - 1), M - 1);  // spans 0
}

struct IntervalDomain : FixPointDomain<Itv> {
    struct Probe {
        bool certified = false;
        Itv  value;
    };
    mutable std::unordered_map<Tree, Probe> fProbe;  // filled by recordProbe, read by widen

    Itv  bottom(Tree) const override { return empty(); }
    Itv  top(Tree) const override { return finite(-IBIG, IBIG); }
    bool lessEqual(const Itv& x, const Itv& y) const override
    {
        if (x.empty) return true;
        if (y.empty) return false;
        return y.lo <= x.lo && x.hi <= y.hi;
    }

    Itv combine(Tree node, const std::vector<Itv>& kids,
                FixPointEvaluator<Itv>& /*ev*/) const override
    {
        int c;
        if (isInt(node->node(), &c)) return finite(c, c);
        if (node->node() == Node(symbol("add"))) return iadd(kids[0], kids[1]);
        if (node->node() == Node(symbol("sub"))) return isub(kids[0], kids[1]);
        if (node->node() == Node(symbol("mod"))) return imod(kids[0], kids[1]);
        if (node->node() == Node(symbol("delay"))) return ijoin(kids[0], finite(0, 0));  // ⊔ {0}
        return finite(-IBIG, IBIG);  // unknown leaf/op
    }

    int widenAfter() const override { return 3; }
    int maxIterations() const override { return 100000; }
    int maxNarrowingIterations() const override { return 4; }

    std::optional<Itv> probeSeed(Tree) const override { return finite(0, IBIG); }
    void recordProbe(Tree var, const Itv& probed, bool certified) const override
    {
        fProbe[var] = Probe{certified, probed};
    }

    Itv widen(Tree var, const Itv& old, const Itv& fresh) const override
    {
        if (old.empty) return fresh;  // widen(⊥, x) = x
        auto       it        = fProbe.find(var);
        const bool certified = (it != fProbe.end() && it->second.certified);
        const long thresh    = (it != fProbe.end() ? it->second.value.hi : IBIG);
        // Lower : if it drops, jump to 0 when the SCC is certified ≥0, else to -∞.
        long lo = (fresh.lo < old.lo) ? (certified ? 0 : -IBIG) : old.lo;
        // Upper : if it grows, jump to the probe threshold when certified, else to +∞.
        long hi = (fresh.hi > old.hi) ? (certified ? thresh : IBIG) : old.hi;
        return finite(lo, hi);
    }
};

}  // namespace

bool checkFixPointInterval()
{
    bool           ok = true;
    IntervalDomain dom;

    // --- Cyclic counter : x = (1 + x@1) mod 2000, attribute [0, 1999]. ---
    // Naive would need ~2000 narrowing rounds ; the probe certifies ≥0 and hands a
    // threshold of 1999, so the real pass lands on [0,1999] in a handful of rounds.
    Tree idx  = tree(unique("X"));
    Tree xAt1 = tree(symbol("delay"), proj(0, ref(idx)));
    Tree body = tree(symbol("mod"), tree(symbol("add"), tree(1), xAt1), tree(2000));
    Tree X    = rec(idx, list1(body));
    Tree root = proj(0, X);

    RecPlan                planX(root);
    FixPointIterator<Itv>  itX(planX, dom);
    Itv                    vx = itX.value(root);
    CHECK((vx == finite(0, 1999)));
    CHECK(dom.fProbe.at(proj(0, X)).certified);  // the counter was certified ≥ 0

    // --- Genuinely negative oscillator : y = -1 - y@1, values {-1,0}. ---
    // The probe must FAIL (the seed [0,+BIG] is not a post-fixpoint), and the real pass
    // from ∅ must still find the exact [-1,0] without widening.
    IntervalDomain dom2;
    Tree idy  = tree(unique("Y"));
    Tree yAt1 = tree(symbol("delay"), proj(0, ref(idy)));
    Tree bodyY = tree(symbol("sub"), tree(-1), yAt1);
    Tree Y     = rec(idy, list1(bodyY));
    Tree rootY = proj(0, Y);

    RecPlan               planY(rootY);
    FixPointIterator<Itv> itY(planY, dom2);
    Itv                   vy = itY.value(rootY);
    CHECK((vy == finite(-1, 0)));
    CHECK(!dom2.fProbe.at(proj(0, Y)).certified);  // the oscillator was NOT certified

    return ok;
}

//-----------------------------------------------------------------------------
// Lifecycle : cleanup() ends a session, the library is reusable right after
//-----------------------------------------------------------------------------

bool checkLifecycle()
{
    bool ok = true;

    Tree before = tree(symbol("survivor"), tree(1), tree(2));
    (void)before;  // any use of 'before' after cleanup() would be invalid

    tlib::cleanup();

    // a fresh session works, including the library-owned symbols (nil, rec...)
    CHECK(isNil(nil()));
    Tree after = tree(symbol("survivor"), tree(1), tree(2));
    CHECK(after->arity() == 2);
    CHECK(isClosed(rec(tree(symbol("f"), ref(1)))));
    CHECK(len(list2(tree(1), tree(2))) == 2);

    // Signature ranges are session state as well: cleanup discards the old
    // registry and the first new signature starts again at global opcode zero.
    auto freshSignature = signature("LifecycleSignature");
    Sym  freshConstructor = freshSignature.add("LifecycleConstructor");
    SymbolTag freshTag;
    CHECK(getSymbolTag(freshConstructor, freshTag));
    CHECK(freshTag.signature == freshSignature.identity() && freshTag.opcode == 0);

    return ok;
}

//-----------------------------------------------------------------------------
// Hash table growth : identity must survive many insertions (rehashing)
//-----------------------------------------------------------------------------

bool checkHashTableGrowth()
{
    bool ok = true;

    const int  n     = 100000;  // well past the initial table sizes
    static Sym leafS = symbol("leaf");

    // record a sample of pointers, then verify identity after the table grew
    Tree first = tree(leafS, tree(0));
    for (int i = 0; i < n; i++) {
        (void)tree(leafS, tree(i));
    }
    CHECK(first == tree(leafS, tree(0)));            // pointer identity preserved
    CHECK(tree(leafS, tree(n / 2)) == tree(leafS, tree(n / 2)));
    CHECK(tree(leafS, tree(n - 1)) != tree(leafS, tree(n - 2)));

    return ok;
}
