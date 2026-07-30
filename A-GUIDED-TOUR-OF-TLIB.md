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

Cross-references are written **§n**, counting the concepts in the order they
appear: §1 is *Signatures and algebras*, §2 *Hash-consing and maximal sharing*,
and so on to §12, *The stack, in one picture*. This section is not one of them.

The concepts are ordered by dependency: each one is introduced before the
sections that rest on it. The order is not a strict layering, though, and it
would be dishonest to pretend otherwise — the first concept needs a `Tree` to
talk about at all, and mentions symbols and hash-consing well before their own
chapters.

Such forward references are signposts. A term used before its own chapter is
defined on the spot in a **framed definition**, giving enough to follow the
current argument and naming the section that develops it properly. Nothing in
an argument depends on what such a box defers, so a reader can take the
thumbnail and move on, or jump ahead to resolve the term first. Footnotes are
reserved for genuine asides — an etymology, a citation.

---

## Signatures and algebras

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
themselves one of the algebras. Take the carrier `Tree`, and for `Add` supply
the function that *builds* the node `Add(x, y)` instead of adding numbers.
Folding a tree into that algebra rebuilds the same tree. That sounds useless,
and it is exactly the point: the tree representation is not a privileged,
special thing that all the other interpretations are computed *from*. It is one
interpretation among the others — the one that happens to throw nothing away.
That is what "syntax" means, made precise. And because it throws nothing away,
every other interpretation can be obtained from it, in exactly one way.

::: definition [Tree]
TLIB's one tree type: a pointer to a `CTree`, which holds a **node** and a
vector of child trees. The node carries either a value — an integer, a
floating-point number — or a symbol, which is how a constructor such as `Add`
is written. A leaf is a tree with no children; `Add(x, y)` is a tree whose node
is the symbol `Add` and whose two children are `x` and `y`. Developed in §3.
:::

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
  whatever the size of the signature — hence interned symbols carrying dense
  constructor opcodes (§3, §7);
- the interpretation of a term must depend **only on the term**, never on how
  or when it was built — hence hash-consing (§2), which makes structurally
  equal terms literally the same object;
- since the value depends only on the term, a shared subterm need be
  interpreted **only once** — hence properties (§5), which memoise a fold's
  results on the nodes themselves and bring a traversal back down to the size
  of the shared graph rather than of the term it denotes;
- and terms in a real compiler are recursive, which the definitions above do
  not cover at all — hence recursive terms (§8) and fixed points (§10).

Four of those words are used here before their own chapters:

::: definition [Interning, opcode, memoisation, recursive term]
**Interning** keeps one canonical object per distinct value in a table and
hands out pointers to it — as compilers do for identifiers, so that comparing
two names costs one pointer comparison (§3, and §2 where it is applied to whole
trees under the name hash-consing).

An **opcode** is a small integer identifying a constructor, so that a fold can
dispatch through a jump table instead of comparing names. What makes them
usable is that they are *dense* and *disjoint* — consecutive within a language,
never shared between two (§7).

**Memoisation**[^memo] caches a function's results so a repeated call with the
same argument returns the stored value. It is valid only for a function whose
result depends on its argument alone, which is why §1's uniqueness and §2's
pointer identity have to come first (§5).

A **recursive term** refers to itself, as in $x = 1 + x$: a finite piece of
syntax denoting an infinite tree, and every feedback loop in a Faust program is
one. It breaks the definitions of this section twice over — no base case for a
fold to stop at, and a value no longer determined by the signature alone but by
a choice of *fixed point* (§8 for the terms, §10 for their attributes).
:::

[^memo]: The term and the technique are Donald Michie's, *Memo functions and machine learning*, Nature 218, 1968.

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

A **homomorphism** $h : 𝒜 → ℬ$ between two $Σ$-algebras is a function
$h : A → B$ on their carriers that commutes with every operation:
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

[^cata]: From the Greek *κατά*, "downwards": a catamorphism collapses a structure into a value, following its shape. The name is Meijer, Fokkinga and Paterson's, *Bananas, Lenses, Envelopes and Barbed Wire*, FPCA 1991.

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
immutable for the whole session, and adding the same symbol to a second
signature fails without disturbing the first. Constructor identity is therefore
a property of the symbol, readable from any tree that uses it, at no cost per
tree.

**Signatures say nothing about meaning.** Nothing in TLIB relates a constructor
to an operation; that relation exists only inside a client's algebra, in the
body of its fold. The same term may be interpreted by any number of algebras,
and TLIB has no opinion about which one is "the" meaning.

::: definition [Session]
The interval between `tlib::init()` and `tlib::cleanup()` — for a compiler, one
compilation. Every symbol and every tree belongs to the session that built it,
nothing is reclaimed before it ends, and at `cleanup()` everything goes at
once. Pointers do not survive it. Developed in §4.
:::
**Signatures partition one global namespace, they do not create their own.**
Symbols are interned by name for the whole session (§3), and a symbol belongs
to at most one signature. Two languages therefore cannot both register a
constructor called `Add`: the first `add("Add")` claims that symbol, and the
second fails. This is why the example above names its constructors
`Arithmetic.Add`, `Arithmetic.Sub`, … — qualifying constructor names by their
language is the convention that keeps independent clients out of each other's
way. What signatures make disjoint is the *opcode space*, not the *name space*.

*Code references verified at `9432d5c`.*

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

## Hash-consing and maximal sharing

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

[^hashcons]: The name is Lisp's: `cons` builds a pair, and *hash-consing* is consing through a hash table. Goto's term for the resulting property was *monocopy*.

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
form whose sharing then does the work. That is exactly the strategy §8 uses
to make alpha-equivalent recursive terms be the same pointer.

::: definition [Canonical form, alpha-equivalence]
A **canonical form** is one chosen representative per equivalence class,
computed by a function mapping every member of a class to that same
representative. Its point here: once terms are canonicalised, the coarser
equivalence is decided by the structural equality of this section — by
comparing two pointers.

Two terms are **alpha-equivalent** when they differ only in the names of their
bound variables: $λx.x$ and $λy.y$ are the same function written twice, and
`rec(f, f+1)` and `rec(g, g+1)` the same recursion. Those names carry no
meaning, so a compiler treating them as different terms duplicates work and
misses sharing. Both are put to work in §8.
:::
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
/* … */
while (t && !t->equiv(n, ar, tbl)) {
    t = t->fNext;
}

if (t) { statsTreeReused();  return t; }      // the term already exists
else   { statsTreeCreated(); /* grow if needed, then allocate */ }
```

Everything else is detail around those seven lines. `CTree` itself is defined
at [tree.hh:138](tlib/tree.hh#L138) (forward-declared at
[tree.hh:107](tlib/tree.hh#L107)); the public constructors `tree(n)`,
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
([tree.hh:341](tlib/tree.hh#L341), declared at [117](tlib/tree.hh#L117)) so that `std::map<Tree, …>` iterates in a
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
Bruijn levels it has, whether it contains a recursive node —
computed once in the constructor and read in $O(1)$ ever after. They are the
degenerate case
of the memoisation idea: an attribute that is a function of the term, and that
TLIB knows about intrinsically, can simply live in the node. Attributes TLIB
does not know about need the general mechanism of §5.

::: definition [de Bruijn representation, aperture]
The **de Bruijn** representation removes the names of bound variables: a
variable is written as the number of binders standing between it and the one
that binds it, so $λx.λy.x$ becomes $λ.λ.2$. The payoff is exactly what §2 is
about — alpha-equivalent terms become *syntactically identical*, hence the same
hash-consed pointer, with no renaming pass. A term's **aperture** is how many
of its de Bruijn references still point outside it, which is what makes it
*open* or *closed*. Developed in §8.
:::
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

*Code references verified at `9432d5c`.*

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

## Nodes and symbols

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
of those costs a virtual call at least.

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
And because names can be *generated*, TLIB can mint fresh symbols on
demand, which is how §5 gives each property a private key and how §9 names the
variables it introduces.

::: definition [Generated symbol]
`unique("W")` returns a symbol named `W0`, `W1`, `W2`, … guaranteed not to
collide with any existing one. The need is as old as Lisp macros, where the
same operator is called `gensym`: a program that generates a binding must be
able to name it without capturing a name the user chose. TLIB uses it for the
fresh variables introduced by rewriting (§9) and — less obviously — to give
every `property` object a private key (§5).
:::
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
hashed ([symbol.cpp:136-138](tlib/symbol.cpp#L136-L138)). Names are normalised, so
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

*Code references verified at `9432d5c`.*

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

## The session memory model

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

`CTree`, `Symbol` and `Node` all derive from it — though only the first two
matter in practice, since a `Node` almost always lives *by value* inside a
`CTree` or a client's structure, and a subobject never passes through
`Garbageable::operator new`. The inheritance only takes effect for a `Node`
allocated on its own, which does not happen. `operator new`
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

The registry supports individual deletion mechanically — `operator delete`
removes the pointer from the list ([garbageable.cpp:95](tlib/garbageable.cpp#L95))
— but that is a property of the allocator, not a licence. **An interned tree or
symbol must never be deleted individually.** `CTree::~CTree`
([tree.cpp:277](tlib/tree.cpp#L277)) deliberately does not remove the node from
the construction table, so deleting one leaves a dangling entry that the next
lookup will dereference. The same holds for `Symbol` and its table. Individual
deletion is for ordinary `Garbageable` objects that no table points at — and
even for those it costs a `std::list::remove`, a linear scan of every live
object, so doing it in a loop makes the session quadratic.

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
address as the stored pointer. The outer object's storage is reclaimed either
way, but **anything a destructor owns is not** — `CTree::~CTree` is what deletes
a node's property map, and `Symbol` holds a `std::string`. For a process that
runs one session and exits this is invisible; for a host that compiles one
program after another it is a cumulative leak.

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
The one concession is diagnostic: the printer's context
([recursive-print.cpp:41](tlib/recursive-print.cpp#L41)) is `thread_local`, so
several threads may print concurrently to distinct streams *provided* the term
graph and its properties stay read-only throughout.

**`Garbageable` is not a general-purpose allocator.** It is a registry of
objects with a single common lifetime. Using it for objects that should die
early converts them into leaks-until-cleanup, and deleting them by hand costs
a linear scan.

**Never delete an interned tree or symbol.** The construction tables are not
updated by the destructors, so an individual delete leaves a dangling entry
that a later lookup will follow. Only `cleanup()` may end a tree's life.

**There is no reference counting, and a raw `Tree` needs no wrapper.** The
session model is the whole storage story: a raw pointer is valid for the whole
session, which is the longest anything lives. `P<T>`
([smartpointer.hh:22-26](tlib/smartpointer.hh#L22-L26)) looks like an owning
smart pointer and is not one — it is a null-checking wrapper with an empty
destructor, unused by the library itself but still live downstream, where
Faust's audio types are `Type = P<AudioType>`. Read it as a null-safety
convenience, never as ownership.

*Code references verified at `9432d5c`.*

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

## Properties

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
([tree.hh:296-325](tlib/tree.hh#L296-L325)):

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

**A fast-path slot that no longer exists** is worth one paragraph, because its
disappearance argues the chapter's thesis better than its presence did. `CTree`
used to carry a single dedicated field bypassing the map entirely, reserved for
one caller-chosen hot property — in Faust, the propagation memo, some 20% of
all property traffic when it was measured. That claimant then moved out to a
plain table keyed by ordinary C++ data, for a reason established by measurement
rather than taste: on large parallel structures the dominant cost was not the
map lookup the slot avoided but *building the hash-consed key*, a cons list of
hundreds of entries per call, paid on cache hits too. Once orphaned, the field
was deleted, and `sizeof(CTree)` fell from 120 bytes to 112 across every node
of every session. A memoisation mechanism is judged by the access pattern it
serves; when the pattern goes, so should the mechanism.

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
pointer, and never touches `CTree`'s property list at all. The first
$(b, value)$ pair lives inline in the entry; a second distinct $b$ promotes it to a
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

**There is no fast path.** Every property goes through the node's map. The
dedicated slot described above was removed once it had no claimant, so a
consumer that wants to beat the map's cost must do what the propagation memo
did: keep its own table, keyed by whatever is actually cheap for it.

**`property2<Tree>` is not part of a tree's property list.** It keeps its own
table, so `clearProperties()` on a node does not clear it, and a debugging pass
that dumps a node's properties will not show it.

**The generic `property<P>` does not free its boxed values before cleanup.**
`set` allocates a `GarbageablePtr<P>`; overwriting reuses it, but the storage
is only reclaimed at the end of the session, like everything else.

**None of this is thread-safe.** Properties are ordinary mutable state on
shared nodes, and §4's single-thread rule covers them.

*Code references verified at `9432d5c`.*

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

---

## Lists, sets and environments

### The idea

A compiler needs more than trees. It needs lists of arguments, sets of free
variables, environments mapping names to values. The reflex is to reach for
`std::vector`, `std::set`, `std::map`.

TLIB does something else, and the whole chapter follows from it: **these
structures are not new types, they are terms**. A list is a tree built from two
constructors, `cons` and `nil`, exactly as Lisp builds one:

```tree svg "the list (1, 2, 3) as an ordinary term"
Sym(cons)
  Int(1)
  Sym(cons)
    Int(2)
    Sym(cons)
      Int(3)
      Sym(nil)
