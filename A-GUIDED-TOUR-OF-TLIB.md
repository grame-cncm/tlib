# A guided tour of TLIB

TLIB is the tree library at the heart of the [Faust](https://faust.grame.fr)
compiler. This document explains what it is made of, in the order in which its
concepts depend on one another — not in the order in which they were written.
It is addressed to a C++ programmer who knows compilers by practice rather than
by theory, and it introduces the small amount of algebra needed to see why the
library has the shape it has.

::: toc+
- **How to read this document** — the six fixed sections, the reading paths, the forward references.
- **Signatures and algebras** — one traversal, many interpretations: why a fold is the unit of work, and what it demands of everything below.
- **Hash-consing and maximal sharing** — one object per distinct term, so that structural equality becomes pointer equality.
- **Nodes and symbols** — what a node may hold, and interning as the base case of the same idea.
- **The session memory model** — allocate freely, free everything at once, and why that suits a compiler.
- **Properties** — attaching computed facts to shared terms: memoisation as the library's central service.
- **Lists, sets and environments** — derived structures encoded as terms, and so shared for free.
- **Signatures and opcodes** — constant-time constructor identity, the mechanism §1 assumed.
- **Recursive terms** — finite syntax for infinite trees: de Bruijn and symbolic forms, and canonical sharing modulo alpha-equivalence.
- **Rewriting** — bottom-up transformation of shared and cyclic terms, where memoisation becomes a termination argument.
- **Fixed points** — computing attributes over recursive terms: Kleene ascent, widening and narrowing.
- **Optional modules** — boolean conditions, occurrence counting.
- **The stack, in one picture** — what TLIB is, and what it deliberately never knows.
:::

## How to read this document

Each concept is presented in six fixed sections, always in the same order, so
that a section can be skipped without losing the thread:

| Section | What it gives you |
| :--- | :--- |
| **The idea** | an intuitive, informal picture of the concept |
| **Its role in TLIB** | why the library needs it, and what would break without it |
| **More precisely** | the concise mathematical statement, once the intuition is in place |
| **In the code** | how it is realised in C++, with pointers to the source |
| **Invariants and non-goals** | what must stay true, and what the concept deliberately does not do |
| **Origins** | where the idea comes from, and what to read about it |

Three reading paths follow from this. *The idea* + *Its role* alone give a
complete informal tour. Adding *More precisely* gives the theoretical account.
Adding *In the code* gives the implementer's account.

Each concept ends with the commit its source references were checked against.
Line numbers drift; that stamp is what tells you how much history separates the
text from the code, and turns re-verification into a targeted diff rather than a
full re-read. The behaviours the tour finds surprising are not merely described
either — they live as running checks in
[tour-examples.cpp](tour-examples.cpp), wired into `ctest`, so a claim that
stops being true breaks a test instead of quietly becoming fiction.

The concepts are ordered by dependency: each one is introduced before the
sections that rest on it. The order is not a strict layering, though, and it
would be dishonest to pretend otherwise — the first concept needs a `Tree` to
talk about at all, and mentions symbols and hash-consing well before their own
chapters.

Such forward references are signposts, and they carry a **footnote at their
first appearance** giving a thumbnail definition — enough to follow the
argument on the spot — and naming the section where the concept is properly
developed. Nothing in an argument depends on the details a footnote defers, so
a reader can safely take the thumbnail and move on, and a reader who prefers to
resolve a term before meeting it can jump ahead.

---

## 1. Signatures and algebras

### The idea

Take an ordinary compiler task. You have an expression language with four
operations — add, subtract, multiply, divide — and numbers as leaves. In C++
you would represent an expression as a tree, and then write passes over it: an
evaluator that computes a number, a type checker that computes a type, an
interval analysis that computes a range of possible values, a printer that
computes a string.

Write those four passes by hand and you notice they are the same program four
times. Each one walks the tree the same way, recurses into the children first,
and then does something at the current node that depends only on *which
operation* the node holds. The only thing that changes from pass to pass is
what "something" means:

| Pass | at a node `Add(x, y)` |
| :--- | :--- |
| evaluate | `x + y` on doubles |
| type-check | join of the types of `x` and `y` |
| interval | `[lo(x)+lo(y), hi(x)+hi(y)]` |
| print | `"(" + x + " + " + y + ")"` |

Universal algebra gives names to the three things at play here, and the names
are worth learning because the whole library is organised around them.

The **signature** is the list of operations of the language, each with the
number of arguments it takes: `Add` takes 2, `Div` takes 2, a numeric literal
takes 0. That is all a signature is — a vocabulary of constructors with their
arities. It says nothing about what they mean.

An **algebra** is one interpretation of that vocabulary. You choose a C++ type
— the *carrier* — and you supply one function per constructor, with matching
arity, over that type. Carrier `double` with `+`, `-`, `*`, `/` is one algebra.
Carrier `Interval` with interval arithmetic is another. Carrier `std::string`
with string concatenation is a third. Each row of the table above is an
algebra.

The **fold** is the one traversal shared by all of them: interpret the children
recursively, then apply the operation of the chosen algebra that corresponds to
the constructor at this node. Once the fold is written, every new pass over the
language costs exactly one new algebra — for the language above, a class with
five short methods, one per constructor — and no new traversal code.

The last piece is the observation that makes the whole thing click. Trees are
themselves one of the algebras. Take the carrier `Tree`[^tree], and for `Add` supply
the function that *builds* the node `Add(x, y)` instead of adding numbers.
Folding a tree into that algebra rebuilds the same tree. That sounds useless,
and it is exactly the point: the tree representation is not a privileged,
special thing that all the other interpretations are computed *from*. It is one
interpretation among the others — the one that happens to throw nothing away.
That is what "syntax" means, made precise. And because it throws nothing away,
every other interpretation can be obtained from it, in exactly one way.

[^tree]: `Tree` is TLIB's one tree type: a pointer to a `CTree`, which holds a **node** and a vector of child trees. The node carries either a value — an integer, a floating-point number — or a symbol, which is how a constructor such as `Add` is written. So a leaf is a tree with no children, and `Add(x, y)` is a tree whose node is the symbol `Add` and whose two children are `x` and `y`. Developed in §3.

### Its role in TLIB

This is the organising principle of the library, and it is worth being explicit
about where the boundary falls.

TLIB provides the material a syntax algebra is built from, and nothing above
it: a universal carrier of terms, and the machinery that operates on that
carrier — building, sharing, comparing, annotating, traversing, rewriting,
taking fixed points. It does not define any particular syntax algebra. In the
example of §1 it is the *client* that declares the four constructors, writes
the typed interface that fixes their arities, and writes the fold. TLIB knows
nothing about Faust signals, about types, about intervals, about audio; those
live in client libraries, which declare their own signatures and write their
own algebras.

So the deal between TLIB and its clients is: *you bring the vocabulary and the
meanings, I bring the term representation and the machinery that operates on
it, generically.* Every later concept in this document is a piece of that
machinery, and each one is justified by a demand the fold makes:

- the fold must get from a constructor to its operation **in constant time**,
  whatever the size of the signature — hence interned[^intern] symbols carrying
  dense constructor opcodes[^opcode] (§3, §7);
- the interpretation of a term must depend **only on the term**, never on how
  or when it was built — hence hash-consing (§2), which makes structurally
  equal terms literally the same object;
- since the value depends only on the term, a shared subterm need be
  interpreted **only once** — hence properties (§5), which memoise[^memo] a
  fold's results on the nodes themselves and bring a traversal back down to the
  size of the shared graph rather than of the term it denotes;
- and terms in a real compiler are recursive[^rec], which the definitions above
  do not cover at all — hence recursive terms (§8) and fixed points (§10).

[^intern]: **Interning** means keeping one canonical object per distinct value, in a table, and handing out pointers to it — as compilers do for identifiers, so that comparing two names costs one pointer comparison. Developed for symbols in §3, and applied to whole trees in §2, where it is called hash-consing.

[^opcode]: An **opcode** here is just a small integer that identifies a constructor, so that a fold can dispatch through a jump table instead of comparing names. What makes them usable is that they are *dense* (0, 1, 2, … within a language) and *disjoint* (two languages never share one). Developed in §7.

[^memo]: **Memoisation** is caching the results of a function so that a repeated call with the same argument returns the stored value instead of recomputing it. The term and the technique are due to Donald Michie, *Memo functions and machine learning*, Nature 218, 1968. It is only valid for a function whose result depends on its argument alone, which is why §1's uniqueness property and §2's pointer identity have to come first. Developed in §5.

[^rec]: A **recursive term** is one that refers to itself, as in $x = 1 + x$ — a finite piece of syntax denoting an infinite tree. Every feedback loop in a Faust program is one. They break the definitions of this section twice over: the term is no longer finite, so a fold has no base case to stop at, and the value it should compute is no longer determined by the signature alone but by a choice of *fixed point*. Developed in §8 (the terms) and §10 (their attributes).

If you keep one sentence from this section: **TLIB is a high-performance
carrier for syntax algebras, and everything else in it exists to make folds
over those algebras correct and fast.**

### More precisely

A **signature** $Σ$ is a finite set of constructor symbols, each with an arity
in $ℕ$.

A **$Σ$-algebra** $𝒜 = (A, (c_𝒜)_{c ∈ Σ})$ has two components: a carrier set
$A$, and, for every constructor $c ∈ Σ$ of arity $n$, a function
$c_𝒜 : Aⁿ → A$.

The **term algebra** $T_Σ$ is the $Σ$-algebra whose carrier is the set of
finite terms built from $Σ$, and whose operation for $c$ is the construction of
the term $c(t₁, …, tₙ)$.

A **homomorphism** $h : 𝒜 → ℬ$ between two $Σ$-algebras is a function $h : A →
B$ on their carriers that commutes with every operation:
$h(c_𝒜(x₁, …, xₙ)) = c_ℬ(h\,x₁, …, h\,xₙ)$.

$T_Σ$ is **initial**: for every $Σ$-algebra $𝒜$ there exists exactly *one*
homomorphism $⟦·⟧_𝒜 : T_Σ → 𝒜$, given by

```math
⟦c(t₁, …, tₙ)⟧_𝒜 = c_𝒜(⟦t₁⟧_𝒜, …, ⟦tₙ⟧_𝒜)
```

— to interpret a constructor applied to some subterms, interpret each subterm
first, then apply the operation that $𝒜$ gives to that constructor. The
right-hand side mentions nothing else: not where the node sits in the term, not
what was built before it, not any state carried down the traversal. That
absence is the whole content of the equation.

That homomorphism is the fold — a *catamorphism*[^cata], in the vocabulary of
functional programming, where the same construction over lists is the familiar
`fold`.

[^cata]: From the Greek *κατά*, "downwards": a catamorphism collapses a structure into a value, following its shape. The name and the systematic study of such operators come from Erik Meijer, Maarten Fokkinga and Ross Paterson, *Functional Programming with Bananas, Lenses, Envelopes and Barbed Wire*, FPCA 1991.

Two consequences are worth stating separately, because they are what the rest
of the library is built on.

**Existence** is the guarantee that writing one operation per constructor is
always enough to define a pass. You never need to know how a subterm was built
in order to interpret it; supplying the $c_𝒜$ is a complete specification. This
is what makes "one algebra per analysis" a viable architecture rather than a
slogan.

**Uniqueness** is the guarantee that the pass is well defined — there is no
second, different interpretation consistent with the same operations. In
particular the identity is the unique homomorphism $T_Σ → T_Σ$, which is the
formal content of "folding a tree into the tree algebra rebuilds the tree".
Uniqueness is also what licenses memoisation: if $⟦t⟧$ is a function of $t$
alone, caching it is not an optimisation that might change behaviour, it is
forced.

Both consequences are stated for *finite* terms. Recursive terms need a fixed
point semantics, which the signature alone does not determine; §8 and §10
return to this.

### In the code

The two assertions to look for in the executable example are these, from
`checkArithmeticSignatureFold()` in [tests.cpp:255](tests.cpp#L255):

```cpp
ArithmeticTreeAlgebra syntax;      // carrier: Tree
ArithmeticEvalAlgebra evaluation;  // carrier: double

Tree expression =
    syntax.Mul(syntax.Add(syntax.Number(2), syntax.Number(3)),
               syntax.Number(4));

CHECK(syntax.fold(expression, syntax) == expression);      // initiality
CHECK(syntax.fold(expression, evaluation) == 20);          // interpretation
```

The first assertion is initiality made testable: the unique homomorphism into
the term algebra is the identity. Note that it is written with `==` on `Tree`,
which is pointer comparison — the rebuilt term is not merely equal to the
original, it *is* the original object. That is hash-consing (§2) showing
through, and it is the reason the assertion can be written this way at all.

The interface an algebra must implement, and its two realisations, are in the
same file: [tests.cpp:177](tests.cpp#L177) for the abstract
`ArithmeticAlgebra<T>`, [tests.cpp:193](tests.cpp#L193) for the tree algebra,
[tests.cpp:244](tests.cpp#L244) for the evaluator. The C++ shape is worth a
remark: the carrier is the template parameter `T`, and the arity of each
constructor is encoded in the arity of the corresponding virtual method. Since
`Add` is declared `T Add(T x, T y)`, an algebra that gets the arity wrong does
not compile. This is where the arity in the signature lives — in the C++ type
system, not in a TLIB data structure.

Constructor symbols are declared by grouping them into a signature:

```cpp
Signature fSignature = signature("Arithmetic");
Sym fAdd = fSignature.add("Arithmetic.Add");
Sym fSub = fSignature.add("Arithmetic.Sub");
```

`signature(name)` ([symbol.hh:231](tlib/symbol.hh#L231)) returns a copyable
handle ([symbol.hh:167](tlib/symbol.hh#L167)) to an interned signature, and
`add(name)` interns a constructor symbol into it. Each signature owns a
disjoint range of 256 opcodes and assigns dense local positions inside it, so
that a fold can dispatch on `tag.localOpcode()` with a jump table instead of
comparing names:

```cpp
Sym constructor;
SymbolTag tag;
if (!isSym(expression->node(), &constructor) ||
    !getSymbolTag(constructor, tag) ||
    tag.signature != fSignature.identity() ||
    expression->arity() != 2) {
    tlib::error("invalid arithmetic expression");
}

auto x = fold(expression->branch(0), algebra);
auto y = fold(expression->branch(1), algebra);

switch (tag.localOpcode()) {
    case 0: return algebra.Add(x, y);
    ...
}
```

The mechanism itself — opcode ranges, tags, the guarantee that a symbol belongs
to at most one signature — is the subject of §7, once symbols (§3) have been
introduced properly. What matters here is only the shape: the `switch` is the
fold's dispatch, and it is O(1) in the number of constructors. The full
specification is [SIGNATURE-SPEC.md](SIGNATURE-SPEC.md); the API is
[symbol.hh:56-83](tlib/symbol.hh#L56-L83) and
[symbol.hh:221-251](tlib/symbol.hh#L221-L251).

Note finally what the fold checks before dispatching: that the node carries a
symbol, that the symbol belongs to *this* algebra's signature, and that its
arity is the expected one. TLIB trees are not intrinsically well-typed terms of
a signature — see below.

### Invariants and non-goals

**TLIB does not enforce arity.** The `Signature` class records which symbols
are constructors of which language; it does not record how many arguments each
takes. Arity is expressed by the client's algebra interface and checked on each
occurrence during the fold, as in the example above. This is a deliberate
trade: it keeps the TLIB API minimal and puts the check where the C++ compiler
can do most of the work.

**A TLIB tree is not, by construction, a term of a signature.** TLIB provides
one universal space of trees: any node, any arity, unregistered symbols and
numeric atoms all coexist. A signature is a *convention* about a subset of that
space, and conformance is established by the fold, not by the constructor. If a
client hands you a tree, you know it is a well-formed tree; that it is a
well-formed arithmetic expression is something your fold discovers.

**A symbol belongs to at most one signature, permanently.** Once
`S.add("name")` has signed a symbol, the association and its opcode are
immutable for the whole session[^session], and adding the same symbol to a second
signature fails without disturbing the first. Constructor identity is therefore
a property of the symbol, readable from any tree that uses it, at no cost per
tree.

**Signatures say nothing about meaning.** Nothing in TLIB relates a constructor
to an operation; that relation exists only inside a client's algebra, in the
body of its fold. The same term may be interpreted by any number of algebras,
and TLIB has no opinion about which one is "the" meaning.

[^session]: A **session** is the interval between `tlib::init()` and `tlib::cleanup()` — for a compiler, one compilation. Every symbol and every tree belongs to the session that built it, nothing is reclaimed before it ends, and at `cleanup()` everything goes at once. Pointers do not survive it. Developed in §4.

**Signatures partition one global namespace, they do not create their own.**
Symbols are interned by name for the whole session (§3), and a symbol belongs
to at most one signature. Two languages therefore cannot both register a
constructor called `Add`: the first `add("Add")` claims that symbol, and the
second fails. This is why the example above names its constructors
`Arithmetic.Add`, `Arithmetic.Sub`, … — qualifying constructor names by their
language is the convention that keeps independent clients out of each other's
way. What signatures make disjoint is the *opcode space*, not the *name space*.

*Code references verified at `9e26537`.*

### Origins

The framework is that of **universal algebra**, whose modern form dates from
Garrett Birkhoff's *On the structure of abstract algebras* (1935): study
algebraic structures by their signatures and identities rather than one
structure at a time.

Its arrival in computing came in the 1970s, with the observation that an
abstract data type is exactly an initial algebra — the specification says which
operations exist and which terms are equal, and initiality says there is
therefore *one* implementation up to isomorphism, and one interpretation into
any model. The reference is Joseph Goguen, James Thatcher, Eric Wagner and
Jesse Wright (the "ADJ group"), *Initial Algebra Semantics and Continuous
Algebras*, JACM 24(1), 1977. The phrase this document borrows from that
tradition — *no junk, no confusion* (§2) — is Burstall and Goguen's.

The programming-language side of the same idea, folds as the canonical way to
consume an inductive structure, is Meijer, Fokkinga and Paterson (1991), cited
in the footnote above. A reader who wants only one paper should take the ADJ
one for the *why* and the *bananas* one for the *how*.

---

## 2. Hash-consing and maximal sharing

### The idea

You already know this trick in its simplest form. In a compiler, you do not
store identifiers as strings scattered through the AST; you intern them in a
symbol table, so that every occurrence of `frequency` in the source points to
the same `Symbol` object. Comparing two identifiers then costs one pointer
comparison instead of a `strcmp`, and you can hang information on the symbol
itself rather than in a side map keyed by string.

**Hash-consing[^hashcons] is that same trick applied to trees rather than to
strings** — recursively, at every node. Instead of allocating a node whenever asked, the
library keeps a table of every node that already exists. When you ask for
$Add(x, y)$, it looks up the table; if a node with that operator and those two
children is already there, you get a pointer to it. Otherwise it allocates one,
records it, and gives you that. Two trees with the same content are never two
objects — they are one object, pointed to twice.

[^hashcons]: The name is Lisp's. `cons` is the Lisp constructor that builds a pair from two values; *hash-consing* is consing through a hash table, so that an identical pair is returned rather than built. Goto's original term for the resulting property was *monocopy*: at most one copy of any value exists in memory.

The consequences are larger than they look.

**Equality becomes pointer comparison.** Not "pointer comparison as an
optimisation for the common case", but pointer comparison as the *definition*.
If two `Tree` pointers differ, the trees differ; comparing two arbitrarily deep
terms costs one instruction. This is what made the assertion in §1 —
`syntax.fold(expression, syntax) == expression` — meaningful.

**Sharing happens whether or not you plan for it.** Nothing in the code above
says "share this subterm". You write the obvious constructor calls, and
identical subterms coalesce because there is no way for them not to. A tree
built by an unrelated pass, hours later in the compilation, will land on the
same node if it has the same content. What you get is not a tree at all but a
**DAG**, and it can be dramatically smaller than the tree it represents.
Consider:

```cpp
Tree t = tree(symbol("x"));
for (int i = 0; i < 30; i++) t = tree(symbol("Add"), t, t);
```

As a term, `t` has more than a billion leaves. As a hash-consed structure, it
is 31 nodes — `CHECK(dagSize(t) == 31)` in
[tour-examples.cpp:81](tour-examples.cpp#L81). Better still, rebuilding the
same thing later from scratch does not allocate anything: the independent
reconstruction lands on the very same object
([tour-examples.cpp:89](tour-examples.cpp#L89)). Any compiler that duplicates
subexpressions — inlining, substitution, unrolling — produces this shape
constantly, in milder form.

**Trees become immutable, and this is forced, not chosen.** If two parts of the
compiler hold the same node because it has the same content, one of them cannot
be allowed to modify it: the modification would silently reach the other. So a
node's content is fixed at construction, forever. Everything mutable has to
move elsewhere — which is why annotations live in property lists (§5) rather
than in fields.

### Its role in TLIB

§1 said the fold demands that the interpretation of a term depend only on the
term. Hash-consing is what turns that demand into a mechanical fact rather than
a discipline: the term *is* the pointer, so a table keyed by pointer is a table
keyed by term. Memoisation, which would otherwise require a hash of the whole
structure at every lookup, costs one map access on an integer-sized key. That
is the whole basis of §5, and through it, of every analysis Faust runs.

It also fixes what the layers below and above must provide. Below: the atoms
carried by nodes must themselves have decidable, cheap equality, since node
comparison is part of the lookup — hence interned symbols (§3). Also below:
ownership becomes diffuse, since a node is reachable from an unknown number of
parents and from the construction table itself. Reference counting or a garbage
collector could resolve that, at a cost paid on every operation; TLIB instead
takes the session model (§4) — allocate freely, free everything at once. The
sharing does not *force* that choice, but it is what makes it the cheap one.
Above: because equality is
now free but *structural*, anything that should identify terms up to a richer
equivalence has to be arranged by construction, by building a canonical
form[^canonical] whose sharing then does the work. That is exactly the strategy
§8 uses to make alpha-equivalent[^alpha] recursive terms be the same pointer.

[^canonical]: A **canonical form** is one chosen representative per equivalence class, computed by a function that maps every member of a class to that same representative. Its point here is that once terms are canonicalised, the coarser equivalence is decided by the structural equality this section provides — that is, by comparing two pointers. Used throughout, and put to work in §8.

[^alpha]: Two terms are **alpha-equivalent** when they differ only in the names of their bound variables: $λx.x$ and $λy.y$ are the same function written twice, and `rec(f, f+1)` and `rec(g, g+1)` are the same recursion. Names of bound variables carry no meaning, so a compiler that treats these as different terms duplicates work and misses sharing. Developed in §8.

One thing hash-consing does *not* buy is worth stating here, because it is the
most common misunderstanding. Sharing the *storage* of a subterm does not share
the *work* of traversing it. A fold written the obvious way over the 31-node
DAG above still recurses into both branches of every node, and still performs
its billion operations — the sharing is invisible to a traversal that does not
look for it. Hash-consing makes memoisation possible and cheap; it does not
perform it. §5 is where the exponent actually disappears.

### More precisely

Let $T_Σ$ be the term algebra of §1. Hash-consing implements a function
$⌜·⌝ : T_Σ → \mathrm{Addr}$ from terms to machine addresses which is
**injective**: $⌜s⌝ = ⌜t⌝$ if and only if $s = t$. Structural equality on terms
is thereby *decided* by equality on addresses.

Equivalently, in the vocabulary of abstract data types, the pointers realise
$T_Σ$ **faithfully**, in the two senses that characterise initiality:

- **no junk** — every live address was produced by a constructor application,
  `CTree::make` being the only way the API offers to obtain one, so every value
  denotes a term;
- **no confusion** — distinct terms are never identified: $s ≠ t$ implies
  $⌜s⌝ ≠ ⌜t⌝$.

Hash-consing adds to *no confusion* its converse in the strong, computational
form: equal terms are not merely "equal by some recursive test" but represented
by the *same* object. The unique-representative property is maintained
inductively from the leaves: a node is looked up by its operator and by the
**addresses** of its children, which is sound precisely because the children
already satisfy the property. Constructing a term of size $n$ therefore costs
$O(n)$ lookups amortised, each of them $O(\mathrm{arity})$, rather than
anything proportional to the size of the subterms.

The representation of a term is a DAG whose node count is the number of
**distinct subterms** of the term, which may be exponentially smaller than the
term's size — as in the example above, where $n$ constructor calls denote a
term with $2ⁿ$ leaves. Note the asymmetry that motivates §5: the DAG is
exponentially smaller, but the *unfolded* traversal of it is not.

Finally, one property is deliberately *not* claimed. Terms are identified up to
structural equality and nothing more. Any coarser equivalence — commutativity,
neutral elements, arithmetic identities — is a different relation, and if a
client wants terms to be shared modulo that relation, it must build a canonical
form and let structural sharing apply to *it*.

### In the code

The whole mechanism is `CTree::make` in
[tree.cpp:319](tlib/tree.cpp#L319):

```cpp
size_t hk = calcTreeHash(n, ar, tbl);
Tree   t  = gHashTable[hk % gHashTableSize];

while (t && !t->equiv(n, ar, tbl)) {
    t = t->fNext;
}

if (t) { statsTreeReused();  return t; }      // the term already exists
else   { statsTreeCreated(); /* allocate */ }
```

Everything else is detail around those seven lines. `CTree` itself is declared
at [tree.hh:138](tlib/tree.hh#L138); the public constructors `tree(n)`,
`tree(n, a)`, … `tree(n, br)` at
[tree.hh:350-389](tlib/tree.hh#L350-L389) are thin wrappers over `make`. The
`CTree` constructors are protected, so no caller can bypass the table and
produce an unregistered node; a derived class still could, and *no junk* is
therefore an invariant of the API as it is meant to be used rather than one the
type system enforces outright.

Two details in those lines matter more than their size suggests.

`equiv` ([tree.cpp:290](tlib/tree.cpp#L290)) compares the node and then the
children **by pointer**, not recursively:

```cpp
if (fNode != n || fBranch.size() != size_t(ar)) return false;
for (int i = 0; i < ar; ++i) {
    if (fBranch[i] != br[i]) return false;
}
```

This is the induction of the previous section made concrete. A structural
comparison here would make construction quadratic; pointer comparison is legal
only because every child was itself obtained from `make`.

`calcTreeHash` ([tree.cpp:308](tlib/tree.cpp#L308)) combines the raw bits of
the node with the *stored hash keys* of the children — again not by traversing
them — so hashing a node is $O(\mathrm{arity})$ regardless of depth. The hash
is only a bucket index: correctness rests entirely on `equiv`, and a collision
costs a walk down `fNext`, never a wrong answer.

Beyond the sharing itself, `CTree` stores three things derived from the term
that are worth knowing about now, since later sections rely on them.

**`fSerial`** ([tree.hh:252](tlib/tree.hh#L252)) is a counter incremented at
each construction, and `std::less<CTree*>` is specialised to compare on it
([tree.hh:341](tlib/tree.hh#L341)) so that `std::map<Tree, …>` iterates in a
defined order instead of in address order. Determinism of the compiler output
depends on this: addresses vary from run to run for reasons no one controls,
whereas a serial is a function of construction order alone — so a deterministic
program fed the same input reproduces the same serials. The order is history-
dependent by nature: build the same trees in a different order and every serial
changes.

**`fCanonHash`** ([tree.hh:253](tlib/tree.hh#L253)) exists for the cases where
that is not good enough. It is a structural hash synthesised at construction
from the node's canonical hash and the children's — and the way the children
are combined ([tree.cpp:186](tlib/tree.cpp#L186)) is worth a second look,
because the obvious formula is wrong here:

```cpp
h ^= br[i]->canonHash() + 0x9e3779b97f4a7c15ULL + (h << 12) + (h >> 4);
```

The addition is what matters. Write the combine the tempting way, as
`h = h * F ^ child`, and it becomes **XOR-linear**: two identical children
contribute the same value twice and cancel each other out. Terms with repeated
identical subterms — a stereo output whose two channels are equal, two equal
definitions in one recursive group — would then hash to a constant, and
collapse together in any name derived from the hash. Mixing an addition in
breaks the linearity, so cancellation cannot happen. The same flaw existed in
Faust's associative-commutative judge and was fixed there too; the pattern is
now banned in both.

`canonicalTreeLess`
([tree.cpp:229](tlib/tree.cpp#L229)) uses it as the primary key of a total
order derived from *values only* — symbols compared by name, ties broken
structurally. Two processes that build the same term values order them
identically, whatever their construction history, which is what canonical forms
need. One exception is deliberate: a node carrying a raw pointer payload falls
back to hashing the pointer ([node.hh:104](tlib/node.hh#L104)), so terms
containing such nodes are outside the canonical guarantee — they are not meant
to enter canonical orderings.

**`fAperture` and `fContains`** ([tree.hh:187-188](tlib/tree.hh#L187-L188)) are
synthesised attributes: small facts about the whole subterm — how many free de
Bruijn[^debruijn] levels it has, whether it contains a recursive node —
computed once in the constructor and read in $O(1)$ ever after. They are the
degenerate case
of the memoisation idea: an attribute that is a function of the term, and that
TLIB knows about intrinsically, can simply live in the node. Attributes TLIB
does not know about need the general mechanism of §5.

[^debruijn]: The **de Bruijn** representation removes the names of bound variables: a variable is written as the number of binders standing between it and the one that binds it, so $λx.λy.x$ becomes $λ.λ.2$. The payoff is exactly what §2 is about — alpha-equivalent terms become *syntactically identical*, hence the same hash-consed pointer, with no renaming pass and no comparison modulo renaming. A term's **aperture** is how many of its de Bruijn references still point outside it, which is what makes it *open* or *closed*. Developed in §8.

Two measurements on real Faust programs give the practical scale: about 72% of
constructed trees never receive a single property
([tree.hh:161](tlib/tree.hh#L161)), which is why property lists are allocated
lazily rather than being an inline member; and most insertions land on an empty
bucket, which is why the load-factor check runs only when the bucket was
already occupied ([tree.cpp:335](tlib/tree.cpp#L335)).

### Invariants and non-goals

**Structural equality is pointer equality — in both directions.** For any two
live trees, `p == q` if and only if they have the same node and the same
branches. This is the single invariant the whole library rests on, and every
other component is written assuming it.

**A tree is never modified after construction.** Node and branches are fixed;
the only mutable state on a `CTree` is annotation (properties, type slot, visit
stamp), and §5 states the condition annotations must satisfy to stay sound.
There is no API to change a branch, and adding one would break every other
holder of the same node.

**Sharing is structural, never semantic.** $Add(1, 2)$ and $Add(2, 1)$ are
different trees. $Mul(x, 1)$ and $x$ are different trees. TLIB has no notion of
which constructors are commutative, associative or neutral, and will not
normalise anything on your behalf. Normalisation is a client's fold or rewrite
(§9), and its output is shared just like any other term.

**Pointer values are meaningless outside the session.** Addresses vary between
runs; `fSerial` is reproducible only for a given construction history; only
`canonHash`-based orders are reproducible across processes, and then only for
terms free of raw pointer payloads. Whenever an ordering must survive a change
in construction history — canonical forms, term normalisation — use
`CanonicalTreeLess` ([tree.hh:500](tlib/tree.hh#L500)), not the default.

**Hash-consing does not make traversals cheap.** It removes duplicate storage,
not duplicate work. An unmemoised fold costs the size of the *term*, not of the
DAG, and the gap between the two is unbounded — exponential in the worst case,
as in the example above. Memoisation is what closes it; see §5.

**Nothing is freed individually.** Every tree ever built stays reachable from
the construction table, and a node shared by an unknown number of parents has
no obvious owner to release it. The library does not attempt reclamation during
a session at all — see §4 for what it does instead, and why that suits a
compiler.

*Code references verified at `9e26537`.*

### Origins

The technique is older than it looks. A. P. Ershov, *On programming of
arithmetic operations* (CACM 1(8), 1958), used a hash table to recognise
identical subexpressions while compiling arithmetic — common-subexpression
elimination, and already the whole idea: hash the operands, find the
expression, reuse it. The name came later, from Eiichi Goto's work on symbolic
manipulation in Lisp (*Monocopy and associative algorithms in extended Lisp*,
University of Tokyo, 1974), which made the property global rather than local to
one expression.

The most spectacular demonstration of what maximal sharing plus memoisation
buys is Randal Bryant's *Graph-Based Algorithms for Boolean Function
Manipulation* (IEEE Trans. Computers, 1986). A reduced ordered BDD is precisely
a maximally shared DAG of a decision term, and its central operations combine
that canonical DAG with memoised recursive traversal — `Apply`, for instance,
recurses on both arguments and keeps a table of the pairs it has already
handled. That combination turned Boolean function manipulation from intractable
into routine, and it is architecturally what §2 and §5 describe here.

For the engineering rather than the theory, Jean-Christophe Filliâtre and
Sylvain Conchon's *Type-Safe Modular Hash-Consing* (ML Workshop, 2006) is the
short, practical reference: the same design questions TLIB answers — the table,
the collisions, the memory model, the interaction with memoisation — worked
through in a different language.

---

## 3. Nodes and symbols

### The idea

A TLIB tree is a **node** plus a list of child trees, and nothing else:

```tree svg "the term (2 + 3) * 4, with the content of each node"
Sym(Arithmetic.Mul)
  Sym(Arithmetic.Add)
    Int(2)
    Int(3)
  Int(4)
```

So the whole question of this section is: *what can a node hold?* And the
answer is settled almost entirely by one constraint inherited from §2. Every
single call to `tree(…)` compares a candidate node against the nodes already in
the table. Node equality is therefore on the hottest path in the library, and
it must be **exact** — an approximate answer would merge two different terms —
and **constant-time**.

That immediately rules out the obvious representation. A node cannot hold a
`std::string`: comparing two would cost a `strcmp`, and every tree construction
would pay it. It cannot hold a polymorphic object either, since comparing two
of those means a virtual call at best.

What it holds instead is a **tagged union** of five fixed-size payloads — a
32-bit integer, a 64-bit integer, a double, a symbol, or a raw pointer — so
that comparing two nodes is comparing a tag and one machine word.

The interesting case is the fourth. A **symbol** is an interned name: TLIB
keeps a table of every name ever requested, and `symbol("Arithmetic.Add")`
always returns the same `Symbol` object. Two symbols are equal exactly when
their names are, and the test costs one pointer comparison.

Notice what just happened. The trick of §2 — keep a table, hand out one shared
object per distinct value, let pointer equality decide — is being applied a
second time, one level down, to strings instead of trees. **TLIB is essentially
that one idea at two scales**: symbols are interned names, trees are interned
nodes-with-children, and each level makes the level above it cheap. Symbols are
the base case; without them, tree construction would have no fast, exact node
comparison to build on.

A node plays two roles, and TLIB does not distinguish them. At an internal node
a symbol is a **constructor name**; at a leaf, an integer or a double is an
**atom**, a value in its own right. Both are just nodes, and a symbol with no
children is a perfectly good leaf too. The distinction that matters to a fold —
which symbols are constructors of *my* language — is the business of signatures
(§7), not of nodes.

### Its role in TLIB

Three things rest on this section.

**It closes the induction of §2.** Hash-consing compares a node and then the
children by pointer; the children are cheap because they are hash-consed, and
the node is cheap because it is a word and a tag. The recursion bottoms out
here, and the whole $O(\mathrm{arity})$ cost of construction depends on it
bottoming out in constant time.

**It is what makes the term universe universal.** §1 said TLIB carries any
client's syntax algebra without knowing anything about it. That is possible
precisely because a constructor is nothing but a symbol, and any name can be
interned. A new language costs no new type, no new node kind, no change to
TLIB: it is a set of names, and its terms live in the same universe as
everybody else's.

**It is where per-constructor data can live.** A symbol is unique per name, so
anything true of a *name* can be stored once, on the symbol, and read from
every tree that uses it. That is exactly how §7 attaches a signature and an
opcode to a constructor: not one field per tree — which would cost memory on
millions of nodes — but one field per symbol, reached through the node the tree
already holds. Interning enables annotation, which is the same argument that
returns in §5 for trees and properties.

Two smaller consequences are worth flagging, because later sections rely on
them. Because symbols have *names*, a hash derived from the name rather than
the address is available, which is what lets §2's `canonHash` be reproducible
across processes — the recursion bottoms out in a value, not in an address.
And because names can be *generated*, TLIB can mint fresh symbols[^gensym] on
demand, which is how §5 gives each property a private key and how §9 names the
variables it introduces.

[^gensym]: `unique("W")` returns a symbol named `W0`, `W1`, `W2`, … guaranteed not to collide with any existing one. The need is as old as Lisp macros, where the same operator is called `gensym`: a program that generates a binding must be able to name it without capturing a name the user chose. In TLIB it is used for the fresh variables introduced by rewriting (§9) and — less obviously — to give every `property` object a private key that no other property can collide with (§5).

### More precisely

Write $\mathrm{Sym}$ for the set of interned symbols. The set of nodes is a
disjoint sum, with decidable equality:

```math
N = \mathbb{Z}_{32} ⊎ \mathbb{Z}_{64} ⊎ \mathbb{D} ⊎ \mathrm{Sym} ⊎ \mathrm{Ptr}
```

— a node is exactly one of five things: a 32-bit integer, a 64-bit integer, a
double, a symbol, or a raw pointer. The sum is *disjoint*, which is the part
that matters in practice: the tag is carried along with the value, so an
integer node is never confused with a double node whatever the bits happen to
say, and equality can always begin by comparing tags.

The type of trees is then given by a single recursive definition:

```math
\mathrm{Tree} = N × \mathrm{Tree}^{*}
```

— a node paired with a finite sequence of subtrees. The equation is to be read
*inductively*: of its several solutions we take the smallest, the trees of
finite depth, which is the one §1's folds need if they are to reach a leaf and
stop. It is also, in the vocabulary of §1, the initial one.

This carrier never changes. §8 does not replace it by something infinite: it
adds two ordinary constructors — a binder and a reference to it — so that a
*finite* tree can denote an infinite unfolding. The trees stay finite, their
meaning need not, and that gap is what makes §8 and §10 delicate. It is also
why the notation $μ$ is kept in reserve here: there it will denote recursion
*inside* a term, a different thing from the recursion of the definition above.

This equation is the precise content of "TLIB provides a universal carrier".
There is exactly **one** tree type, not
one per language: any signature $Σ$ of §1 embeds into it through an injection
$Σ → \mathrm{Sym}$, and its term algebra $T_Σ$ appears as the subset of
$\mathrm{Tree}$ whose nodes are in the image of that injection and whose
arities agree. Well-formedness with respect to $Σ$ is a *predicate on* the
universe, not a property of the type — which is why §1 concluded that
conformance is something a fold checks rather than something a constructor
guarantees.

Interning is the statement that naming is a bijection. Writing
$\mathrm{sym} : \mathrm{String} → \mathrm{Sym}$ and
$\mathrm{name} : \mathrm{Sym} → \mathrm{String}$:

```math
\mathrm{name}(\mathrm{sym}(s)) = s
\qquad
\mathrm{sym}(\mathrm{name}(q)) = q
\qquad
p ≠ q ⟺ \mathrm{name}(p) ≠ \mathrm{name}(q)
```

The first two equations say that interning and naming are inverse to each
other: intern a name and read it back, you get the name; read a symbol's name
and intern it, you get the same symbol. The third is the one everything rests
on — two symbols differ exactly when their names differ — and it is what makes
`p == q` a legitimate test of "same name".

The first equation holds for names already in normal form; TLIB normalises
control characters on the way in, as the invariants below record.

### In the code

`Node` is at [node.hh:77](tlib/node.hh#L77), and it is exactly the tagged union
described above:

```cpp
class Node : public Garbageable {
    int fType;                     // kIntNode, kInt64Node, kDoubleNode, kSymNode, kPointerNode
    union { int i; double f; Sym s; void* p; int64_t v; } fData;
```

Equality ([node.hh:176](tlib/node.hh#L176)) is the one line the rest of the
library leans on:

```cpp
bool operator==(const Node& n) const { return fType == n.fType && payload() == n.payload(); }
```

`payload()` ([node.hh:93](tlib/node.hh#L93)) reads the union as one opaque
64-bit word, whatever member was actually written — the comparison is on the
payload's *bits*, not on its value. It is spelled with `memcpy` (the C++17
spelling of `std::bit_cast`, which compiles to a single load) rather than by
reading an inactive union member, which every mainstream compiler supports but
the standard does not.

This explains a detail that would otherwise look superstitious: the narrower
constructors write `fData.f = 0.0` *before* storing their value
([node.hh:130-162](tlib/node.hh#L130-L162)). Zeroing the widest member first
makes the unused bits deterministic, so that two nodes built from the same
`int` compare equal — which is what makes a whole-word comparison exact for
payloads narrower than the word.

Comparing floating-point payloads by bits rather than with `==` is not merely a
shortcut, it is necessary. IEEE equality is not reflexive: a `NaN` is not equal
to itself. A hash-consing table built on it would fail to find a `NaN` node it
had just inserted, and would keep allocating new ones forever. Bitwise
comparison restores reflexivity and makes node equality a genuine equivalence
relation, which §2 needs it to be. The price is a surprise in the other
direction: `+0.0` and `-0.0` have different bit patterns, so they are different
nodes. Both halves are checked in
[tour-examples.cpp:114-122](tour-examples.cpp#L114-L122), next to the IEEE
behaviour they depart from.

Pattern matching is a family of predicates rather than a `switch` on the tag —
`isInt(n, &i)`, `isDouble(n, &d)`, `isSym(n, &s)`
([node.hh:212](tlib/node.hh#L212) onwards), each testing the tag and extracting
the payload in one call. Their tree-level counterparts `tree2int`, `tree2str`
and friends ([tree.hh:392](tlib/tree.hh#L392) onwards) do the same one level
up, raising a TLIB error instead of returning false.

Symbols are in [symbol.hh:88](tlib/symbol.hh#L88) and
[symbol.cpp:130](tlib/symbol.cpp#L130). `Symbol::get` is the same shape as
`CTree::make` — hash the name, walk the bucket chain, return the existing entry
or allocate one — which is the code-level form of "the same idea at two
scales". Each `Symbol` then carries, besides its name:

- `fData`, a free `void*` slot for the client (`getUserData` / `setUserData`);
- `fSignature` and `fOpcode`, the constructor identity of §7, written once by
  `Signature::add` and immutable thereafter;
- `fHash`, the name hash used for the table, and `fCanonKey`
  ([symbol.cpp:248](tlib/symbol.cpp#L248)), a second, *canonical* key which is
  the same thing except for names of the form `R<instance>_<k>`, where the
  instance number is stripped. Those names are generated by §8's
  canonicalisation, and stripping the instance is what lets orders derived from
  the key be independent of how many times canonicalisation has run in the
  session.

`unique(prefix)` is `Symbol::prefix` ([symbol.cpp:188](tlib/symbol.cpp#L188)):
a per-prefix counter, a check that the name really is new, and a hard failure
after 10 000 attempts.

One detail in `Symbol::get` is easy to miss and shows up in the invariants
below: every character below 32 is replaced by a space before the name is
hashed ([symbol.cpp:135](tlib/symbol.cpp#L135)). Names are normalised, so
`symbol("a\nb")` and `symbol("a b")` are the *same* symbol
([tour-examples.cpp:132](tour-examples.cpp#L132)).

### Invariants and non-goals

**One symbol per name, for the whole session.** $p ≠ q$ if and only if the
names differ. Symbol pointers are stable for the session and are never freed
before it ends (§4).

**Names are normalised on the way in.** Control characters become spaces, so
two names differing only in such characters denote one symbol. This is
deliberate — it keeps generated and printed names well behaved — but it means
`name(symbol(s)) == s` holds only for names already in normal form.

**Node equality is on representation, not on numeric value.** `Node(1)` and
`Node(1.0)` are different nodes, since the tags differ. `+0.0` and `-0.0` are
different nodes. Two `NaN`s with the same bit pattern are the *same* node.
TLIB compares what is stored, never what it might mean; numeric coercion is a
client's business.

**Nothing constrains which node kinds may have children.** `tree(Node(3), a, b)`
is accepted: an integer node with two branches. Treating numbers as leaves is a
convention of every sane client, not a rule TLIB enforces — the same
permissiveness as §1's "a tree is not, by construction, a term of a signature".

**The pointer payload is opaque and non-canonical.** TLIB never dereferences
it, never frees it, and hashes it by address, so terms containing pointer nodes
fall outside the cross-process guarantees of §2. It is an escape hatch for
foreign data — in Faust, boxed primitives — and its lifetime is the client's
problem.

**Generated names depend on session history.** `unique("R")` numbers its
results from a counter, so the same computation run twice in one session
produces different names, and two sessions agree only if they generate the same
names in the same order. Anything that must be canonical cannot be built on
`unique()`; §8 derives names from *content* instead, which is what makes
alpha-equivalent recursive terms land on the same pointer.

**The set of payload kinds is closed.** Adding a sixth kind means editing
`Node`, its equality, its canonical hash and its predicates. The pointer
payload exists precisely so that this is rarely necessary.

*Code references verified at `9e26537`.*

### Origins

The shape of the data is John McCarthy's, in the paper that started the field:
*Recursive Functions of Symbolic Expressions and Their Computation by Machine,
Part I* (CACM 3(4), 1960). An S-expression is an atom or a pair of
S-expressions — TLIB's node and its branches — and structures are shared rather
than copied. The paper also gives atomic symbols a **property list**, a place
to attach facts about a name rather than about an occurrence: that is the
ancestor of the signature and opcode fields §7 stores on a `Symbol`, and, one
level up, of the tree properties of §5.

Interning as a *global* mechanism — one table holding every symbol, so that
reading the same name twice yields the same object and `eq` decides equality in
one instruction — belongs to the implementations rather than to the 1960 paper;
the object list is documented in the *LISP 1.5 Programmer's Manual* (McCarthy
et al., MIT Press, 1962) and became standard equipment afterwards. It is worth
keeping the two apart: TLIB's symbol table descends from the object list, while
TLIB's per-symbol and per-tree annotations descend from property lists.

For the table itself, the reference is Knuth's *The Art of Computer
Programming*, volume 3, §6.4 — separate chaining, load factors and rehashing
are exactly what `Symbol::get` and `CTree::make` implement.

---

## 4. The session memory model

### The idea

Ask the obvious question about §2 and you get an uncomfortable answer. A tree
is reachable from every parent that contains it, from the construction table
that produced it, and from any property list that mentions it — and none of
those knows about the others. So: **who deletes it?**

The classic answers all fit badly here.

*Reference counting* is the reflex, but the construction table holds a
reference to every tree that exists, so no count ever reaches zero. You would
have to make the table's reference special, then decide when to sweep it,
which is a garbage collector wearing a disguise.

*Garbage collection* would work, at the price of knowing the roots, scanning,
and — the awkward part — removing dead entries from the hash table as it goes.

*Individual deletion* is not even expressible: no piece of code is in a
position to know that it holds the last use of a subterm, because sharing is
precisely the property that hides that information.

TLIB takes the fourth answer: **nobody deletes anything, until everything is
deleted at once**. A **session** is the interval between `tlib::init()` and
`tlib::cleanup()` — for a compiler, one compilation. During the session,
allocation is free and reclamation does not happen. At `cleanup()`, every tree,
every symbol and every property table goes in one sweep, and the library is
immediately ready for the next session.

A C++ programmer will recognise the arena, or region, pattern: group objects
with a common lifetime and release them together. The twist is the choice of
region. Here there is exactly one, and it is the whole library for the whole
compilation.

This looks like a shortcut and it is worth insisting that it is not. Freeing a
tree individually would not merely be difficult — it would be **unsound**,
because it would break §2. Suppose a tree were freed and the allocator later
handed the same address back for an unrelated term. Some pointer obtained
earlier would then compare equal to a tree it has nothing to do with, and
"pointer equality is structural equality" would silently become false. The
session model is what makes that impossible: within a session, an address is
handed out once and means the same term forever.

### Its role in TLIB

The memory model is not a service TLIB offers on the side; it is the condition
under which the previous two sections are true at all.

**It makes the construction table harmless.** §2's table keeps a pointer to
every tree ever built. Under any reclamation scheme that would be a leak to
manage or a weak-reference mechanism to design. Under the session model it is
simply not a question.

**It makes annotation safe.** §5 attaches computed facts to trees, keyed by
tree pointers. That is only meaningful if a tree outlives every table that
mentions it, which here is automatic: nothing outlives the session and nothing
dies before it.

**It makes memoisation valid across an entire compilation.** A fold's result
cached during an early pass is still attached to the right node in a later
pass, because the node has not moved and cannot have been recycled.

And it fixes the shape of the contract with the host application. `Tree` and
`Sym` are not owning handles; they are borrowed pointers whose validity is the
session's. A batch compiler never notices. A hosted compiler — `libfaust`
compiling one program after another — must call `cleanup()` between them, and
must not keep a `Tree` across the boundary.

### More precisely

This is **region-based memory management** in its simplest form: lifetime is a
property of a *region*, not of an object. Allocation puts an object in the
region; nothing is deallocated individually; the region is released as a whole.
TLIB has exactly one region per session.

The property that matters is stronger than "memory is eventually reclaimed".
Write $⌜·⌝$ for §2's map from terms to addresses. §2 claimed it is injective.
What the session model adds is that it is also **stable**:

> Within a session, $⌜t⌝$ is defined once and never changes, and no address is
> ever reused for a different term.

Without stability, injectivity would only hold instant by instant, and every
pointer held across a deallocation would be suspect. With it, a `Tree` obtained
at any point in the session remains a valid, exact name for its term until
`cleanup()`. This is what licenses pointer-keyed tables (§5), pointer equality
as term equality (§2), and serial numbers as a stable order (§2). The boundary
itself is exercised in [tour-examples.cpp:151](tour-examples.cpp#L151): a term,
then `cleanup()`, then the same term rebuilt in a fresh session — with the
comment marking the exact line past which the earlier pointer must not be
touched.

The cost is stated just as simply: peak memory is the **total** allocated
during the session, not the live set at any moment. There is no reuse. The
model is therefore sound exactly when sessions are bounded — which is the case
for a compilation, and is not the case for a long-running interactive process
that never calls `cleanup()`.

### In the code

Everything hangs on one base class, [garbageable.hh:41](tlib/garbageable.hh#L41):

```cpp
class TLIB_API Garbageable {
   public:
    static void* operator new(std::size_t size);
    static void  operator delete(void* ptr);
    static void  cleanup();   ///< delete every Garbageable allocated since the last cleanup
};
```

`CTree`, `Symbol` and `Node` all derive from it. `operator new`
([garbageable.cpp:79](tlib/garbageable.cpp#L79)) allocates normally and then
records the pointer in a global list; `cleanup()`
([garbageable.cpp:51](tlib/garbageable.cpp#L51)) walks that list and deletes
every entry.

It is worth being precise about what this is and is not. It is a **registry**,
not an arena: allocation still goes through `::operator new`, plus one list
node per object. The win is not allocation speed — a bump allocator would be
faster — but ownership: no code anywhere has to decide whether it holds the
last use of anything. A flag, `gHeapCleanup`
([garbageable.cpp:49](tlib/garbageable.cpp#L49)), tells individual deletes to
skip the registry while the sweep is running, which is what keeps `cleanup()`
linear instead of quadratic.

Individual deletion *is* still possible during a session, and its cost tells
you it is not meant to be common: `operator delete` calls `std::list::remove`
([garbageable.cpp:95](tlib/garbageable.cpp#L95)), a linear scan of every live
object. Delete one object, fine; delete in a loop and the session is quadratic.

The registries are function-local statics
([garbageable.cpp:35](tlib/garbageable.cpp#L35)) rather than file-scope ones —
construct-on-first-use. A `Garbageable` may well be allocated from another
translation unit's static initialiser, and C++ leaves the relative order of
those unspecified; a function-local static is initialised on first call,
whatever the order.

`tlib::init()` and `tlib::cleanup()` ([tlib.cpp:43](tlib/tlib.cpp#L43)) are the
session boundary, and `cleanup()` does two things rather than one:

```cpp
void cleanup()
{
    Garbageable::cleanup();   // free every tree, symbol, property table
    resetInternals();         // and reset the tables and internal caches
}
```

The second line is the subtle one. TLIB itself holds a few lazily interned
symbols and cached key trees — the list `cons`/`nil`, the recursion symbols,
the property keys of `recursive-tree.cpp`. Those die with everything else in
the first line, so the static variables pointing at them must be cleared too,
or the next session would start with pointers into freed memory. That is what
`tlibResetListInternals()` and `tlibResetRecInternals()`
([tlib.cpp:30-31](tlib/tlib.cpp#L30-L31)) exist for. The rule generalises: a
cache holding `Tree` values is session state, and must be reset when the
session is.

One platform difference is worth knowing before it surprises you. On Windows
([garbageable.cpp:55-63](tlib/garbageable.cpp#L55-L63)), `cleanup()` frees the
memory of each object without invoking its destructor, because an object using
virtual inheritance from `Garbageable` may not have the same complete-object
address as the stored pointer. Memory is reclaimed either way, but a
destructor's own work is not done — `CTree::~CTree`, for instance, is what
deletes a node's property map.

### Invariants and non-goals

**Every `Tree` and every `Sym` is invalid after `cleanup()`.** They are
borrowed pointers, not handles, and their lifetime is exactly the session's.
Storing one in a structure that outlives the session is the one truly fatal
mistake this design allows.

**Nothing is reclaimed during a session.** Memory grows monotonically with the
number of *distinct* terms built. Maximal sharing is what makes this
affordable: what grows is the number of distinct subterms, not the number of
times they are used.

**A session is single-threaded.** The construction table, the symbol table and
the allocation registries are global mutable state with no synchronisation
anywhere in the library. Two threads building trees concurrently corrupt them.
(The one exception is diagnostic: the printer's context in
[recursive-print.cpp:41](tlib/recursive-print.cpp#L41) is `thread_local`, so
printing from several threads is safe.)

**`Garbageable` is not a general-purpose allocator.** It is a registry of
objects with a single common lifetime. Using it for objects that should die
early converts them into leaks-until-cleanup, and deleting them by hand costs
a linear scan.

**There is no reference counting, and a raw `Tree` needs no wrapper.** The
session model is the whole storage story: a raw pointer is valid for the whole
session, which is the longest anything lives. `P<T>`
([smartpointer.hh:22-26](tlib/smartpointer.hh#L22-L26)) looks like an owning
smart pointer and is not one — it is a null-checking wrapper with an empty
destructor, unused by the library itself but still live downstream, where
Faust's audio types are `Type = P<AudioType>`. Read it as a null-safety
convenience, never as ownership.

*Code references verified at `9e26537`.*

### Origins

The technique is old and has been rediscovered under several names — arenas,
regions, zones, pools. The classic engineering reference is David Hanson's
*Fast allocation and deallocation of memory based on object lifetimes*
(Software: Practice and Experience 20(1), 1990), which makes the case exactly
as this section does: group objects whose lifetimes coincide, allocate
cheaply, and free the group in one operation instead of tracking objects
individually.

The theoretical development is Mads Tofte and Jean-Pierre Talpin's
*Region-Based Memory Management* (Information and Computation 132(2), 1997),
where a type-and-effect system *infers* the regions rather than leaving them to
the programmer. TLIB does not need the inference — it has one region — but the
paper is where the notion that lifetime can be a property of a region rather
than of an object is worked out properly.

The pattern is also standard practice in compilers built since: LLVM's
`BumpPtrAllocator` and the per-pass arenas of most modern compiler
infrastructures rest on the same observation, that a compilation is a bounded
batch process whose peak memory is bounded by its input.

---

## 5. Properties

### The idea

Everything so far has been building up to this one. §1 showed that a pass over
a term is a fold. §2 made structurally equal terms be the same object. §4 made
that object's address stable for the whole compilation. Put the three together
and the conclusion is immediate: **the result of a fold can be cached on the
node itself, and looked up by pointer.**

Why it must be cached at all is worth re-deriving, because the numbers are
brutal. Take the 31-node DAG of §2, the one denoting a term with 2³⁰ leaves. A
fold written the obvious way recurses into both children of every node, so it
performs a billion operations on a structure of 31 nodes. It re-computes the
value of the *same shared subterm* over and over — and §1's uniqueness theorem
says that value cannot possibly differ between visits. Every recomputation is
provably redundant. Memoise, and the same fold performs 31 operations.

So a pass needs a table from tree to result. The obvious C++ answer is a
`std::unordered_map<Tree, P>` living in the pass. TLIB offers something else:
the table is turned inside out and **distributed over the nodes themselves**.
Each `CTree` carries a small map, and an annotation is stored there:

```cpp
property<int> depth;          // one pass's annotation
depth.set(t, 3);              // stored on t itself
int d; depth.get(t, d);       // read back from t
```

Two things make this work. First, a lookup no longer searches anything global —
you already hold `t`, so you are already at the table. Second, and less
obviously, the cache inherits the lifetime of what it annotates: the annotation
dies with the node, at `cleanup()`, and no pass ever has to remember to clear
its table.

The remaining question is how one node distinguishes the annotations of a dozen
different passes. The answer is a small trick with a large payoff: a property's
key is itself a **tree**, built from a freshly generated symbol (§3), minted
when the `property` object is constructed. Two `property<int>` objects created
by two unrelated passes therefore have different keys and cannot collide — with
no registry of property names, no enum to extend, and no coordination between
passes that do not know about each other.

### Its role in TLIB

This is the service the whole library exists to provide. Sections 1 to 4 each
established one of its preconditions, and none of them is dispensable:

| Precondition | From | Without it |
| :--- | :--- | :--- |
| the value depends only on the term | §1 (uniqueness) | the cache returns wrong answers |
| equal terms are one object | §2 (hash-consing) | the cache misses every shared subterm |
| addresses are stable and unique | §4 (session) | a cached entry can migrate to another term |
| the annotation outlives no one | §4 (session) | dangling entries, or manual invalidation |

Turn any one of them off and memoisation stops being sound, cheap, or safe.
That is why this concept comes fifth rather than first: it is not a feature
bolted on, it is what the four preceding decisions were *for*.

Its practical role is equally direct. A Faust compilation is a sequence of
passes over the same shared graph — typing, interval analysis, occurrence
counting, code generation — and each is a fold whose results are properties.
Properties are also how passes communicate: one pass annotates, a later pass
reads. The tree is the blackboard.

### More precisely

A property is a **partial function** $Tree ⇀ P$, represented not as one table
but distributed: the graph of the function is scattered across the nodes it is
defined on.

The condition under which caching it is legitimate is exactly §1's uniqueness,
and it deserves stating as an obligation on the *caller* rather than a property
of the library:

> A value may be memoised on a tree only if it is a function of that tree
> alone.

Anything else — a value depending on the path taken to reach the node, on a
surrounding environment, on a mutable compiler flag — is not a function of the
tree, and storing it on the tree makes the second reader of that node get an
answer computed for someone else. This is the one way to use properties
incorrectly, and the library cannot detect it.

The interesting case is a function of *two* arguments, $f(a, b)$, which is
common in practice: evaluating a box in an environment, substituting in a
context. Such a function is not memoisable on $a$ alone. Either the pair
$(a, b)$ becomes the key — the compound-key approach — or the table nests,
$a ↦ (b ↦ P)$. §5's `property2` is the second, and the reason is measured
rather than aesthetic; see below.

The complexity statement is the payoff. For a fold with memoisation over a
hash-consed term:

```math
\text{cost} = O(\#\{\text{distinct subterms}\}) \quad\text{instead of}\quad O(\#\{\text{subterms}\})
```

— the cost becomes the size of the DAG rather than the size of the term it
denotes, and §2 showed the gap between the two is unbounded.

### In the code

The mechanism on the node is four short methods on `CTree`
([tree.hh:306-335](tlib/tree.hh#L306-L335)):

```cpp
typedef std::map<Tree, Tree> plist;   // both key and value are Trees
void setProperty(Tree key, Tree value);
Tree getProperty(Tree key);           // nullptr when absent
```

Everything is a tree, including the key — which is what makes the mechanism
untyped and universal. `plist` is allocated **lazily**
([tree.hh:161-168](tlib/tree.hh#L161-L168)): about 72% of nodes never receive a
property, so an always-present member would be paid for by three nodes out of
four for nothing. The comment there also records why it is a `std::map` rather
than a flat scanned buffer: one real Faust file has a single node carrying tens
of thousands of properties, and a linear scan made the whole compilation
quadratic.

`property<P>` ([property.hh:30](tlib/property.hh#L30)) is the typed façade over
that untyped mechanism, and the key line is its constructor:

```cpp
property() : fKey(tree(Node(unique("property_")))) {}
```

A fresh symbol per property object (§3's `gensym`), turned into a tree, used as
the key. There is also a named form, `property("some-name")`, for the rarer
case where two parts of a program must deliberately share one annotation.

For `P = Tree`, `int` and `double` there are specialisations
([property.hh:73](tlib/property.hh#L73) onwards) that store the value directly
in a node — the value *is* a tree, or fits in one. For any other `P`, the
generic template boxes the value in a `GarbageablePtr<P>` and stores the
pointer in a node, which costs an allocation and an indirection but works for
arbitrary C++ types.

Two refinements are where the engineering shows, and both are worth reading in
the source because both record what was measured.

**`fFastProperty`** ([tree.hh:297](tlib/tree.hh#L297)) is a single dedicated
slot on every `CTree`, bypassing the map entirely, for one caller-chosen hot
property — in Faust, `propagate`, about 20% of all property traffic measured
over `examples/*.dsp`. The warning attached to it matters: unlike
`setProperty`, this slot is **not namespaced by key**, so exactly one consumer
in the whole program may claim it, and two unrelated users of it would silently
overwrite each other.

**`property2`** ([property.hh:157](tlib/property.hh#L157) and its `Tree`
specialisation at [property.hh:251](tlib/property.hh#L251)) memoises the binary
functions described above, and its two long comment blocks are a rare thing in
a library: a written record of three designs that were tried and rejected on
measurement.

- The naive approach folds $b$ into a freshly hash-consed compound key on every
  call. Every distinct $b$ then mints both a new tree and a new property entry
  piled on the same $a$; one real case reached **56 000+ entries on a single
  node**.
- Nesting a container under $a$ instead fixes that, but the container has to
  reach `setProperty` wrapped in a brand-new `CTree` — 100+ bytes, never
  shareable, one per annotated node. Two attempts along this line regressed
  memory, worst on files where most nodes need several distinct $b$ (about 89%
  of boxes in `piano1.dsp`).
- Falling back to the compound key after the second $b$ fixed memory and
  regressed time: every access then paid a global hash-consing lookup on top of
  the local one.

The design that survived, for the case that is actually used, does the opposite
of everything this section has advocated: `property2<Tree>` keeps its table in
**its own** `std::unordered_map<Tree, Entry>`, keyed directly by the `a`
pointer, and never touches `CTree`'s property list at all. The first $(b,
value)$ pair lives inline in the entry; a second distinct $b$ promotes it to a
small nested map. That the library's most-used memo table abandoned the
per-node scheme is not an embarrassment — it is the honest answer to a
different access pattern, and the reasoning is preserved in the code precisely
so that nobody re-derives the three rejected designs.

One detail in that specialisation connects back to §2. Keying by raw pointer in
an `unordered_map` makes iteration order depend on addresses, which vary
between runs — exactly the non-determinism §2 warned about. The code notes why
it is harmless here: the map is only ever point-queried by $(a, b)$ and never
iterated, so its order cannot leak into generated output.

### Invariants and non-goals

**A memoised value must be a function of the tree alone.** The library cannot
check this, and violating it produces wrong results rather than crashes — the
worst kind. If a value depends on a context, the context belongs in the key
(`property2`), not in your assumptions.

**Properties never go stale within a session, and this is structural.** A tree
is immutable (§2), so the input to a memoised function cannot change under its
cached result. There is no invalidation protocol because there is nothing to
invalidate. What *can* go stale is a value depending on state outside the
trees — a compiler option, a target — which is a violation of the invariant
above, not a limitation of the mechanism.

**Everything is session state.** Properties die at `cleanup()` with the nodes
they annotate (§4). A `property` object that outlives a session must not be
reused across the boundary: its key tree belonged to the old session.

**Distinct `property` objects are independent; identically named ones are
not.** The default constructor guarantees isolation through a fresh symbol.
The named constructor deliberately gives that up, and two components using the
same name share one annotation, whether or not they intended to.

**`fFastProperty` has exactly one legitimate owner.** It is not keyed, so it
offers no protection at all against a second claimant.

**`property2<Tree>` is not part of a tree's property list.** It keeps its own
table, so `clearProperties()` on a node does not clear it, and a debugging pass
that dumps a node's properties will not show it.

**The generic `property<P>` does not free its boxed values before cleanup.**
`set` allocates a `GarbageablePtr<P>`; overwriting reuses it, but the storage
is only reclaimed at the end of the session, like everything else.

**None of this is thread-safe.** Properties are ordinary mutable state on
shared nodes, and §4's single-thread rule covers them.

*Code references verified at `9e26537`.*

### Origins

The idea of hanging computed facts on the nodes of a syntax tree, and of
defining a pass by what it computes at each construct rather than by how it
walks, is Donald Knuth's *Semantics of Context-Free Languages* (Mathematical
Systems Theory 2(2), 1968) — attribute grammars. A property in the sense of
this section is a **synthesized attribute**: a value computed from a node's
children and stored at the node. The inherited attributes of the same paper are
the case TLIB deliberately does not support, because a value flowing *down*
from a parent is not a function of the subtree alone, and so is exactly what
must not be memoised on it — see the invariant above. `property2` is the
pragmatic answer for the commonest such case.

The storage mechanism is older still and was met in §3: the property lists that
McCarthy's 1960 Lisp attached to atomic symbols. `setProperty`/`getProperty` is
that device, moved from symbols to trees.

For memoisation itself the reference remains Michie's 1968 memo functions,
cited in §1 — and it is worth noticing that Knuth's attribute grammars and
Michie's memo functions appeared in the same year, independently, as two views
of one idea: compute a value from a structure, once, and keep it.
