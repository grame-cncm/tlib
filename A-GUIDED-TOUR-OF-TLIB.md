# A guided tour of TLIB

TLIB is the tree library at the heart of the [Faust](https://faust.grame.fr)
compiler. This document explains what it is made of, in the order in which its
concepts depend on one another — not in the order in which they were written.
It is addressed to a C++ programmer who knows compilers by practice rather than
by theory, and it introduces the small amount of algebra needed to see why the
library has the shape it has.

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
| **Origins** | where the idea comes from, and one paper worth reading |

Three reading paths follow from this. *The idea* + *Its role* alone give a
complete informal tour. Adding *More precisely* gives the theoretical account.
Adding *In the code* gives the implementer's account.

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
for (int i = 0; i < 30; i++) t = tree(fAdd, t, t);
```

As a term, `t` has more than a billion leaves. As a hash-consed structure, it
is 31 nodes. Any compiler that duplicates subexpressions — inlining,
substitution, unrolling — produces this shape constantly, in milder form.

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
from the node's canonical hash and the children's, and `canonicalTreeLess`
([tree.cpp:229](tlib/tree.cpp#L229)) uses it as the primary key of a total
order derived from *values only* — symbols compared by name, ties broken
structurally. Two processes that build the same term values order them
identically, whatever their construction history, which is what canonical forms
need. One exception is deliberate: a node carrying a raw pointer payload falls
back to hashing the pointer ([node.hh:90](tlib/node.hh#L90)), so terms
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
a hash-consed DAG of a decision term, and every BDD operation is a memoised
fold over it; that combination turned Boolean function manipulation from
intractable into routine, and it is architecturally the same as what §2 and §5
describe here.

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
demand, which is how §8 names the variables it introduces.

[^gensym]: `unique("R")` returns a symbol named `R0`, `R1`, `R2`, … guaranteed not to collide with any existing one. The idea and the need are as old as Lisp macros, where the same operator is called `gensym`: whenever a program generates a binding, it must be able to name it without accidentally capturing a name the user chose. §8 uses it for the variables introduced when converting a recursion to symbolic form.

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
stop. It is also, in the vocabulary of §1, the initial one. §8 is where that
restriction has to be given up, and it is why the notation $μ$ is kept in
reserve here: there it will denote recursion *inside* a term, which is a
different thing from the recursion of the definition above.

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

`Node` is at [node.hh:75](tlib/node.hh#L75), and it is exactly the tagged union
described above:

```cpp
class Node : public Garbageable {
    int fType;                     // kIntNode, kInt64Node, kDoubleNode, kSymNode, kPointerNode
    union { int i; double f; Sym s; void* p; int64_t v; } fData;
```

Equality ([node.hh:144](tlib/node.hh#L144)) is the one line the rest of the
library leans on:

```cpp
bool operator==(const Node& n) const { return fType == n.fType && fData.v == n.fData.v; }
```

The payload is compared through `fData.v`, the 64-bit member — that is, as raw
bits, whatever the actual type. This explains a detail that would otherwise
look superstitious: the narrower constructors write `fData.f = 0.0` *before*
storing their value ([node.hh:109-141](tlib/node.hh#L109-L141)). Zeroing the
widest member first makes the unused bits deterministic, so that two nodes
built from the same `int` compare equal.

Comparing floating-point payloads as bits rather than with `==` is also the
right choice, and not only for speed. IEEE equality is not reflexive: a `NaN`
is not equal to itself. A hash-consing table built on it would fail to find a
`NaN` node it had just inserted, and would keep allocating new ones forever.
Bitwise comparison restores reflexivity and makes node equality a genuine
equivalence relation. The price is a second surprise in the other direction:
`+0.0` and `-0.0` have different bit patterns, so they are different nodes.

Pattern matching is a family of predicates rather than a `switch` on the tag —
`isInt(n, &i)`, `isDouble(n, &d)`, `isSym(n, &s)`
([node.hh:180](tlib/node.hh#L180) onwards), each testing the tag and extracting
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
`symbol("a\nb")` and `symbol("a b")` are the *same* symbol.

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

### Origins

Both halves of this section are in the paper that started the field: John
McCarthy's *Recursive Functions of Symbolic Expressions and Their Computation
by Machine, Part I* (CACM 3(4), 1960). Lisp's data is atoms and pairs — TLIB's
nodes and branches — and Lisp's atoms are interned in the *oblist*, so that
reading the same name twice yields the same object and `eq` compares them in
one instruction. The design of this section is that arrangement, sixty years
on, with a tagged union in place of the atom types and a growable hash table in
place of the association list.

For the table itself, the reference is Knuth's *The Art of Computer
Programming*, volume 3, §6.4 — separate chaining, load factors and rehashing
are exactly what `Symbol::get` and `CTree::make` implement.