```

Nothing in `list.cpp` allocates anything but trees. `cons(a, b)` *is*
`tree(gConsSym, a, b)`.

The payoff is that everything the previous chapters established applies to
lists without a line of extra code:

- **Two equal lists are the same pointer.** Comparing two argument lists of any
  length costs one instruction, and a list can be a property key, a set
  element, or a node of another tree.
- **Tails are shared automatically.** `cons(x, l)` allocates one node and
  reuses `l` — the classic persistent list, obtained here not by careful
  design but because hash-consing leaves no alternative. Two lists ending in
  the same suffix share that suffix even if they were built by unrelated
  passes hours apart.
- **Lists are immutable**, so an environment captured in a closure cannot be
  mutated behind its holder's back.

Sets and environments are then built on lists, and each adds exactly one idea.

A **set** is a list that is *ordered and duplicate-free*. That is a canonical
form in the sense of §2: every set of the same elements is the same list, hence
the same pointer, so set equality is again pointer equality and identical sets
computed by different analyses coalesce.

An **environment** is a stack of key-value pairs, searched from the top. Pushing
a binding does not modify the environment; it builds a new one that shares the
old as its tail. Lexical scoping and shadowing fall out of list structure, and
an inner scope costs one node.

### Its role in TLIB

This chapter is the demonstration that §1's claim of a *universal carrier* was
not rhetorical. The one tree type absorbs the auxiliary data structures of a
compiler, and they inherit sharing, constant-time equality, immutability,
memoisability and session lifetime — five properties that would each have to be
re-engineered for a `std::vector`.

It is also what lets the rest of the library stay small. `property` keys are
trees; environments passed to `property2` are trees; the free-variable sets an
analysis computes are trees, so they can themselves be memoised on the nodes
they describe. A set of symbols returned by a fold is a value in the same
universe as the term it came from, and §9's rewriting traverses environments
with the same machinery as terms.

The price is stated plainly in the non-goals: these are *functional* structures
with functional costs. `nth` is linear, `addElement` is linear, and a list used
where an array is wanted will disappoint.

### More precisely

Lists extend the signature of §1 with two constructors:

```math
Σ_{list} = \{\, \mathrm{nil}^{(0)},\; \mathrm{cons}^{(2)} \,\}
```

— and nothing more, so a list *is* a term of the universal carrier and every
statement of §2 to §5 applies to it unchanged.

Sets are the interesting case, because they are a **quotient**: the set
$\{a, b\}$ has many list representations, and TLIB picks one. Write $\prec$ for
the total order on trees. A list $[e_1, …, e_n]$ is the canonical form of a set
when

```math
e_1 ≺ e_2 ≺ … ≺ e_n
```

— strictly increasing, so ordered *and* duplicate-free in one condition. Every
set operation preserves that form, `list2set` establishes it, and the
consequence is the one §2 promised: because the representative is unique, and
because equal terms are one object, **set equality is pointer equality** —
inserting the same three elements in two different orders yields one object
([tour-examples.cpp:171](tour-examples.cpp#L171)).
Sorted representatives also make union, intersection and difference linear
merges rather than quadratic scans.

The order used is the one on serial numbers (§2), which is worth remembering
precisely: it is a total order, reproducible for a given construction history,
but *not* derived from values. Two sessions that build the same elements in a
different order will canonicalise the same set to the same *pointer* within
each session, but the element order — and so the printed representation — may
differ between them. `CanonicalTreeLess` exists for the cases where that is not
acceptable; sets do not use it.

An environment is a list of pairs, and lookup is the standard rule that makes
shadowing work:

```math
\mathrm{search}(k, \mathrm{push}(k', v, ρ)) =
\begin{cases}
v & \text{if } k = k' \\
\mathrm{search}(k, ρ) & \text{otherwise}
\end{cases}
```

— the topmost binding of a key hides every binding below it, and no binding is
ever removed or modified, only covered
([tour-examples.cpp:184](tour-examples.cpp#L184)).

### In the code

Everything is in [list.hh](tlib/list.hh) and [list.cpp](tlib/list.cpp), and the
constructors are as small as promised
([list.cpp:143](tlib/list.cpp#L143)):

```cpp
Tree cons(Tree a, Tree b) { ensureListSymbols(); return tree(gConsSym, a, b); }
```

`nil` is a single tree built from a `nil` symbol
([list.cpp:135](tlib/list.cpp#L135)), created on first use and — because it is
session state (§4) — reset by `tlibResetListInternals()` at `cleanup()`. The
predicates `isNil` and `isList` ([list.cpp:152](tlib/list.cpp#L152)) test the
node and the arity, which is the pattern every client fold uses.

`hd` and `tl` are `branch(0)` and `branch(1)`
([list.hh:140-148](tlib/list.hh#L140-L148)) — a list is *not* a distinguished
type, so accessing its head is accessing a branch.

The set operations ([list.cpp:325](tlib/list.cpp#L325) onwards) are where the
canonical form is maintained, and `addElement` shows the shape of all of them:

```cpp
Tree addElement(Tree e, Tree l)
{
    if (isList(l)) {
        if (e->serial() < hd(l)->serial()) return cons(e, l);       // insert here
        else if (e == hd(l))               return l;                // already present
        else                               return cons(hd(l), addElement(e, tl(l)));
    } else {
        return cons(e, nil());
    }
}
```

Three things are worth noticing. The comparison is on `serial()`, the total
order discussed above. The `e == hd(l)` test is a pointer comparison doing the
work of a deep structural equality, which is §2 paying off inside a data
structure. And the last branch rebuilds the prefix while sharing the tail —
the rebuilt prefix nodes are themselves hash-consed, so inserting into two
similar sets does not duplicate the shared parts.

`setUnion` ([list.cpp:387](tlib/list.cpp#L387)) is a merge of two sorted lists
that stops early on `isNil`, and — a small but real optimisation that only
sharing makes possible — returns the *other* list unchanged when one side is
empty, rather than copying it.

Environments are two functions, `pushEnv` and `searchEnv`
([list.cpp:443-448](tlib/list.cpp#L443-L448)), over the same cons cells.

Two utilities in the same file bridge to §9. `tmap`
([list.cpp:538](tlib/list.cpp#L538)) applies a function to every node of a tree
with a caller-supplied property key as its memo, and `substitute`
([list.cpp:609](tlib/list.cpp#L609)) replaces a variable by a value. Both are
the ancestors of the general rewriting machinery, and `substitute` is one of
the two functions whose per-call fresh keys produced the pathological node
carrying tens of thousands of properties that §5 mentioned.

*Code references verified at `9432d5c`.*

### Invariants and non-goals

**A list is a term, with all that follows.** Equal lists are one pointer,
lists can be property keys and set elements, and nothing can mutate one. There
is no separate list type to convert to or from.

**Sets are canonical only with respect to the serial order.** Within a session,
equal sets are the same pointer — that is the guarantee clients rely on. But
the order is derived from construction history, not from values, so the
*element order* of a set is not reproducible across processes that built their
elements differently. For orderings that must survive that, §2's
`CanonicalTreeLess` is the tool, and sets do not use it.

**Set operations assume their arguments are already canonical.** `setUnion` of
two arbitrary lists is meaningless; go through `list2set` first. Nothing checks
this.

**These are functional structures with functional costs.** `nth`, `len`,
`isElement`, `addElement` and `searchEnv` are all linear. An environment
searched in a hot loop is a linear scan, and the remedy is memoisation (§5),
not a different container.

**Recursion is by the C++ stack.** `addElement`, `setUnion` and their siblings
recurse over the list, so a set of a hundred thousand elements is a hundred
thousand frames deep. The structures are meant for the modest collections a
compiler manipulates — argument lists, free-variable sets — not for bulk data.

**An environment never forgets.** Shadowing covers a binding, it does not
remove it, so a deeply nested scope keeps every outer binding reachable and
alive. That is what makes environments cheap to copy and share; it also means
they only shrink by being discarded.

### Origins

This is Lisp again, and deliberately: the `cons`/`nil` pair, the shared tails,
the association list used as an environment are all in McCarthy's 1960 paper
(§3). What TLIB adds is that the cells are hash-consed, which turns two
familiar properties into stronger ones — structural equality becomes pointer
equality, and sharing becomes automatic rather than a consequence of how the
programmer happened to build the list.

The general principle behind lists, sets and environments here is
**persistence**: an operation produces a new version without destroying the
old, and versions share their common parts. Chris Okasaki's *Purely Functional
Data Structures* (Cambridge University Press, 1998) is the reference for
designing such structures and for reasoning about their costs — including the
honest accounting of which operations stay linear.

Representing a set as a sorted duplicate-free list, so that equality is
representation equality and union is a merge, is folklore; the observation that
matters here is Filliâtre and Conchon's (§2): once the representation is
canonical *and* hash-consed, structural equality of the underlying values comes
for free, and set equality collapses to a pointer comparison.

---

## Signatures and opcodes

### The idea

§1 wrote a fold and waved at one line of it:

```cpp
switch (tag.localOpcode()) {
    case 0: return algebra.Add(x, y);
    ...
}
```

This chapter is that line. The question it answers is narrow and entirely
practical: **given a node, how does a fold get to the right operation of the
algebra, in constant time?**

Consider the alternatives a compiler usually settles for. Comparing symbol
names is a string comparison per node per pass. Comparing interned symbol
pointers against a list of known constructors is better — one comparison each —
but still a *linear* scan: a language with 80 constructors averages 40
comparisons per node, on every node, in every pass. Testing `isSigInput(s)`,
then `isSigDelay(s)`, then `isSigBinOp(s)` in sequence, which is what large
compilers accumulate over time, is the same scan wearing a friendlier syntax.

What a `switch` needs instead is a **small dense integer**: 0, 1, 2, … with no
gaps, so the compiler emits a jump table and the dispatch is one indexed
branch regardless of how many constructors the language has.

So each constructor symbol is given a number. The difficulty is that TLIB
hosts *several* languages at once — Faust has signals, boxes, types — and their
numbers must not collide, while each language wants its own numbering to start
at 0 and stay dense.

The solution is the one operating systems use for address spaces. Each
**signature** reserves an aligned block of 256 consecutive numbers, and hands
its constructors positions 0, 1, 2, … inside it. A constructor's global opcode
is `base + local`; its local position is recovered by masking off the block,
which since blocks are aligned is one modulo:

```cpp
constexpr std::uint8_t localOpcode() const noexcept
{
    return static_cast<std::uint8_t>(opcode % kOpcodesPerSignature);
}
```

Two languages are then disjoint by construction, each has a dense 0-based
numbering for its `switch`, and a fold can check in one comparison that the
node it is looking at belongs to *its* language at all — which is the
verification §1 said had to happen somewhere.

The last piece is where these numbers live. Not on the tree: there are millions
of trees and adding a field to `CTree` costs megabytes. They live on the
**symbol**, which §3 established is unique per name and therefore the natural
home for anything true of a name. Every tree using that constructor reaches its
identity through the node it already holds, at no per-tree cost.

### Its role in TLIB

This is the mechanism that makes §1's architecture affordable rather than
merely elegant.

Without it, "one algebra per analysis" would still work, but each pass would
pay a linear identification cost per node, and the elegance would be bought
with a constant factor that grows as the language does. With it, adding a
constructor to a language costs nothing to the passes that already exist.

It also gives folds their only line of defence. A `Tree` is a universal object
(§3): nothing in its type says which language it belongs to. Comparing
`tag.signature` against the algebra's own identity is what turns "this is a
tree" into "this is a term of my language", and it costs one pointer
comparison. §1 concluded that conformance is discovered by the fold; §7 is what
makes that discovery cheap enough to do on every node.

Finally, it keeps TLIB out of its clients' business. The library knows that
constructors are grouped and numbered. It does not know what they mean, how
many arguments they take, or which language is which — those stay in the
client, as §1's non-goals promised.

### More precisely

A signature $S$ reserves an aligned range of $k = 256$ global opcodes:

```math
\mathrm{range}(S) = [\mathrm{base}(S),\; \mathrm{base}(S) + k - 1],
\qquad \mathrm{base}(S) \equiv 0 \pmod k
```

— a contiguous block of 256 numbers starting at a multiple of 256. Bases are
handed out by a session counter in order of first creation, so the $i$-th
signature created gets $\mathrm{base} = k \cdot i$, and distinct signatures have
disjoint ranges.

Within a signature, constructors receive dense local opcodes in order of first
addition, and the global opcode of a constructor $c$ is

```math
\mathrm{opcode}(c) = \mathrm{base}(S) + \mathrm{local}(c),
\qquad \mathrm{local}(c) \in [0, k-1]
```

— so recovering the local position is $\mathrm{opcode}(c) \bmod k$, and the
alignment is what makes that modulo a mask rather than a division, and what
lets it be computed without consulting any table.

Two properties follow, and they are the ones a fold relies on:

- **disjointness** — $\mathrm{signature}(c) = \mathrm{signature}(c')$ whenever
  $c$ and $c'$ have opcodes in the same range, so one comparison of signature
  identities decides membership;
- **density** — the local opcodes of a language with $n$ constructors are
  exactly $\{0, …, n-1\}$, which is what a jump table requires.

Both are checked in [tour-examples.cpp:194](tour-examples.cpp#L194), together
with the idempotence of `add` and the fact that an unregistered symbol is
simply unsigned rather than erroneous.

Note what is *not* claimed. The signature records which symbols are
constructors of which language; it does not record their arities, and $Σ$ in
the sense of §1 is therefore only half-represented — the vocabulary without the
arities. The arities live in the client's algebra interface, where the C++ type
system checks them, and are verified per occurrence by the fold.

### In the code

The public API is four declarations in
[symbol.hh:56-83](tlib/symbol.hh#L56-L83) and
[symbol.hh:167-251](tlib/symbol.hh#L167-L251): the constant
`kOpcodesPerSignature`, the `SymbolTag` a fold reads, the `Signature` handle,
and `getSymbolTag`.

Declaring a language is three lines per constructor:

```cpp
Signature arith = signature("Arithmetic");
Sym fAdd = arith.add("Arithmetic.Add");   // local opcode 0
Sym fSub = arith.add("Arithmetic.Sub");   // local opcode 1
```

`signature(name)` ([symbol.cpp:311](tlib/symbol.cpp#L311)) interns a symbol to
*identify* the signature — signatures live in the same namespace as everything
else (§3) — and, on first call only, reserves the next aligned block from
`gNextSignatureBase` ([symbol.cpp:55](tlib/symbol.cpp#L55)). Calling it again
with the same name returns a handle to the same block and the same allocation
state, so a language can be declared across several translation units.

`Signature::add` ([symbol.cpp:342](tlib/symbol.cpp#L342)) is the interesting
one, and its ordering is deliberate. Every failure is checked *before* anything
is written:

- capacity is tested before the name is interned, so a rejected 257th
  constructor does not leave a stray symbol in the table;
- a symbol already signed by *this* signature is returned unchanged, making
  `add` idempotent — declaring a language twice is harmless;
- a symbol signed by *another* signature is an error that changes nothing.

The two fields are assigned only after every validation has passed
([symbol.cpp:373-376](tlib/symbol.cpp#L373-L376)), so a failed `add` leaves
neither the signature nor the symbol table modified. That "commit last"
discipline is what makes the invariants below true even in the presence of
errors.

Reading the tag is deliberately trivial
([symbol.hh:241](tlib/symbol.hh#L241)):

```cpp
inline bool getSymbolTag(Sym sym, SymbolTag& tag)
{
    if (!sym) tlib::error("getSymbolTag: null symbol");
    if (!sym->fSignature) return false;       // an ordinary, unsigned symbol
    tag = {sym->fSignature, sym->fOpcode};
    return true;
}
```

Two field reads, inlined, on the hot path of every fold. Note the return value:
an *unsigned* symbol is not an error, it is simply not a constructor of any
language — most symbols in a session are ordinary.

The state itself is two fields on `Symbol` ([symbol.hh:114-115](tlib/symbol.hh#L114-L115))
and a session-local registry mapping signature identity to `{base,
nextLocalOpcode}` ([symbol.cpp:48-55](tlib/symbol.cpp#L48-L55)), cleared at
`cleanup()` like everything else. The executable version of the whole
mechanism, fold included, is `checkArithmeticSignatureFold()` in
[tests.cpp:255](tests.cpp#L255); the full specification is
[SIGNATURE-SPEC.md](SIGNATURE-SPEC.md).

*Code references verified at `9432d5c`.*

### Invariants and non-goals

**A symbol belongs to at most one signature, permanently.** The association and
the opcode are written once and never change for the rest of the session.
Constructor identity is therefore a property of the symbol, readable from any
tree that uses it.

**Ranges are disjoint; local opcodes are dense.** The first 256 distinct
constructors of a signature get exactly $0…255$; the 257th is rejected. A
language needing more than 256 constructors needs more than one signature, and
TLIB does not offer a way to join them.

**Signatures do not create a namespace.** They partition the *opcode* space,
not the *name* space, and all symbols share one global namespace (§3). Two
languages cannot both register `Add`; qualifying constructor names by their
language (`Arithmetic.Add`) is the convention that keeps clients out of each
other's way.

**Arity is not recorded.** `Signature` says nothing about how many arguments a
constructor takes; the fold checks it per occurrence, and the client's typed
interface is what makes that check systematic.

**A failed `add` changes nothing.** Errors are reported through the TLIB error
handler with no partial state — no half-registered symbol, no consumed opcode.

**The identity symbol is an ordinary symbol.** It can itself be a constructor
of some signature; the two roles are independent, and nothing prevents a
program from using the same `Sym` for both.

**Opcodes are session state.** Two sessions assign bases in creation order, so
a program that declares its languages in a different order gets different
global opcodes. Nothing should be persisted that depends on their numeric
values — only on their density and disjointness.

### Origins

The technique is the oldest one in language implementation: replace a name by a
small integer and dispatch through a table. It is what a bytecode interpreter
does with its opcodes, what a parser generator does with its token numbers, and
what the ADJ group's initial-algebra picture (§1) assumes when it treats a
signature as a finite indexed family — the index *is* the opcode.

The specific arrangement here, aligned blocks with a base and an offset so that
independent namespaces coexist in one flat numbering, is the same device as
segmented addressing and as the tagged pointers used in language runtimes:
reserve aligned regions so that a mask recovers the local part and a comparison
of bases decides membership. Its appeal is that both operations are single
instructions.

What is worth taking from this chapter is less the trick than the placement.
The identity lives on the interned symbol rather than on the term — §3's
observation that interning creates a natural home for anything true of a name,
applied once more, and the reason a constructor's identity costs nothing per
tree.

---

## Recursive terms

### The idea

Every Faust program has feedback in it. A one-pole filter, a delay line, a
reverb: the output of a signal depends on its own past. Written as an equation
that is

```text
x = 1 + x
```

and the compiler has to represent that. Here is the difficulty, and it is not
a detail of taste — it is an impossibility.

**Hash-consing builds bottom-up.** To construct `tree(n, a, b)` you must
already hold `a` and `b`, because the table is looked up by the *addresses* of
the children. A cyclic term has no bottom: to build the node for `x` you would
need the node for `1 + x`, which needs the node for `x`. There is no order in
which `tree()` can be called. Whatever else recursion is going to be, it cannot
be a cycle through branches.

TLIB offers two representations, and the interesting part is that it needs both.

**The de Bruijn form makes the cycle a binder and an index.** Instead of a
variable pointing back at its definition, a node marks *where the recursion
starts* and a reference says *how many binders up* to look:

```cpp
Tree r = rec( tree(symbol("+"), tree(1), ref(1)) );   // x = 1 + x
```

`rec` binds, `ref(1)` refers to the nearest enclosing binder. The term is
finite, acyclic, and an ordinary tree in every respect — so hash-consing works
on it unchanged. What makes this representation valuable is a second property
that comes free: **there are no names at all**. Two recursions that differ only
in the name of their variable are not merely equivalent, they are *the same
term*, hence the same pointer. Alpha-equivalence, which is usually a traversal,
becomes a pointer comparison.

**The symbolic form gives the variable a name**, because that is what a
compiler wants to read, print, schedule and generate code from:

```cpp
Tree x = tree(symbol("X"));
Tree r = rec(x, tree(symbol("+"), tree(1), ref(x)));   // X = 1 + X
```

And here is the trick that makes it possible at all, the one worth stopping on.
Look at how the two are built:

```cpp
Tree ref(Tree id)            { return tree(gSymRecSym, id); }
Tree rec(Tree id, Tree body) { Tree t = tree(gSymRecSym, id);
                               t->setProperty(recdefKey(), body); return t; }
```

They construct **the same node**. `rec(id, body)` *is* `ref(id)`, with the body
attached as a **property** (§5) rather than as a branch. The definition does not
participate in the node's identity, is not looked up by the hash-consing table,
and — crucially — does not have to exist when the node is built.

That is how the cycle is squared. The branches of a TLIB tree remain a finite
acyclic structure, exactly as §3's `Tree = N × Tree*` requires; the cycle lives
in the *property graph* layered on top. A traversal that follows branches
terminates naturally at a recursive node and simply does not see the loop; a
traversal that wants to enter the recursion asks for the property explicitly.
Recursion became visible only where it is wanted.

### Its role in TLIB

Recursion is what the library is ultimately for. A Faust program is a system of
mutually recursive signal equations, and every later stage — typing, interval
analysis, scheduling, code generation — is a computation over recursive terms.

Three things this chapter establishes are used everywhere downstream.

**The de Bruijn form is the canonical form.** §2 said that identifying terms up
to a coarser equivalence has to be arranged by *construction*, by building a
canonical representative and letting structural sharing do the work. This is
that strategy carried out: alpha-equivalent recursions converge on one term,
and everything already true of shared terms — pointer equality, memoisation,
one traversal per distinct subterm — becomes true modulo alpha-equivalence at
no extra cost.

**The symbolic form is canonical too, which is less obvious.** `deBruijn2Sym`
does not invent fresh names; it derives each variable's name from the
*canonical hash of the de Bruijn group it names*. Two alpha-equivalent
recursions therefore receive the same variable name, and their symbolic forms
collide in the hash-consing table — fusion for free. Converting the same term
twice returns the same pointer.

**Recursion is what breaks the fold.** §1's fold recurses into children and
stops at leaves; §5 memoises it. Neither survives contact with a term whose
meaning is an infinite unfolding: there is no base case, and the value of a
node can depend on itself. That is not a gap in this chapter, it is the
statement of the next two — §9 for transforming such terms, §10 for computing
attributes over them.

### More precisely

A recursive term denotes an infinite tree. Not an arbitrary one: an infinite
tree with **finitely many distinct subtrees**, which is called a *rational* or
*regular* tree. The finite syntax and the infinite denotation are related by
unfolding, written with the fixed-point binder the earlier chapters kept in
reserve:

```math
μx.\,(1 + x) \;=\; 1 + (1 + (1 + \cdots))
```

— the term on the left is finite and is what TLIB stores; the tree on the right
is what it means, and is never built. §3's carrier is untouched: the equation
$\mathrm{Tree} = N × \mathrm{Tree}^{*}$ still describes finite trees of finite
depth, and this chapter adds two ordinary constructors to the signature rather
than a new kind of object.

The de Bruijn representation replaces a bound variable by the number of binders
between the reference and its binder, so no names appear. The bookkeeping is
one synthesized attribute, the **aperture**, computed at construction
(§2) by three rules:

```math
a(\mathrm{ref}(k)) = k
\qquad
a(\mathrm{rec}(b)) = a(b) - 1
\qquad
a(c(t_1,…,t_n)) = \max_i a(t_i)
```

— a reference contributes its own level; a binder discharges one level; any
other node takes the deepest of its children. A term is **closed** when
$a(t) ≤ 0$, meaning every reference is matched by an enclosing binder, and
**open** otherwise. Because the attribute is stored on the node, the test is
$O(1)$ rather than a traversal.

The two representations are related by two conversions. Writing $\mathcal{D}$
for `deBruijn2Sym` and $\mathcal{S}$ for `sym2deBruijn`, the property that
matters is that $\mathcal{D}$ is a **function of the term's value**, not of its
history:

```math
t = t' \;\Longrightarrow\; \mathcal{D}(t) = \mathcal{D}(t')
\quad\text{(as pointers, both sides)}
```

— which holds because the name $\mathcal{D}$ gives to a variable is computed
from the canonical hash (§2) of the closed, name-free de Bruijn group it binds.
Equal groups get equal names, equal names give equal symbolic terms,
hash-consing gives one pointer. This is the precise sense in which
"alpha-equivalent recursive terms are the same pointer in both
representations", and it is checked, along with the identity of `rec` and
`ref`, in [tour-examples.cpp:234](tour-examples.cpp#L234).

One consequence of deriving a name from a hash deserves to be stated rather
than hidden: two structurally *different* groups whose 64-bit canonical hashes
collide would receive the same variable name. That is not a silent corruption —
the second group would attempt to define an already-defined variable with a
different body, which the protocol below makes a fatal error.

### In the code

Everything lives in [recursive-tree.cpp](tlib/recursive-tree.cpp), with the API
in [tree.hh:415-490](tlib/tree.hh#L415-L490).

The de Bruijn constructors are one line each
([recursive-tree.cpp:175-190](tlib/recursive-tree.cpp#L175-L190)): `rec(body)`
is `tree(gDebruijnSym, body)` and `ref(level)` is a node holding an integer
level, asserted positive. The symbolic pair
([recursive-tree.cpp:202-232](tlib/recursive-tree.cpp#L202-L232)) is where the
design shows, and the comment in `scanForRecs` states it flatly: *"rec and ref
are the same node in symbolic form"*.

That identity forces a protocol, because it punches a hole in §2's
immutability. Branches are immutable; **properties are not**. A symbolic
recursive node is hash-consed by its *name*, so calling `rec(id, body')` a
second time with a different body would silently change what every existing
holder of that pointer means. The rules
([tree.hh:428-445](tlib/tree.hh#L428-L445), enforced at
[recursive-tree.cpp:205-217](tlib/recursive-tree.cpp#L205-L217)) are therefore:

- `ref(id)` creates the node with no definition;
- the first `rec(id, body)` fills it;
- `rec(id, body)` again with the **same** body is an idempotent no-op — which
  is what lets a hash-consed reconstruction pass through unchanged;
- a **different** body is a redefinition, and erasing a definition is an
  erasure: both are fatal, with no override.

The consequence for every transformation in the library is spelled out in the
same comment: *a transformation never redefines, it maps every variable to a
fresh one*. §9's rewriting is built on that rule. And the error is raised
*before* the property is written, so a violation never corrupts the existing
definition — it stops the compilation instead. Making it fatal was not a
formality: the change immediately exposed two of TLIB's own tests, written
before the protocol, that redefined variables themselves.

`calcTreeAperture` ([recursive-tree.cpp:263](tlib/recursive-tree.cpp#L263))
implements the three rules above, and is called once per node from the `CTree`
constructor. Alongside it, `calcTreeContains`
([recursive-tree.cpp:315](tlib/recursive-tree.cpp#L315)) synthesizes the
`kContainsRec` bit — "a recursive node occurs here or below" — whose negation
`isRecFree()` is a genuinely useful shortcut: a term with no recursive node
reconstructs to itself, and a bottom-up fold over it reaches its final value in
a single pass and can never change during a fixpoint iteration (§10). The
comment there also records a soundness argument worth reading: the recursion
symbols are null before initialisation, so a tree built earlier gets the bit
clear — correct rather than racy, since building a recursive node requires
passing one of those symbols to `tree()`.

`deBruijn2Sym` ([recursive-tree.cpp:418](tlib/recursive-tree.cpp#L418))
requires a closed term and walks it with a memo. Its heart is `contentVar`
([recursive-tree.cpp:451](tlib/recursive-tree.cpp#L451)):

```cpp
snprintf(buf, sizeof(buf), "D%016zx", static_cast<size_t>(dbj->canonHash()));
return tree(symbol(buf));
```

The variable is *named after the content it binds*. A cached variant,
`deBruijn2SymCached`, stores the result as a property so a repeated conversion
of the same term costs a lookup.

`sym2deBruijn` ([recursive-tree.cpp:809](tlib/recursive-tree.cpp#L809)) is the
harder direction and the most engineered function in the library, because
mutual recursion has to be handled with a single-binder notation. It is
organised around the **strongly connected components** of the dependency graph
between recursive variables — Tarjan's algorithm, taken from the
[DirectedGraph](DirectedGraph/) library rather than hand-written. A recursive
node of the component being converted is inlined by extending the environment;
a node of any *other* component is converted separately into a closed term and
reused. Two memos rather than one make this affordable: closed results are
keyed by term alone, open ones by (term, environment), so environments stay
small and shared closed sub-DAGs are converted exactly once.

Two ways to test alpha-equivalence coexist, and the header is honest about
which to use ([tree.hh:486-491](tlib/tree.hh#L486-L491)): `areEquiv` converts
both sides and compares, which is the theorem but is super-linear on large
nests; `alphaEquiv` ([recursive-tree.cpp:902](tlib/recursive-tree.cpp#L902)) is
a pair-memoised walk carrying a variable bijection, linear in distinct pairs,
and is what validations should call.

Finally `canonicalizeRecNames` ([recursive-tree.cpp:961](tlib/recursive-tree.cpp#L961))
renames a term's recursive groups in dependency order as `R<instance>_<k>`. It
is *not* a canonical form and [tree.hh:541-557](tlib/tree.hh#L541-L557) says so
carefully: the instance prefix is fresh per call, so alpha-equivalent inputs
give alpha-equivalent — not pointer-equal — results. What *is*
instance-independent is the resulting **order**, because `fCanonKey` (§3)
strips the instance from those names. For a true canonical form, `deBruijn2Sym`
is the function to call.

*Code references verified at `9432d5c`.*

### Invariants and non-goals

**A recursive definition is immutable once given.** Redefining a symbolic
variable with a different body, or erasing its definition, is fatal. This is
not configurable, and the transition regime that once tolerated it is over.
Transformations allocate fresh variables instead.

**The cycle is in the properties, not in the branches.** A traversal that
follows `branch(i)` never loops, and never enters a recursive definition — it
stops at the recursive node. That is a feature (termination is free, §2's
finite carrier is preserved) and a trap: code that must see through the
recursion has to fetch the definition explicitly, and code that assumes
"visiting all branches visits the whole term" is wrong on recursive terms.

**`deBruijn2Sym` requires a closed term.** An open de Bruijn term — one with a
free reference — has no symbolic meaning, and the conversion asserts rather
than guessing.

**Canonicity is a property of `deBruijn2Sym`, not of every renaming.** Same
term in, same pointer out, because names are derived from content.
`canonicalizeRecNames` gives a canonical *order*, not a canonical *term*.
Confusing the two is the likeliest misreading of this chapter.

**Hash collisions are detected, not tolerated.** Content-derived names rest on
a 64-bit hash; a collision between structurally different groups surfaces as a
fatal redefinition, never as two different terms silently sharing a name.

**Aperture is a de Bruijn notion only.** Symbolic references count as zero, so
`isClosed` says nothing about whether a symbolic term's variables are all
defined. That is a different question, and the answer lives in the definition
properties.

**Nothing here decides what a recursive term *means*.** The signature gives the
syntax; the semantics of the fixed point — least, greatest, or an iteration
that must be made to converge — is the client's, and §10 is the machinery for
computing it.

### Origins

The nameless representation is Nicolaas Govert de Bruijn's, in *Lambda calculus
notation with nameless dummies, a tool for automatic formula manipulation, with
application to the Church-Rosser theorem* (Indagationes Mathematicae 34, 1972).
The paper's motivation is exactly the one this chapter gives: alpha-equivalence
makes syntactic identity useless for mechanical manipulation, and removing
names restores it. What TLIB adds is the observation that *syntactic identity
plus hash-consing is pointer identity*, so the benefit is not merely
conceptual — it is O(1) equality and automatic sharing of alpha-equivalent
recursions.

The objects being denoted are **rational trees**: infinite trees with finitely
many distinct subtrees. Bruno Courcelle's *Fundamental Properties of Infinite
Trees* (Theoretical Computer Science 25(2), 1983) is the reference for their
theory — unfolding, the equivalence of systems of equations with their
solutions, and why regularity is exactly the condition that makes finite
representation possible. They reached practice through Prolog II, where Alain
Colmerauer replaced unification's occurs-check with unification over rational
terms, precisely so that cyclic structures could be first-class.

The conversion out of the symbolic form uses Robert Tarjan's *Depth-First
Search and Linear Graph Algorithms* (SIAM Journal on Computing 1(2), 1972) to
find the mutually recursive groups. That a 1972 graph algorithm and a 1972
notation for binders meet inside one function is a fair summary of what this
chapter is: recursion is a graph problem wearing the clothes of syntax.

---

## Rewriting

### The idea

A fold (§1) turns a term into a value of some other domain. Very often what a
compiler wants instead is a term into *another term*: constant folding,
simplification, normalisation, substitution, lowering. That is a fold whose
target algebra is the syntax algebra itself — and §1 already told us what
happens then, since folding into the term algebra rebuilds the term. To *change*
something, you apply a local rule to each rebuilt node:

```cpp
Tree simplified = treeRewrite(t, [](Tree n) {
    Tree x, y;
    int  a, b;
    if (isTree(n, symbol("Add"), x, y) && isInt(x->node(), &a) && isInt(y->node(), &b))
        return tree(a + b);        // fold two constants
    return n;                      // no local change
});
```

The rule sees a node whose children have *already been rewritten*, and returns
either a replacement or the node itself. That is the whole interface.

What makes this worth a chapter is that the obvious implementation is wrong in
three separate ways on TLIB's trees, and each correction is instructive.

**Sharing.** A recursive walk that rebuilds every node visits a shared subterm
once per occurrence. §2 showed the gap can be exponential, so the traversal
must memoise — and here memoisation is not an optimisation but the difference
between a compiler that finishes and one that does not.

**Minimal reconstruction.** If a rule changes nothing in a subterm, the rebuilt
node should be *the same pointer*, not an equal copy. Hash-consing already
guarantees an equal copy would be the same pointer — but only if you rebuild
it, which costs a table lookup per node. Checking whether any child actually
changed avoids that, and lets an unchanged subtree be returned untouched.

**Recursion.** A rewrite must cross recursive definitions, which live in
properties (§8), not in branches. And it cannot simply rebuild a recursive node
in place: the definition of a symbolic variable is immutable, so writing a new
body under the same variable is the redefinition §8 makes fatal. The only
correct discipline is to allocate a **fresh variable** for every recursive
definition traversed — which means that rewriting with the identity rule
returns a term that is alpha-equivalent to its input, not equal to it.

### Its role in TLIB

Rewriting is the *write* half of the library, where the previous chapters were
mostly about reading. Most structural transformations a Faust compilation
performs — normalising signal expressions, substituting, lowering to a form the
code generator accepts — are one of the `treeRewrite` family with a different
rule.
In practice the workhorse is not the plain form but the *paired* one described
below, since most real transformations need to consult the annotations of the
original node while building from rewritten children.

Its architectural value is the same as the fold's: the traversal, the sharing,
the memoisation, the recursion discipline and the minimal reconstruction are
written once and correct once. A client writes a rule of a dozen lines, and
inherits behaviour on cyclic shared graphs that is genuinely difficult to get
right.

It is also where §5's warning comes due. A property attached to a node is a
fact about *that* node; a rewrite produces new nodes, which carry no
annotations. So a pass that consults types or intervals must run *before* the
rewrite, and anything the rewrite invalidates must be recomputed after it. The
specification states the rule as **rewrite, then re-annotate** — including the
fixed points of §10, which have to be re-run on the result.

### More precisely

The basic traversal is the **congruence closure** of a local rule: rewrite the
children, rebuild, apply the rule once to the rebuilt node. In inference-rule
form, as the header itself writes it:

```math
\dfrac{\text{rule} ⊢ t_i ⇒ u_i \quad (\text{for every } i)}
      {\text{rule} ⊢ f(t_1,…,t_n) ⇒ \text{rule}⟦\,f(u_1,…,u_n)\,⟧}
```

— to rewrite a node, rewrite each child, reassemble, and apply the rule to the
result. The memo makes the judgment $t ⇒ u$ computed once per *pointer*, which
turns a tree rewrite into a **DAG rewrite**.

For a recursive definition the rule is not applied at all; the traversal
alpha-renames:

```math
\dfrac{\text{body}[\mathrm{var} := \mathrm{var}'] ⇒ \text{body}'}
      {\mathrm{rec}(\mathrm{var}, \text{body}) ⇒ \mathrm{rec}(\mathrm{var}', \text{body}')}
```

— a fresh variable, a rewritten body, and the memo bound to the new reference
*before* descending, so that recursive occurrences encountered inside the body
already have a target. This is the step that makes rewriting a cyclic structure
terminate: the cycle is cut by an entry in the memo.

That fresh variable has a consequence which is easy to miss and expensive to
discover. **A rewrite is not a function on terms; it is a function only modulo
alpha-equivalence.** Run it twice on the same input and you get two results
that are alpha-equivalent but not the same pointer, because each run mints new
variables. In other words the transformation carries hidden state — the
generator of fresh names.

Now recall §5's condition for memoisation: a value may be cached against an
argument only if it is a function of that argument alone. A rewrite does not
satisfy it. What rescues the memo is that the hidden state is *fixed for the
duration of one call*: inside a single traversal each original maps to exactly
one result, so the pass is a function for as long as it runs, and the table is
consistent. The memo is born with the call and dies with it, and that is not an
implementation detail — it is the whole of what makes it well defined.

Compose two such passes by **nesting** them — invoking a memoised rewrite from
inside another rewrite's rule — and there is no consistent table to be had.
Share the memo, and the inner pass meets trees the outer pass has just
produced: fresh groups it has no entry for, which it dutifully renames again,
so two copies of one recursive group survive into the output. Give the passes
separate memos, and the inner one is re-entered from different points of the
outer traversal, minting different variables for the *same* original each time,
so the outer result becomes inconsistent with itself — which is worse. There is
no third option: *the cache of a function-modulo-alpha, keyed by syntactic
identity, is not a well-defined object*. The nesting is the error, not its
implementation.

::: warning [How rewriting passes may be composed]
**Rules compose freely** — a rule may call other rules, examine the node, build
whatever it likes, locally, within the algebra.

**Folds compose in a pipeline** — each runs to completion before the next
begins, its memo born and dying with it. Alpha-equivalence is a congruence, so
a sequence of passes is well defined even though each is only a function up to
renaming.

**A fold is never invoked from inside a rule.** Not with a shared memo, not
with a separate one.
:::

One nuance the rule does not forbid: a cache that survives *between* completed
invocations of the **same** pass is legitimate. It caches the function as fixed
at its first computation, which is a coherent object; what is incoherent is
state shared between two transformations that are both in flight.

The guarded variant exists because some rules have a premise that is a
**judgment about the source term** rather than a property of its shape — a
type, an interval, any annotation computed by a previous analysis:

```math
\dfrac{Γ ⊢ t : [k,k]}{t → k}
```

Such a premise cannot survive reconstruction: once the children have been
rewritten, the rebuilt node is a fresh tree carrying no judgment, and the rule
can never fire. It must therefore be tried *before* descending, on the original
node. Hence a second pair of rules, with a top-down guard `pre` and a bottom-up
rule `post`:

```math
\dfrac{\text{pre}⟦t⟧ = \mathrm{some}(c)}{t ⇒ c}
\qquad
\dfrac{\text{pre}⟦t⟧ = \mathrm{none} \quad t_i ⇒ u_i}
      {t ⇒ \text{post}⟦\,f(u_1,…,u_n)\,⟧}
```

— when the guard fires, the subtree is replaced wholesale and its children are
never visited; otherwise the ordinary congruence applies. The priority of the
guard over the descent is *part of the semantics*, not an optimisation: without
it the rewrite is lost, not merely delayed.

### In the code

[rewrite.hh](tlib/rewrite.hh) is a header-only file whose comments are, unusually,
a specification — the inference rules above are transcribed from it. The core is
`treeRewriteMemo` ([rewrite.hh:64](tlib/rewrite.hh#L64)), some forty lines that
handle all three difficulties.

The recursive case ([rewrite.hh:74-86](tlib/rewrite.hh#L74-L86)) is the one to
read closely:

```cpp
Tree newVar = tree(unique("W"));
memo[t]      = ref(newVar);                        // cut the cycle, before descending
Tree newBody = treeRewriteMemo(body, rule, memo);
return rec(newVar, newBody);
```

Three chapters converge in four lines. `unique("W")` is §3's gensym, giving the
fresh variable §8's immutability protocol demands. `memo[t] = ref(newVar)` is
what makes a cyclic traversal terminate — and it is *final* rather than
provisional, because §8 established that `ref(newVar)` and
`rec(newVar, newBody)` are the same hash-consed pointer. And the assertion just
above it catches a caller error the type system cannot: a symbolic reference
whose variable was never defined arrives here with a null body.

The ordinary case ([rewrite.hh:88-102](tlib/rewrite.hh#L88-L102)) implements
minimal reconstruction explicitly:

```cpp
for (int i = 0; i < ar; i++) {
    br[i]   = treeRewriteMemo(t->branch(i), rule, memo);
    changed = changed || (br[i] != t->branch(i));
}
if (changed) { r = tree(t->node(), br); }
```

`changed` is a pointer comparison, which is §2 being spent well: detecting that
a rewritten child is identical to the original costs one instruction, and
saves a hash-consing lookup for every node of every unchanged subtree.

Note also what the traversal does *not* do: it attaches no property to the
trees it visits. The memo is local to the call
([rewrite.hh:105-108](tlib/rewrite.hh#L105-L108)), which is a deliberate reversal of §5's
usual advice — and the reason is the pathology §5 reported. A rewrite keyed by
a fresh property per call is exactly how one real Faust node came to carry tens
of thousands of properties; a local `unordered_map` that dies with the call
does the same job without polluting the nodes.

Two further families sit on the same traversal. The **guarded** variant
([rewrite.hh:315](tlib/rewrite.hh#L315)) takes `pre` and `post` as above, with
the plain form documented as exactly the guarded form whose guard always
returns `nullopt` — a pleasant way to specify one function in terms of another.
The **paired** family, `treeRewritePaired`
([rewrite.hh:191](tlib/rewrite.hh#L191)), passes the rule *both* trees —
`rule(original, rebuilt)` — so a transformation can consult annotations carried
by the original while building from rewritten children, and exposes its memo so
that nested arguments can be matched with their transforms. The full
specification of both is [REWRITE-SPEC.md](REWRITE-SPEC.md).

*Code references verified at `9432d5c`.*

### Invariants and non-goals

**Rewriting a recursive term renames it.** Under the identity rule the result
is alpha-equivalent to the input, not pointer-equal — `areEquiv`, not `==`.
This surprises everyone once, so it is pinned by a test
([tour-examples.cpp:317](tour-examples.cpp#L317)), next to the non-recursive
case where the identity rule does return the very same pointer. It is forced by §8: reusing the variable would be
a redefinition, and the in-place variant that once did so was removed for
exactly that reason.

**The rule is never applied to recursive nodes.** `treeRewrite` traverses a
definition through its body and handles the binder itself, so a rule that
expects to see `rec` nodes will never fire on one.

**Annotations do not survive a rewrite.** New nodes carry no properties, and
the judgments a guard consulted are stale for the result. Rewrite, then
re-annotate — including re-running any fixed point of §10. Nothing enforces
this ordering.

**The guard has priority over the descent, by definition.** When `pre` fires,
children are never visited and `post` is not applied to the replacement.
`pre(t)` returning `t` itself is how one says "keep this subtree verbatim, do
not enter it".

**One rule application per rebuilt node, not to fixpoint.** `treeRewrite`
applies the rule exactly once per node; it does not re-apply until nothing
changes. A rule that produces a redex its own pass would rewrite must either
handle that itself or be run again by the caller.

**The memo is per call, and must be.** Two rewrites with the same rule share
nothing, and a rewrite performed twice does the work twice. Memoising *across*
completed calls of the same pass is legitimate and is the caller's business
(§5, with the property-count pathology in mind). Sharing a memo between passes
that are both running is not.

**Never invoke a rewrite from inside a rule.** This is the sharpest rule in the
chapter, and the one that cost the most to learn. The failure is silent and
arrives late: on one real program a nested pass left 15 recursive groups where
13 were correct, duplicating a 2048-sample delay line and costing a third of
the generated code's runtime. No cache discipline avoids it — see *How
rewriting passes may be composed* above. If a rule needs another
transformation, apply that transformation's **rules** locally, without its
driver, or run it as a separate pass.

**This became an error only when recursive definitions became immutable.** The
older design tolerated nesting because its memo *reused* the variables of the
term it rewrote, which made the transformation syntactically deterministic — by
means of exactly the redefinition that §8's protocol now makes fatal. Closing
that hole made the library more correct and exposed an unsoundness that had
been latent behind it, which is a fair description of how most of this chapter
was learned.

**Termination is guaranteed for the traversal, not for the rules.** The
traversal always terminates, even on cyclic terms, because of the memo. A rule
that rewrites a node into something containing a fresh redex can still diverge
if the caller iterates it.

### Origins

The framework is **term rewriting**, whose standard reference is Franz Baader
and Tobias Nipkow's *Term Rewriting and All That* (Cambridge University Press,
1998): rules, congruence closure, confluence and termination. TLIB implements
one specific strategy — innermost, one pass, one application per node — and
deliberately provides none of the theory's machinery for reaching a normal
form, leaving that to the caller.

Rewriting a *shared graph* rather than a tree is the older subject of **term
graph rewriting**, surveyed in Barendregt et al., *Term Graph Rewriting*
(PARLE, 1987). The distinction matters exactly as this chapter describes it:
rewriting a shared node once serves every one of its occurrences, and the memo
here is what turns the tree semantics into the graph one. Note that TLIB never
rewrites in place: it rebuilds immutably, and the result is shared because
hash-consing shares it.

For the guarded variant the ancestry is different: a rule with a premise
discharged by a prior judgment is a **conditional rewrite rule**, and the
observation that such a premise must be checked before the congruence descent
— because reconstruction destroys the evidence — is the practical form of the
well-known awkwardness of type-directed transformations. The library's answer,
that priority is part of the semantics rather than a scheduling choice, is
stated in the specification rather than left to be discovered.

---

## Fixed points

### The idea

§1 promised that every analysis is a fold. §8 broke the promise: a recursive
term has no base case, and the value of a node can depend on its own value.
Ask a fold for the type of `x = 1 + x` and it recurses forever.

The classical answer is to stop asking for *the* value and start computing
**successive approximations**. Guess something for `x`, evaluate the body with
that guess, and use the result as the next guess:

```text
x₀ = ⊥            (nothing known yet)
x₁ = 1 + x₀
x₂ = 1 + x₁
…
```

If the guesses stop changing, the last one is a **fixed point** — a value that
the equation maps to itself, and therefore a consistent answer for the
recursion. The whole chapter is about making that idea terminate in a compiler.

Three difficulties stand in the way, and TLIB's iterator answers each.

**It may not converge at all.** For an interval analysis on `x = x + 1` the
approximations grow forever: $[0,0], [0,1], [0,2], …$. The cure is
**widening**: after a few honest iterations, when a value keeps growing, jump
deliberately to something bigger — often $[0, +∞)$ — so the sequence stabilises.
It is a controlled loss of precision, exchanged for termination.

**Widening overshoots.** Having jumped to $[0, +∞)$ you may be able to come back
part of the way: re-evaluating from a stable point sometimes yields something
tighter that is still consistent. That descending pass is **narrowing**, and it
is bounded, because it is a recovery of precision and not a correctness
requirement.

**Some answers are better guessed than derived.** For certain domains a good
candidate is known in advance — *this filter's output is non-negative* — and
checking a guess is far cheaper than deriving it. The iterator therefore offers
a third regime, a **descending probe**: seed the whole recursive group with a
candidate, take one step, and if the result is no larger than the seed, the
seed was a valid answer.

Around all this sits one structural decision. A program's recursive variables
form a dependency graph, and its **strongly connected components** are the
groups that must be solved together (§8's Tarjan machinery again). Components
are solved in dependency order, so by the time a group is iterated, everything
it depends on is already settled — and only genuinely mutual recursion pays the
cost of iteration.

### Its role in TLIB

This is where TLIB stops being a data structure library and becomes a compiler
substrate. Faust's type inference, its interval analysis, its vectorisability
and computability judgments are all attributes over recursive signal terms, and
all of them are this iterator with a different domain.

The division of labour is the same one §1 set up, extended to the recursive
case. The **iterator** knows about terms: it walks lists, `rec`, `ref` and
`proj`, finds the components, runs the ascending and descending regimes, and
memoises. The **domain** knows about values: what `⊥` and `⊤` are, how to
compare them, and how to combine a constructor with its children's values. The
header says it plainly — the iterator is *temporal-blind*, it never takes a
union of values; a delay's temporal union lives in the domain's own rule.

So a new analysis over recursive terms costs one class implementing
`FixPointDomain<V>` — exactly as a new analysis over finite terms cost one
algebra in §1. That is the chapter's real content: the fold survives recursion,
at the price of a lattice and an iteration strategy.

### More precisely

Let $V$ be the attribute domain, ordered by $⊑$ — read $x ⊑ y$ as "$x$ is at
least as precise as $y$", with $⊥$ the least element and $⊤$ the greatest. A
recursive group of $n$ variables induces a function

```math
F : V^n → V^n
```

— evaluate each variable's body under an assignment of values to all the
variables of the group, and collect the results. A **fixed point** is an
assignment with $F(X) = X$; a **post-fixed point** is one with $F(X) ⊑ X$,
which is the weaker and more useful notion, since any post-fixed point is a
sound over-approximation.

Everything that follows rests on one hypothesis that the domain owes and the
library cannot check: $F$ must be **monotone**, $X ⊑ Y ⟹ F(X) ⊑ F(Y)$. Since
$F$ is built by evaluating bodies with `combine`, this amounts to requiring
`combine` to be monotone in its children's values. Without it the ascending
sequence below need not be increasing, Kleene iteration has no reason to
converge to anything meaningful, and the narrowing argument — that applying $F$
to a post-fixed point yields another post-fixed point — simply fails.

The ascending regime is **Kleene iteration**: start at $⊥$ and apply $F$ until
nothing moves.

```math
X_0 = ⊥^n, \qquad X_{k+1} = F(X_k)
```

— which converges when the domain has no infinite ascending chains, and does
not otherwise. Intervals over the integers have such chains, which is exactly
the case that needs help.

**Widening** replaces the update by an operator $∇$ that must satisfy two
conditions: $x ⊑ x ∇ y$ and $y ⊑ x ∇ y$ (it over-approximates both arguments),
and any sequence built with it stabilises after finitely many steps. Applied
after a threshold, it turns a divergent ascent into a terminating one at the
cost of precision.

**Narrowing** then iterates $F$ *without* widening from the post-fixed point
reached. Each step of $F$ applied to a post-fixed point is again a post-fixed
point, so every intermediate result stays sound and one may stop at any time —
which is why the number of narrowing steps is a tunable and not a correctness
parameter.

The **probe** is the same idea used as a certificate rather than a computation.
Given a candidate $P$ for the whole component, if

```math
F(P) ⊑ P
```

then $P$ is a post-fixed point and therefore sound. One application of $F$
decides it. Note the quantifier: the certificate is required on the *whole
product*, every branch of every variable of the component, not branch by
branch — a component either certifies or it does not.

Finally, the iterator computes over **components in dependency order**. Within
one component it uses a *Jacobi* update: freeze the current assignment, compute
every branch against that frozen snapshot, then swap. Updating in place
(Gauss-Seidel) would converge at least as fast, but the result could depend on
the order the variables happen to be visited in; freezing makes each round a
function of the previous round alone.

### In the code

[fixpoint.hh](tlib/fixpoint.hh) is header-only and organised around two
interfaces the client implements or receives.

`FixPointDomain<V>` ([fixpoint.hh:66](tlib/fixpoint.hh#L66)) is the lattice.
Its defaults are worth reading as a design statement: they define an **exact**
domain — converge by equality, never widen, no cap, no narrowing, no probe — so
a domain with no infinite chains implements four methods (`bottom`, `top`,
`combine`, `lessEqual`) and nothing else. Approximation is opt-in, added by
overriding `widenAfter()`, `widen()` and optionally `probeSeeds()`.

`combine` is declared `const`, and the comment explains why in terms this tour
has been using since §1: *an algebra is a DENOTATION, not a process* — a node's
value depends on its constructor and its children's values, on nothing else.
State that is genuinely needed is declared `mutable`, which says precisely that
it is not part of the denotation. That is §5's memoisation invariant, restated
as a C++ signature.

`FixPointEvaluator<V>` ([fixpoint.hh:56](tlib/fixpoint.hh#L56)) is the handle
the iterator passes *into* `combine`, letting a constructor ask for the value of
any subtree rather than only receiving its direct branches. The motivation is
concrete: a Faust slider keeps its four range signals in a nested list, so its
node has two branches while the operation it denotes takes five arguments.
Asking costs nothing extra, since values are memoised either way.

The solver itself ([fixpoint.hh:286-311](tlib/fixpoint.hh#L286-L311)) is the
two-phase regime, readable almost as pseudocode:

```cpp
do {                                                  // Phase 1 : ascending Kleene
    ++iteration;
    const bool applyWiden = iteration > fDomain.widenAfter();
    done = jacobiStep(members, approx, applyWiden);
} while (!done && iteration < fDomain.maxIterations());

if (!done) {                                          // guard-rail : the only sound
    ... row[b] = fDomain.top(proj(b, x));             // fallback is top
} else if (fDomain.widenAfter() < INT_MAX) {
    while (!ndone && narrow < fDomain.maxNarrowingIterations()) {
        ndone = jacobiStep(members, approx, /*applyWiden*/ false);   // Phase 2
    }
}
```

Two details in it are worth the reader's attention. The iteration cap is a
**guard-rail, not a strategy**: if the ascent has not converged when the cap is
reached, the only sound thing to report is `top`, and that is what happens —
losing all precision rather than reporting an unsound value. And the narrowing
phase only runs for domains that widen, since an exact domain has nothing to
recover.

`jacobiStep` ([fixpoint.hh:402](tlib/fixpoint.hh#L402)) is the frozen-snapshot
round described above; the comment `fCurrentApprox = approx; // frozen : eval
reads only this during the round` is the whole of the Jacobi discipline.

`probeComponent` ([fixpoint.hh:325](tlib/fixpoint.hh#L325)) implements the
third regime, with one refinement over the sketch above: a domain supplies an
*ordered list* of seed candidates, tried until one certifies, so an analysis can
fall back from a strong certificate to a weaker one — the interval domain tries
positivity $[0, \mathrm{BIG}]$ first, then the symmetric
$[-\mathrm{BIG}, \mathrm{BIG}]$ that a contracting signed loop such as a plucked
string still satisfies. The probe *informs*, it never writes the settled values.

Underpinning all of it is the memo, and its three classes are where §8's
synthesized bits pay off. A subterm free of recursive nodes — `isRecFree()`,
one bit read — reaches its final value in one pass and is memoised **for
good**. A value belonging to an already-settled component is likewise
permanent. Only values that depend on the component being iterated are
*moving*, and only those are discarded between rounds. The plan itself comes
from `RecPlan` ([tree.hh:515](tlib/tree.hh#L515)), memoised one per root per
session, so repeated analyses of the same term share one Tarjan run.

*Code references verified at `9432d5c`.*

### Invariants and non-goals

**The iterator computes a post-fixed point, not necessarily the least one.**
With widening it is deliberately not the least. Soundness means the answer
over-approximates the true value; precision is a separate, best-effort concern.

**The domain owes the iterator a real lattice, and a monotone `combine`.**
`lessEqual` must be a partial order and `bottom`/`top` its bounds; `widen` must
over-approximate both arguments and must stabilise every chain; and `combine`
must be **monotone** in its children's values, since that is what makes the
induced $F$ monotone. Nothing checks any of it, and the failure is not
uniform across the phases: during the ascent a non-monotone `combine` would
show up as oscillation, which the iteration cap catches and answers with
`top` — imprecise but sound. It is the **narrowing** phase that rests nakedly
on monotonicity, since "each step from a post-fixed point stays a post-fixed
point" is exactly the property that fails without it. Monotone by
construction, unchecked by machine, and narrowing is where a violation would
turn silent. A `widen` that merely returns the fresh value, which is the
default, makes the iterator non-terminating on a domain with infinite chains
rather than incorrect.

**Reaching the iteration cap is a precision failure, not an error.** The result
is `top` for every branch of the component: sound, useless, and silent. A
domain that cares should make its cap generous or track how often it is hit.

**The iterator never unions values.** It is temporal-blind: it walks the term
structure and delegates every value decision to `combine`. An analysis whose
recursion needs a union at a delay must put it in its own delay rule.

**Values are memoised in three classes, and only one is invalidated.** A value
below a recursive node that is currently moving is recomputed each round;
everything else is permanent for the session. A domain whose `combine` is *not*
a pure function of node and children's values breaks this silently — the same
invariant as §5, now with iteration to amplify it.

**Fixed points are computed over the symbolic form.** The iterator walks `rec`,
`ref` and `proj`, so the input is a symbolic recursive term (§8), not a de
Bruijn one.

**Nothing survives a rewrite.** §9's rule applies here too: a rewritten term is
made of new nodes, so its attributes must be recomputed — including re-running
this fixed point.

### Origins

The ascending regime is **Kleene iteration**, from the fixed-point theorem of
Knaster and Tarski — Alfred Tarski's *A lattice-theoretical fixpoint theorem and
its applications* (Pacific Journal of Mathematics 5(2), 1955) is the standard
citation, and it is the same result that gives §8's recursive terms their
meaning as least fixed points.

Widening and narrowing are Patrick and Radhia Cousot's, introduced with
**abstract interpretation** in *Abstract interpretation: a unified lattice model
for static analysis of programs by construction or approximation of fixpoints*
(POPL, 1977), and developed in *Comparing the Galois connection and
widening/narrowing approaches to abstract interpretation* (PLILP, 1992). The
framing this chapter uses — a sound over-approximation obtained by iterating in
an abstract domain, with widening to force termination and narrowing to recover
precision — is theirs entirely. Faust's interval analysis is an abstract
interpretation in the strict sense of that paper.

The component-wise organisation is folklore in dataflow analysis and rests
again on Tarjan (§8): solving strongly connected components in dependency order
is what keeps iteration confined to genuinely mutual recursion. The choice of a
Jacobi rather than Gauss-Seidel update trades speed for order-independence, a
trade a compiler that must be deterministic (§2) has good reason to make.

---

## Optional modules

### The idea

Two small modules ship with TLIB and are used by Faust, but nothing in the core
depends on them: remove either and the library still builds. They are worth a
short chapter for a reason that has nothing to do with their size — **they are
the proof that the preceding chapters are enough**. Both are written entirely
in terms of trees, lists, sets and properties, with no new mechanism, no new
node kind and no privileged access.

**`dcond`** represents boolean conditions in disjunctive or conjunctive normal
form. A DNF condition is a *set of sets* of trees: the inner sets are
conjunctions of atoms, the outer set their disjunction. Since §6's sets are
ordered and duplicate-free and §2 makes equal terms one pointer, two conditions
with the same clauses are the same pointer — so comparing conditions becomes a
set operation rather than a proof search.

**`occur`** counts, for every subtree of a given root, how many times it occurs.
That is the natural question to ask of a DAG before generating code: a subterm
used once can be inlined, a subterm used many times deserves a name and a
temporary variable. The counting is a traversal that increments a property per
node.

### Its role in TLIB

Their role is deliberately marginal, and saying so is the point.

`dcond` is, in the vocabulary of §1, **another algebra** — a boolean one, whose
carrier happens to be `Tree`. It illustrates that the universal carrier is not
limited to syntax: normal forms of logical formulae live in the same space as
signal terms, share the same table, and can be memoised on nodes with the same
`property`.

`occur` is an application of §5 with one twist worth copying. Occurrence counts
are meaningless without a root — the same subtree occurs a different number of
times in different terms — so the count cannot simply be *the* count of a node.
`Occur` therefore mints a **fresh property key per root**, which is §3's gensym
used to parameterise an annotation. The pattern generalises: whenever a fact is
a function of a node *and* something else, either the key or the table has to
carry that something else, exactly as §5's `property2` does for a second tree.

Neither module is on the path of any other chapter. They are here because a
library that claims its core is sufficient should be able to point at things
built on top of it without extending it.

### More precisely

A DNF condition is a disjunction of conjunctions of atoms, stored as a set of
sets:

```math
c = \bigvee_{i} \Big( \bigwedge_{j} a_{ij} \Big)
```

— the inner sets are clauses, the outer set their disjunction. Because §6's
sets are ordered and duplicate-free, two conditions with the same clauses are
one pointer, and the operations become set manipulations: conjunction pairs
clauses, disjunction unions clause sets.

Ordering is where care is needed, because the argument order of the predicate
is easy to read backwards. The implementation is
$\mathrm{dnfLess}(c_1, c_2) \iff c_1 ∨ c_2 = c_1$, and in a lattice where $∨$ is
the join, $c_1 ∨ c_2 = c_1$ says $c_2 ⊑ c_1$. So:

```math
\mathrm{dnfLess}(c_1, c_2) \iff c_2 ⟹ c_1
```

— it holds when the **second** argument is the stronger condition. The test
suite pins exactly that, checking `dnfLess(a, a ∧ b)`
([tests.cpp:1100](tests.cpp#L1100)): $a ∧ b$ implies $a$. The header comment
([dcond.hh:37](tlib/dcond.hh#L37)) stated the converse until recently, and
this chapter reproduced the error faithfully; the test is what settled it.

For occurrences, the count is a function of *two* arguments — a subtree and the
root it is counted in:

```math
\mathrm{count}_{r}(t) = \#\{\, \text{positions } p \text{ in } r : r|_p = t \,\}
```

— the number of positions of $r$ at which $t$ appears. Note that this is a count
over the *unfolded term*, not over the DAG: a subterm shared by two parents
occurs twice, which is exactly what a code generator needs to know.

### In the code

`dcond` is eight declarations ([dcond.hh:34-42](tlib/dcond.hh#L34-L42)):
`dnfCond`, `dnfAnd`, `dnfOr`, `dnfLess` and the four `cnf` counterparts. The
implementation ([dcond.cpp](tlib/dcond.cpp)) is set manipulation over §6's
sets, and it is the least finished corner of the library — the header asks for
memoisation that is not there (*"WARNING : Memoization probably needed
here !!!!"*), `dnfAnd` carries an *"A REVOIR !!!"*
([dcond.cpp:192](tlib/dcond.cpp#L192)), and the test suite covers idempotence,
commutativity and one ordering example rather than an algebraic
specification.

`occur` is one small class ([occur.hh:33](tlib/occur.hh#L33)):

```cpp
class Occur : public Garbageable {
    Tree fKey;                        // a fresh property key, specific to this root
   public:
    Occur(Tree root);                 // count the occurrences of each subtree of root
    int getCount(Tree t);
};
```

The constructor ([occur.cpp:36-38](tlib/occur.cpp#L36-L38)) builds the key,
walks the tree incrementing a count per node, and then resets the root's own
count to zero — the root does not occur inside itself. `specificKey`
([occur.cpp:61-67](tlib/occur.cpp#L61-L67)) is where the per-root key is minted
with `unique`, and `countOccurrences`
([occur.cpp:72-77](tlib/occur.cpp#L72-L77)) is the three-line traversal.

*Code references verified at `9432d5c`.*

### Invariants and non-goals

**Neither module is required.** Nothing in `tree`, `node`, `symbol`, `list`,
`property`, `recursive-tree`, `rewrite` or `fixpoint` refers to them.

**`dcond` assumes its inputs are in normal form**, as `setUnion` assumes
canonical sets (§6). It also does not memoise, which its own header admits.

**`dcond`'s constants are not specified.** `nil` serves as a special case in
the operations, but which formula it denotes — the empty disjunction, or truth
— is nowhere written down, and the DNF of *true* would conventionally be the
set containing the empty clause rather than the empty set. Anyone relying on
the boundary cases should pin them down first. This chapter describes the
module as it is, not as a specified algebra.

**Occurrence counts are per root, and per session.** A count read with one
`Occur`'s key is meaningless for another root. The counts are properties, so
they die at `cleanup()` like everything else (§4).

**`occur` counts the unfolded term, not the DAG.** That is the point — but it
means the count of a heavily shared subterm can be exponentially larger than
the number of nodes, and the traversal that computes it visits every position
unless the caller has arranged otherwise.

**`occur` does not cross recursive definitions.** It follows branches, and §8
established that definitions live in properties, so occurrences inside a
recursive body are not counted from outside it.

### Origins

Normal forms for boolean expressions are as old as the subject; the specific
observation that matters here is the one §2's origins already made about BDDs —
a canonical representation plus maximal sharing turns logical equivalence into
pointer equality. `dcond` uses the weaker, simpler device of DNF over canonical
sets, which is adequate when the formulae are small and the operations rare.

Counting occurrences to decide what deserves a name is **common subexpression
elimination** seen from the code generator's side, and takes us back to Ershov
(1958), cited in §2: the same hash table that finds a repeated subexpression is
what makes counting its uses meaningful.

---

## The stack, in one picture

Twelve sections is a lot of detail to hold at once. Here is the whole library
in one diagram, read bottom-up — each layer using only what is below it:

```mermaid
flowchart BT
    G["§4 session memory<br/><i>allocate freely, free at once</i>"]
    S["§3 symbols<br/><i>interned names</i>"]
    N["§3 nodes<br/><i>tagged union</i>"]
    T["§2 hash-consed trees<br/><i>equal content = same pointer</i>"]
    P["§5 properties<br/><i>memoisation on the node</i>"]
    L["§6 lists, sets, environments<br/><i>encoded as terms</i>"]
    O["§7 signatures and opcodes<br/><i>O(1) constructor identity</i>"]
    R["§8 recursive terms<br/><i>finite syntax, infinite meaning</i>"]
    W["§9 rewriting"]
    F["§10 fixed points"]
    C["client algebras<br/><i>types, intervals, code generation</i>"]

    G --> S --> N --> T
    T --> P
    T --> L
    S --> O
    T --> R
    P --> R
    R --> W
    R --> F
    P --> W
    P --> F
    O --> C
    W --> C
    F --> C
    L --> C
```

The load-bearing edges are the ones through the middle. Everything rests on
hash-consed trees; hash-consing rests on cheap exact node equality, which rests
on interned symbols; and all of it rests on a memory model that never recycles
an address. Properties depend on trees and enable everything above them.
Recursive terms need both trees and properties, because §8's cycle goes through
the property graph. Rewriting and fixed points are the two ways of computing
over recursion, and the client's algebras sit on top, which is where TLIB stops.

### The argument in twelve sentences

| § | The one thing to remember |
| :--- | :--- |
| 1 | A pass is a fold: one traversal, one algebra per interpretation, and the term algebra is one of them. |
| 2 | Two structurally equal terms are one object, so equality is a pointer comparison and sharing is automatic. |
| 3 | A node is a tagged union and a symbol is an interned name — the same idea as §2, one level down. |
| 4 | Nobody frees anything until everything is freed, which is what keeps a pointer meaning one term forever. |
| 5 | A fold's result is cached on the node it belongs to, which is only sound because the value depends on the term alone. |
| 6 | Lists, sets and environments are terms, so they inherit sharing, equality and memoisation for free. |
| 7 | A constructor's identity is a dense opcode on its symbol, so a fold dispatches in constant time. |
| 8 | Recursion is a finite term denoting an infinite tree, with the cycle in the properties, never in the branches. |
| 9 | Rewriting is a fold into the syntax algebra, memoised for sharing and renaming for immutability. |
| 10 | Attributes over recursion are computed by iteration in a lattice, with widening for termination. |
| 11 | The optional modules add nothing to the core, which is the point. |
| 12 | Everything above is machinery; the meaning lives in the client's algebras. |

### What TLIB deliberately never knows

The boundary has been redrawn in almost every chapter, and it is the same line
each time.

TLIB does not know what a signal is, what a type is, what an interval is, or
what audio is. It does not know the arity of any constructor, which symbols
form a language, or what a term *means*. It does not know whether a fixed point
should be least or greatest, what a delay does to a value, or which analyses a
compiler wants to run. It has no opinion on normalisation, and will not
simplify, reorder or rewrite anything on its own.

What it knows is how to represent terms so that equal ones are identical, how
to annotate them so that nothing is computed twice, how to keep that true in
the presence of recursion, and how to hand a client's algebra a term with a
constructor identity it can dispatch on in one instruction.

That division is the reason the library has survived two decades inside a
compiler that has changed a great deal around it. The core says nothing about
audio, so nothing about audio can obsolete it.

### Where to go next

- [README.md](README.md) — the API surface, layer by layer, and the build.
- [SIGNATURE-SPEC.md](SIGNATURE-SPEC.md) — §7 in full, with the conformance
  tests.
- [REWRITE-SPEC.md](REWRITE-SPEC.md) — §9 in full, including the paired family.
- [tour-examples.cpp](tour-examples.cpp) — every surprising claim in this
  document, as running checks.
- [tests.cpp](tests.cpp) — the library's own test suite, and the executable
  form of §1's arithmetic example.
- [CONCEPT-TOUR-AUTHORING.md](CONCEPT-TOUR-AUTHORING.md) — how this document
  was written, if you want to write one for your own library.
