/*
 * TLIB : tree library
 * Copyright (C) 2003-2026 GRAME, Centre National de Creation Musicale
 * SPDX-License-Identifier: LGPL-2.1-or-later
 *
 * Performance scenarios inspired by the Faust compiler use of tlib:
 *   - build large hash-consed expression DAGs with low/high sharing,
 *   - traverse trees by logical occurrences and by unique shared nodes,
 *   - annotate subtrees with occurrence counts,
 *   - stress property maps and binary memoization property2<Tree>,
 *   - build and convert recursive de Bruijn trees.
 *
 * Usage:
 *   ./build/tlib-benchmark [scale] [runs]
 *   ./build/tlib-benchmark --scale N --runs N
 *
 * scale is a positive integer. The default scale=1 keeps the suite quick
 * enough for interactive development; larger values increase loop counts.
 * runs controls how many times each scenario is measured; the reported time
 * is the median.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(__APPLE__) || defined(__linux__)
#include <sys/utsname.h>
#endif

#include "occur.hh"
#include "tlib.hh"

using Clock = std::chrono::steady_clock;

static volatile std::uintptr_t gPtrSink   = 0;
static int                     gBadNotes  = 0;  // scenarios whose validation note failed
static volatile std::size_t    gCountSink = 0;

struct BenchResult {
    double      fElapsedMs;
    std::string fNote;
};

static double ms(Clock::time_point t0, Clock::time_point t1)
{ return std::chrono::duration<double, std::milli>(t1 - t0).count(); }

static std::size_t fullBinaryNodes(int depth)
{ return (std::size_t(1) << (depth + 1)) - 1; }

static std::size_t fullTernaryNodes(int depth)
{
    std::size_t nodes = 0;
    std::size_t level = 1;
    for (int i = 0; i <= depth; ++i) {
        nodes += level;
        level *= 3;
    }
    return nodes;
}

static int scaled(int base, int scale)
{ return base * scale; }

static int parsePositive(const char* text, int fallback)
{
    int value = std::atoi(text);
    return (value > 0) ? value : fallback;
}

static double median(std::vector<double>& samples)
{
    std::sort(samples.begin(), samples.end());
    std::size_t n = samples.size();
    if ((n % 2) == 0) {
        return (samples[n / 2 - 1] + samples[n / 2]) / 2.0;
    } else {
        return samples[n / 2];
    }
}

static void report(const std::string& name, std::size_t work, double elapsedMs,
                   const std::string& note = "")
{
    const double rate = (elapsedMs > 0.0) ? (double(work) / elapsedMs / 1000.0) : 0.0;
    std::cout << std::left << std::setw(30) << name << std::right << std::setw(14) << work
              << std::setw(12) << std::fixed << std::setprecision(3) << elapsedMs << std::setw(12)
              << std::setprecision(3) << rate;
    if (!note.empty()) {
        std::cout << "  " << note;
    }
    std::cout << "\n";
}

template <typename Measure>
static void reportMedian(const std::string& name, std::size_t work, int runs, Measure measure)
{
    std::vector<double> samples;
    samples.reserve(runs);
    std::string note;
    bool        stableNote = true;

    for (int i = 0; i < runs; ++i) {
        BenchResult r = measure();
        samples.push_back(r.fElapsedMs);
        if (i == 0) {
            note = r.fNote;
        } else if (note != r.fNote) {
            stableNote = false;
        }
    }

    if (!stableNote) {
        note = "unstable-note";
    }
    if (note.find("BAD") != std::string::npos) {
        ++gBadNotes;
    }
    report(name, work, median(samples), note);
}

static Tree makeLowSharingTree(int depth, int seed)
{
    if (depth == 0) {
        return tree(seed);
    }
    return tree(symbol("bin"), makeLowSharingTree(depth - 1, seed * 2 + 1),
                makeLowSharingTree(depth - 1, seed * 2 + 2));
}

static Tree makeHighSharingTree(int depth, int seed, int states)
{
    seed %= states;
    if (depth == 0) {
        return tree(seed % 64);
    }
    return tree(symbol("bin"), makeHighSharingTree(depth - 1, seed * 3 + 1, states),
                makeHighSharingTree(depth - 1, seed * 5 + 7, states));
}

static Tree makeRepeatedTree(int depth, Tree leaf)
{
    if (depth == 0) {
        return leaf;
    }
    Tree x = makeRepeatedTree(depth - 1, leaf);
    return tree(symbol("repeat"), x, x);
}

static Tree makeRecursiveBody(int depth)
{
    Tree body = ref(1);
    for (int i = 0; i < depth; ++i) {
        body = tree(symbol("recop"), body, ref(1), tree(i & 63));
    }
    return body;
}

static Tree makeSymbolicRecBody(int depth, Tree var, int seed, int states)
{
    seed %= states;
    if (depth == 0) {
        return (seed % 4 == 0) ? ref(var) : tree((seed % 257) - 128);
    }
    Tree a = makeSymbolicRecBody(depth - 1, var, seed * 3 + 1, states);
    Tree b = makeSymbolicRecBody(depth - 1, var, seed * 5 + 7, states);
    Tree c = ((seed + depth) % 3 == 0) ? ref(var) : tree((seed + depth) & 255);
    return tree(symbol("symrecop"), a, b, c);
}

static Tree makeSymbolicRecursiveTree(int depth, int seed, int states, Tree& var, Tree& body)
{
    var  = tree(unique("R"));
    body = makeSymbolicRecBody(depth, var, seed, states);
    return rec(var, body);
}

static std::size_t countLogical(Tree t)
{
    std::size_t n = 1;
    for (int i = 0; i < t->arity(); ++i) {
        n += countLogical(t->branch(i));
    }
    return n;
}

static std::size_t countUnique(Tree t)
{
    if (t->isAlreadyVisited()) {
        return 0;
    }
    t->setVisited();
    std::size_t n = 1;
    for (int i = 0; i < t->arity(); ++i) {
        n += countUnique(t->branch(i));
    }
    return n;
}

static std::size_t distinctNodes(Tree t)
{
    std::set<Tree>    seen;
    std::vector<Tree> stack;
    stack.push_back(t);
    while (!stack.empty()) {
        Tree x = stack.back();
        stack.pop_back();
        if (!seen.insert(x).second) {
            continue;
        }
        for (int i = 0; i < x->arity(); ++i) {
            stack.push_back(x->branch(i));
        }
    }
    return seen.size();
}

static void annotateSharing(Tree key, Tree t)
{
    Tree countTree = t->getProperty(key);
    int  count     = countTree ? countTree->node().getInt() : 0;
    if (count == 0) {
        for (int i = 0; i < t->arity(); ++i) {
            annotateSharing(key, t->branch(i));
        }
    }
    t->setProperty(key, tree(count + 1));
}

static bool isSymbolicRecDef(Tree t, Tree& var, Tree& body)
{
    return isRec(t, var, body);
}

static Tree negateNodeNumbers(const Node& n)
{
    switch (n.type()) {
        case kIntNode:
            return tree(-n.getInt());
        case kInt64Node:
            return tree(Node(-n.getInt64()));
        case kDoubleNode:
            return tree(-n.getDouble());
        default:
            return nullptr;
    }
}

// Bottom-up rules for treeRewrite (see tlib/rewrite.hh).
// The rule receives a node whose branches are already transformed.
static Tree negateRule(Tree t)
{
    Tree numeric = negateNodeNumbers(t->node());
    if (!numeric) {
        return t;
    }
    return (t->arity() == 0) ? numeric : tree(numeric->node(), t->branches());
}

static Tree identityRule(Tree t)
{
    return t;
}

static void benchConstruction(int scale, int runs)
{
    std::cout << "\n[construction]\n";
    const int lowDepth  = 18 + (scale > 1 ? 1 : 0);
    const int highDepth = 20 + (scale > 2 ? 1 : 0);

    reportMedian("build-low-sharing", fullBinaryNodes(lowDepth), runs, [=]() {
        tlib::cleanup();
        auto t0          = Clock::now();
        Tree low         = makeLowSharingTree(lowDepth, 1);
        auto t1          = Clock::now();
        gPtrSink         = reinterpret_cast<std::uintptr_t>(low);
        std::string note = "unique=" + std::to_string(distinctNodes(low));
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("build-high-sharing", fullBinaryNodes(highDepth), runs, [=]() {
        tlib::cleanup();
        auto t0          = Clock::now();
        Tree high        = makeHighSharingTree(highDepth, 1, 128);
        auto t1          = Clock::now();
        gPtrSink         = reinterpret_cast<std::uintptr_t>(high);
        std::string note = "unique=" + std::to_string(distinctNodes(high));
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("rebuild-high-sharing", fullBinaryNodes(highDepth), runs, [=]() {
        tlib::cleanup();
        Tree high        = makeHighSharingTree(highDepth, 1, 128);
        auto t0          = Clock::now();
        Tree high2       = makeHighSharingTree(highDepth, 1, 128);
        auto t1          = Clock::now();
        gPtrSink         = reinterpret_cast<std::uintptr_t>(high2);
        std::string note = (high == high2) ? "cache-hit shared" : "NOT-SHARED";
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });
}

static void benchTraversal(int scale, int runs)
{
    std::cout << "\n[traversal]\n";
    const int depth = 21 + (scale > 2 ? 1 : 0);

    reportMedian("walk-logical-occurrences", fullBinaryNodes(depth), runs, [=]() {
        tlib::cleanup();
        Tree        root    = makeHighSharingTree(depth, 1, 96);
        auto        t0      = Clock::now();
        std::size_t logical = countLogical(root);
        auto        t1      = Clock::now();
        gCountSink          = logical;
        std::string note    = "visited=" + std::to_string(logical);
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    tlib::cleanup();
    Tree        root = makeHighSharingTree(depth, 1, 96);
    std::size_t uniq = distinctNodes(root);
    tlib::cleanup();
    reportMedian("walk-unique-shared-nodes", uniq, runs, [=]() {
        tlib::cleanup();
        Tree root = makeHighSharingTree(depth, 1, 96);
        CTree::startNewVisit();
        auto        t0     = Clock::now();
        std::size_t unique = countUnique(root);
        auto        t1     = Clock::now();
        gCountSink         = unique;
        std::string note   = "visited=" + std::to_string(unique);
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });
}

static void benchOccurrences(int scale, int runs)
{
    std::cout << "\n[occurrences]\n";
    const int depth = 20 + (scale > 2 ? 1 : 0);

    reportMedian("Occur-all-visits", fullBinaryNodes(depth), runs, [=]() {
        tlib::cleanup();
        Tree        leaf = tree(symbol("x"));
        Tree        root = makeRepeatedTree(depth, leaf);
        auto        t0   = Clock::now();
        Occur       occ(root);
        auto        t1   = Clock::now();
        std::string note = "leaf-count=" + std::to_string(occ.getCount(leaf));
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("sharing-first-visit-annotate", fullBinaryNodes(18), runs, [=]() {
        tlib::cleanup();
        Tree sharingRoot = makeLowSharingTree(18, 1);
        Tree key         = tree(unique("sharing-count"));
        auto t0          = Clock::now();
        annotateSharing(key, sharingRoot);
        auto        t1   = Clock::now();
        Tree        c    = sharingRoot->getProperty(key);
        std::string note = "root-count=" + std::to_string(c ? c->node().getInt() : 0);
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });
}

static void benchProperties(int scale, int runs)
{
    std::cout << "\n[properties]\n";
    const int manyHosts = scaled(500000, scale);
    const int hotKeys   = scaled(60000, scale);
    const int envCount  = scaled(80000, scale);

    reportMedian("property-set-many-hosts", manyHosts, runs, [=]() {
        tlib::cleanup();
        Tree list = nil();
        for (int i = 0; i < manyHosts; ++i) {
            list = cons(tree(i), list);
        }
        Tree key = tree(symbol("many-hosts-key"));

        auto t0 = Clock::now();
        for (Tree l = list; isList(l); l = tl(l)) {
            hd(l)->setProperty(key, l);
        }
        auto t1 = Clock::now();
        tlib::cleanup();
        return BenchResult{ms(t0, t1), ""};
    });

    reportMedian("property-get-many-hosts", manyHosts, runs, [=]() {
        tlib::cleanup();
        Tree list = nil();
        for (int i = 0; i < manyHosts; ++i) {
            list = cons(tree(i), list);
        }
        Tree key = tree(symbol("many-hosts-key"));
        for (Tree l = list; isList(l); l = tl(l)) {
            hd(l)->setProperty(key, l);
        }

        auto        t0 = Clock::now();
        std::size_t n  = 0;
        for (Tree l = list; isList(l); l = tl(l)) {
            n += (hd(l)->getProperty(key) != nullptr);
        }
        auto t1          = Clock::now();
        gCountSink       = n;
        std::string note = "hits=" + std::to_string(n);
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("property-set-one-host", hotKeys, runs, [=]() {
        tlib::cleanup();
        Tree              host = tree(symbol("hot-property-host"));
        std::vector<Tree> keys;
        keys.reserve(hotKeys);
        for (int i = 0; i < hotKeys; ++i) {
            keys.push_back(tree(symbol("hot-key"), tree(i)));
        }

        auto t0 = Clock::now();
        for (int i = 0; i < hotKeys; ++i) {
            host->setProperty(keys[i], tree(i));
        }
        auto t1 = Clock::now();
        tlib::cleanup();
        return BenchResult{ms(t0, t1), ""};
    });

    reportMedian("property-get-one-host", hotKeys, runs, [=]() {
        tlib::cleanup();
        Tree              host = tree(symbol("hot-property-host"));
        std::vector<Tree> keys;
        keys.reserve(hotKeys);
        for (int i = 0; i < hotKeys; ++i) {
            keys.push_back(tree(symbol("hot-key"), tree(i)));
            host->setProperty(keys[i], tree(i));
        }

        auto        t0 = Clock::now();
        std::size_t n  = 0;
        for (int i = 0; i < hotKeys; ++i) {
            n += (host->getProperty(keys[i]) != nullptr);
        }
        auto t1          = Clock::now();
        gCountSink       = n;
        std::string note = "hits=" + std::to_string(n);
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("property2-set-one-box", envCount, runs, [=]() {
        tlib::cleanup();
        property2<Tree>   memo;
        Tree              box = tree(symbol("eval-box"), tree(0));
        std::vector<Tree> envs;
        envs.reserve(envCount);
        for (int i = 0; i < envCount; ++i) {
            envs.push_back(tree(symbol("env"), tree(i)));
        }

        auto t0 = Clock::now();
        for (int i = 0; i < envCount; ++i) {
            memo.set(box, envs[i], tree(i));
        }
        auto t1 = Clock::now();
        tlib::cleanup();
        return BenchResult{ms(t0, t1), ""};
    });

    reportMedian("property2-get-one-box", envCount, runs, [=]() {
        tlib::cleanup();
        property2<Tree>   memo;
        Tree              box = tree(symbol("eval-box"), tree(0));
        std::vector<Tree> envs;
        envs.reserve(envCount);
        for (int i = 0; i < envCount; ++i) {
            envs.push_back(tree(symbol("env"), tree(i)));
            memo.set(box, envs[i], tree(i));
        }

        auto        t0 = Clock::now();
        std::size_t n  = 0;
        for (int i = 0; i < envCount; ++i) {
            Tree value = nullptr;
            n += memo.get(box, envs[i], value) && value == tree(i);
        }
        auto t1          = Clock::now();
        gCountSink       = n;
        std::string note = "hits=" + std::to_string(n);
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });
}

static void benchRewrites(int scale, int runs)
{
    std::cout << "\n[rewrites]\n";
    const int sharedDepth = 20 + (scale > 2 ? 1 : 0);
    const int recDepth    = 10 + (scale > 2 ? 1 : 0);
    const std::size_t recWork = fullTernaryNodes(recDepth);

    reportMedian("rewrite-identity-shared", fullBinaryNodes(sharedDepth), runs, [=]() {
        tlib::cleanup();
        Tree root = makeHighSharingTree(sharedDepth, 1, 128);
        auto t0 = Clock::now();
        Tree rewritten = treeRewrite(root, identityRule);
        auto t1 = Clock::now();
        gPtrSink = reinterpret_cast<std::uintptr_t>(rewritten);
        std::string note = "identity=" + std::string((rewritten == root) ? "yes" : "NO");
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("rewrite-negate-shared", fullBinaryNodes(sharedDepth), runs, [=]() {
        tlib::cleanup();
        Tree root = makeHighSharingTree(sharedDepth, 1, 128);
        auto t0 = Clock::now();
        Tree rewritten = treeRewrite(root, negateRule);
        auto t1 = Clock::now();
        gPtrSink = reinterpret_cast<std::uintptr_t>(rewritten);
        std::string note = "changed=" + std::string((rewritten != root) ? "yes" : "no");
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("rewrite-negate-shared-rt", fullBinaryNodes(sharedDepth) * 2, runs, [=]() {
        tlib::cleanup();
        Tree root = makeHighSharingTree(sharedDepth, 1, 128);
        auto t0 = Clock::now();
        Tree rewritten = treeRewrite(root, negateRule);
        Tree restored  = treeRewrite(rewritten, negateRule);
        auto t1 = Clock::now();
        gPtrSink = reinterpret_cast<std::uintptr_t>(restored);
        // no rec in this DAG : hash-consing restores the exact pointer
        std::string note = "roundtrip=" + std::string((restored == root) ? "yes" : "NO");
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("rewrite-symbolic-rec-pure", recWork, runs, [=]() {
        tlib::cleanup();
        Tree var;
        Tree body;
        Tree root = makeSymbolicRecursiveTree(recDepth, 1, 96, var, body);
        auto t0 = Clock::now();
        Tree rewritten = treeRewrite(root, negateRule);
        auto t1 = Clock::now();
        // pure : fresh variable, and the old definition is left untouched
        Tree newVar;
        Tree newBody;
        Tree oldVar;
        Tree oldBody;
        bool fresh     = isSymbolicRecDef(rewritten, newVar, newBody) && rewritten != root &&
                         newVar != var && newBody != body;
        bool oldIntact = isSymbolicRecDef(root, oldVar, oldBody) && oldBody == body;
        gPtrSink = reinterpret_cast<std::uintptr_t>(rewritten);
        std::string note = "pure=" + std::string((fresh && oldIntact) ? "yes" : "NO");
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });
}

static void benchRecursiveTrees(int scale, int runs)
{
    std::cout << "\n[recursive-trees]\n";
    const int depth = scaled(1200, scale);
    const int alphaDepth = 10 + (scale > 2 ? 1 : 0);
    const std::size_t alphaWork = fullTernaryNodes(alphaDepth);

    reportMedian("build-debruijn-rec", depth, runs, [=]() {
        tlib::cleanup();
        auto t0          = Clock::now();
        Tree body        = makeRecursiveBody(depth);
        Tree r           = rec(body);
        auto t1          = Clock::now();
        gPtrSink         = reinterpret_cast<std::uintptr_t>(r);
        std::string note = std::string("closed=") + (isClosed(r) ? "yes" : "no");
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("debruijn-to-symbolic", depth, runs, [=]() {
        tlib::cleanup();
        Tree body = makeRecursiveBody(depth);
        Tree r    = rec(body);
        auto t0   = Clock::now();
        Tree sym  = deBruijn2Sym(r);
        auto t1   = Clock::now();
        gPtrSink  = reinterpret_cast<std::uintptr_t>(sym);
        tlib::cleanup();
        return BenchResult{ms(t0, t1), "memo-local"};
    });

    reportMedian("debruijn-to-symbolic-repeat", depth, runs, [=]() {
        tlib::cleanup();
        Tree body        = makeRecursiveBody(depth);
        Tree r           = rec(body);
        Tree sym         = deBruijn2Sym(r);
        auto t0          = Clock::now();
        Tree sym2        = deBruijn2Sym(r);
        auto t1          = Clock::now();
        gPtrSink         = reinterpret_cast<std::uintptr_t>(sym2);
        // deBruijn2Sym is canonical (content-derived names) : converting the
        // same tree twice yields the SAME pointer, no persistent cache needed
        std::string note = (sym == sym2) ? "canonical" : "BAD";
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("debruijn-to-symbolic-cached", depth, runs, [=]() {
        tlib::cleanup();
        Tree body = makeRecursiveBody(depth);
        Tree r    = rec(body);
        auto t0   = Clock::now();
        Tree sym  = deBruijn2SymCached(r);
        auto t1   = Clock::now();
        gPtrSink  = reinterpret_cast<std::uintptr_t>(sym);
        tlib::cleanup();
        return BenchResult{ms(t0, t1), "property-cache"};
    });

    reportMedian("debruijn-to-symbolic-cached-hit", depth, runs, [=]() {
        tlib::cleanup();
        Tree body        = makeRecursiveBody(depth);
        Tree r           = rec(body);
        Tree sym         = deBruijn2SymCached(r);
        auto t0          = Clock::now();
        Tree sym2        = deBruijn2SymCached(r);
        auto t1          = Clock::now();
        gPtrSink         = reinterpret_cast<std::uintptr_t>(sym2);
        std::string note = (sym == sym2) ? "cache-hit" : "NOT-SHARED";
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("symbolic-to-debruijn", depth, runs, [=]() {
        tlib::cleanup();
        Tree body = makeRecursiveBody(depth);
        Tree r    = rec(body);
        Tree sym  = deBruijn2Sym(r);
        auto t0   = Clock::now();
        Tree db   = sym2deBruijn(sym);
        auto t1   = Clock::now();
        gPtrSink  = reinterpret_cast<std::uintptr_t>(db);
        std::string note = (db == r) ? "roundtrip=yes" : "roundtrip=NO";
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("symbolic-to-debruijn-repeat", depth, runs, [=]() {
        tlib::cleanup();
        Tree body = makeRecursiveBody(depth);
        Tree r    = rec(body);
        Tree sym  = deBruijn2Sym(r);
        Tree db   = sym2deBruijn(sym);
        auto t0   = Clock::now();
        Tree db2  = sym2deBruijn(sym);
        auto t1   = Clock::now();
        gPtrSink  = reinterpret_cast<std::uintptr_t>(db2);
        // sym2deBruijn uses a local memo : the second call recomputes, and
        // pointer equality comes from hash-consing (canonical result), not
        // from a persistent cache.
        std::string note = (db == db2) ? "stable" : "BAD";
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("alpha-equivalence-symbolic", alphaWork, runs, [=]() {
        tlib::cleanup();
        Tree var1;
        Tree body1;
        Tree var2;
        Tree body2;
        Tree r1 = makeSymbolicRecursiveTree(alphaDepth, 1, 96, var1, body1);
        Tree r2 = makeSymbolicRecursiveTree(alphaDepth, 1, 96, var2, body2);
        auto t0 = Clock::now();
        bool eq = areEquiv(r1, r2);
        auto t1 = Clock::now();
        gCountSink = eq ? 1 : 0;
        std::string note = std::string("equiv=") + (eq ? "yes" : "NO");
        tlib::cleanup();
        return BenchResult{ms(t0, t1), note};
    });

    reportMedian("lift-open-rec-body", depth, runs, [=]() {
        tlib::cleanup();
        Tree body   = makeRecursiveBody(depth);
        auto t0     = Clock::now();
        Tree lifted = lift(body);
        auto t1     = Clock::now();
        gPtrSink    = reinterpret_cast<std::uintptr_t>(lifted);
        tlib::cleanup();
        return BenchResult{ms(t0, t1), ""};
    });
}

//------------------------------------------------------------------------------
// Run conditions : timings are only comparable across runs sharing these.
// A ~3-4x regression once traced back to a reconfigured build directory that
// silently lost -O3 (empty CMAKE_BUILD_TYPE) ; this header makes that visible
// in every benchmark output instead of requiring a diff against reference-perf.
//------------------------------------------------------------------------------

static std::string currentDateTime()
{
    std::time_t now = std::time(nullptr);
    char        buf[32];
    std::strftime(buf, sizeof buf, "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    return buf;
}

static std::string buildType()
{
#ifdef TLIB_BUILD_TYPE
    std::string t = TLIB_BUILD_TYPE;
    return t.empty() ? "(unspecified)" : t;
#else
    return "(unknown)";
#endif
}

static bool isOptimizedBuildType(const std::string& type)
{
    return type == "Release" || type == "RelWithDebInfo" || type == "MinSizeRel";
}

static std::string compilerInfo()
{
#if defined(__clang__)
    return std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#else
    return "unknown compiler";
#endif
}

static std::string platformInfo()
{
#if defined(__APPLE__) || defined(__linux__)
    struct utsname u;
    if (uname(&u) == 0) {
        return std::string(u.sysname) + " " + u.release + " (" + u.machine + ")";
    }
#endif
    return "unknown platform";
}

// Best-effort AC/battery detection ; diagnostic only, never affects a return
// code. macOS via `pmset`, Linux via /sys/class/power_supply, else "unknown"
// (e.g. Windows, or a desktop with no battery).
static std::string powerSource()
{
#if defined(__APPLE__)
    if (FILE* p = popen("pmset -g batt 2>/dev/null", "r")) {
        char        line[256];
        std::string out;
        while (fgets(line, sizeof line, p)) {
            out += line;
        }
        pclose(p);
        if (out.find("AC Power") != std::string::npos) return "AC power";
        if (out.find("Battery Power") != std::string::npos) return "battery";
    }
    return "unknown";
#elif defined(__linux__)
    const char* candidates[] = {"/sys/class/power_supply/AC/online",
                                 "/sys/class/power_supply/ACAD/online",
                                 "/sys/class/power_supply/ADP1/online"};
    for (const char* path : candidates) {
        if (FILE* f = std::fopen(path, "r")) {
            char c    = std::fgetc(f);
            bool onAc = (c == '1');
            std::fclose(f);
            return onAc ? "AC power" : "battery";
        }
    }
    return "unknown";
#else
    return "unknown";
#endif
}

static void printRunConditions()
{
    std::string type = buildType();
    std::cout << "run conditions\n";
    std::cout << "  date        : " << currentDateTime() << "\n";
    std::cout << "  build type  : " << type;
    if (!isOptimizedBuildType(type)) {
        std::cout << "   *** NOT an optimized build : timings are meaningless"
                      " next to a Release run ***";
    }
    std::cout << "\n";
    std::cout << "  compiler    : " << compilerInfo() << "\n";
    std::cout << "  assertions  : "
#ifdef NDEBUG
              << "disabled (NDEBUG)"
#else
              << "enabled (NDEBUG not defined)"
#endif
              << "\n";
    std::cout << "  platform    : " << platformInfo() << "\n";
    std::cout << "  power       : " << powerSource() << "\n";
}

int main(int argc, const char* argv[])
{
    tlib::init();

    int scale      = 1;
    int runs       = 15;
    int positional = 0;

    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--help") == 0 || std::strcmp(argv[i], "-h") == 0) {
            std::cout << "Usage: " << argv[0] << " [scale] [runs]\n"
                      << "       " << argv[0] << " --scale N --runs N\n";
            return 0;
        } else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc) {
            scale = parsePositive(argv[++i], scale);
        } else if (std::strncmp(argv[i], "--scale=", 8) == 0) {
            scale = parsePositive(argv[i] + 8, scale);
        } else if ((std::strcmp(argv[i], "--runs") == 0 || std::strcmp(argv[i], "-r") == 0) &&
                   i + 1 < argc) {
            runs = parsePositive(argv[++i], runs);
        } else if (std::strncmp(argv[i], "--runs=", 7) == 0) {
            runs = parsePositive(argv[i] + 7, runs);
        } else if (positional == 0) {
            scale = parsePositive(argv[i], scale);
            ++positional;
        } else if (positional == 1) {
            runs = parsePositive(argv[i], runs);
            ++positional;
        }
    }

    std::cout << "tlib performance scenarios (scale=" << scale << ", runs=" << runs
              << ", time=median)\n";
    printRunConditions();
    std::cout << std::string(82, '-') << "\n";
    std::cout << std::left << std::setw(30) << "case" << std::right << std::setw(14) << "work"
              << std::setw(12) << "median-ms" << std::setw(12) << "Mops/s"
              << "  note\n";
    std::cout << std::string(82, '-') << "\n";

    benchConstruction(scale, runs);
    benchTraversal(scale, runs);
    benchOccurrences(scale, runs);
    benchProperties(scale, runs);
    benchRewrites(scale, runs);
    benchRecursiveTrees(scale, runs);

    // Keep the sinks observable.
    if (gPtrSink == 0 && gCountSink == 0) {
        std::cerr << "unexpected empty benchmark sinks\n";
        return 1;
    }
    if (gBadNotes > 0) {
        std::cerr << gBadNotes << " scenario(s) failed their validation note\n";
        return 1;
    }
    return 0;
}
