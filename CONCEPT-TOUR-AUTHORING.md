# Writing a guided tour of a codebase

A method for producing the kind of document that explains *what a
library is made of and why it has that shape* — as opposed to a README
(what it does), a specification (what it must do), or API docs (how to
call it). The output is a single document that walks the concepts of a
system in dependency order, each concept explained four times over at
increasing precision.

Written for an AI agent doing the drafting, with a human reviewing
concept by concept. `A-GUIDED-TOUR-OF-TLIB.md` in this repository is the
worked example.

---

## When this method fits

| Situation | Use this method? |
| :--- | :--- |
| A library whose design is not obvious from its API | yes — this is the case it is for |
| Onboarding a competent programmer into unfamiliar theory | yes |
| A system whose parts only make sense together | yes |
| "How do I call function X" | no — API docs |
| "What must this component guarantee" | no — a specification |
| A change log, a tutorial, a quickstart | no |

The test: if the honest answer to "why is it built this way?" takes
three paragraphs and a reference, this method pays. If it takes one
sentence, write the sentence.

---

## Agree on the method before writing

Do not draft the whole document and then negotiate. Propose the section
template, the concept order and the audience; get agreement; write **one
concept**; get feedback on length, depth and tone; only then continue.
The first concept is the contract for the remaining ones.

Three questions must be answered before drafting:

- **Audience.** Not "developers". Which knowledge is assumed and which
  must be built? "A C++ programmer who knows compilers by practice but
  not the theory" determines every paragraph of the informal sections.
- **Language.** Not automatically the language of the conversation, and
  not always obvious from the repository: mixed-language projects are
  common (README in English, specifications in French). Decide from the
  intended readership and from what the document sits next to.
- **Depth per concept**, expressed as a target length for the first one.

---

## Shape of the document

```tree
Title + one-paragraph statement of purpose
  ::: toc+          — the plan and the table of contents in one
  How to read       — the section template, the reading paths, conventions
  Concept 1..N      — six fixed sections each
  (optional) Bibliography, written last
```

**Order concepts by dependency, not by history.** Not the order the code
was written in, not the order of the source tree — the order in which
each concept needs the previous ones to be stated. Announce the ordering
principle, then say plainly where it is imperfect: a first concept
usually has to name the carrier type it manipulates before that type has
its own chapter. Claiming a strict layering that the text then violates
costs more credibility than admitting the loop.

**Write the `::: toc+` plan first.** It doubles as the spec you check the
content against, and unwritten entries render struck through, so the
table of contents is also a progress indicator.

---

## The six sections

Every concept gets the same six headings in the same order, so a reader
can skip a level uniformly across the document.

| Section | Answers |
| :--- | :--- |
| **The idea** | what it is, informally, entered through what the reader already does |
| **Its role in X** | why the system needs it; what breaks without it |
| **More precisely** | the formal statement, once intuition is in place |
| **In the code** | how it is realised, with pointers to the source |
| **Invariants and non-goals** | what must stay true; what it deliberately refuses to do |
| **Origins** | where the idea comes from; what to read |

Fixed headings, **variable weight**. A minor concept may have a
`More precisely` of two lines. Do not pad a section to fill the
template; do not drop the heading either — its presence is what makes
the document skimmable.

### The idea

Enter through the reader's existing practice, never through the theory.
Describe a task they have done by hand, let them notice the pattern,
*then* name it. The names of the concepts should arrive as labels for
something already recognised.

> Write those four passes by hand and you notice they are the same
> program four times. […] Universal algebra gives names to the three
> things at play here.

End on the observation that reframes everything — the sentence the
reader should still have in mind three chapters later. Every concept has
one, but it is usually the last thing to stabilise: write a frank
placeholder in the first draft rather than stalling on it, and come back
once the formal and code sections have taught you what the concept
really turns on.

Use a concrete, small, *runnable-looking* example. Prefer one that also
demonstrates a magnitude ("31 nodes for a term with a billion leaves")
over one that merely illustrates a shape.

### Its role in X

The section that makes a tour out of a list. Two jobs:

1. **Locate the boundary.** What the system provides, and what it
   deliberately leaves to its clients. Be precise here; an overstated
   boundary ("X provides the syntax algebra") is the most likely
   technical error in the whole document.
2. **Justify what comes next as a demand.** Each later concept should
   appear here as the answer to a requirement this one creates, with a
   forward reference. This turns the remaining chapters into promises
   rather than a table of contents.

A one-sentence summary in bold at the end of the section, for the reader
who keeps nothing else.

### More precisely

Concise, and only what the rest of the document will actually use.

- **Every formula gets a prose paraphrase**, immediately after, as a
  dash-introduced clause or sentence. No exceptions. The paraphrase
  should say what the formula *means for the reader*, and may point at
  what the formula does **not** contain — often the real content.
- **Economise notation.** Each new symbol is a cost. If a notation will
  be needed later for something more important, do not spend it now
  (reserve $μ$ for recursion inside terms rather than for the recursive
  type definition, and say so).
- **Prefer the standard notation that avoids ambiguity.** $|𝒜|$ for a
  carrier is standard but collides with cardinality; naming the carrier
  $A$ is equally standard and reads better.
- **Split the consequences.** Do not state a theorem and move on; state
  separately each consequence the programmer will rely on, and what it
  licenses. "Uniqueness is what makes memoisation forced rather than
  optional" is worth more than the theorem.
- Say explicitly which cases the statement does *not* cover, and which
  chapter handles them.

### In the code

**Point, do not paraphrase.** Links to `file.ext:line`, API signatures,
and short excerpts only where the excerpt *is* the argument. Code copied
into prose goes stale at the first refactoring; a line reference degrades
gracefully.

Three things worth doing here:

- Reduce the mechanism to its irreducible core — "everything else is
  detail around those seven lines" — and show those.
- **Explain what looks superstitious.** A line that seems arbitrary
  (`fData.f = 0.0` before storing a narrower field) almost always
  encodes an invariant. Finding and stating it is the highest-value
  paragraph in the section.
- Quote real measurements from the code or from a run, and attribute
  them ("about 72% of trees never receive a property"). Never round a
  measured claim into a stronger general one.

### Invariants and non-goals

Bold lead-in per item, one short paragraph each. Two kinds of content:

- **Invariants**: what must stay true for the rest of the system to
  work.
- **Non-goals**: what the concept refuses to do, especially where a
  reader would assume otherwise.

Write the non-goals against the **most likely misreading** of the
sections above. If `More precisely` presented terms as an algebra, the
reader will assume the type enforces well-formedness — say that it does
not. This section is where a careful document earns its trust.

Note deliberate limitations honestly, including platform-specific
behaviour and stale comments in the source that would mislead a reader
of the code.

### Origins

Short: a paragraph or three. One line of history, then one to three
references. Prefer:

- the paper that **introduced** the idea, even if unfashionable;
- the paper that best **demonstrates the payoff** (BDDs for
  hash-consing + memoisation), which is often more persuasive to an
  engineer than the founding paper;
- one **practical modern** reference if the founding papers are hard
  going.

Verify attributions. Do not credit a term to a paper that does not use
it; separate what a founding paper actually contains from what its
successors added.

---

## Conventions that carry the document

### Forward references

Unavoidable in a dependency-ordered text. Handle them with a **footnote
at first mention**, containing (a) a thumbnail definition sufficient to
follow the current argument, and (b) the section that develops it.

```
… terms in a real compiler are recursive[^rec], which the definitions
above do not cover at all.

[^rec]: A **recursive term** is one that refers to itself, as in
$x = 1 + x$ — a finite piece of syntax denoting an infinite tree. […]
Developed in §8 (the terms) and §10 (their attributes).
```

State the convention in *How to read*, so the device is legible. It has
a pleasant side effect: it makes the reader curious about chapters they
have not reached.

The footnote list is also a vocabulary audit. Any term used without a
footnote and without an inline gloss is debt — a `Tree` mentioned in
chapter 1 and defined in chapter 3 needs one.

Keep each footnote to a few sentences. If they grow past that
systematically, the concept order is probably wrong.

### Cross-references

Use section numbers (`§5`) rather than titles, and put them where a
reader would ask the question, not where the topic is finished.

### Markup

Rendered by markpage — see `AI-AUTHORING.md` for the full set. What this
genre uses:

| Need | Construct |
| :--- | :--- |
| Plan + table of contents | `::: toc+`, written first |
| Inline mathematics | `$ … $` — not backticks |
| Display formula | ` ```math ` or `$$ … $$` |
| Syntax tree, term structure | ` ```tree svg ` — an *indented outline*, one node per line |
| Filesystem or module layout | ` ```tree ` |
| Algorithm in pseudocode | ` ```algorithm ` |
| Real code | ` ```cpp ` etc., short excerpts only |
| Forward reference, term definition | footnote `[^name]` |

Reserve backticks for genuine code: identifiers, files, types. A
mathematical symbol in backticks reads as an implementation detail.

A generic Markdown linter will complain about repeated headings
(`MD024`) — that is the template working as intended.

---

## Verification discipline

This is what separates a guided tour from a plausible essay. **Writing
this kind of document is a code review**, and it will find defects.
Budget for that.

**Check every claim about the code against the code**, with a line
reference, at the moment of writing it. Not "the API is roughly"; open
the file.

**When a comment and the code disagree, the code wins** — and the
comment is a finding to report, not something to quietly repeat or
quietly ignore.

**Measure rather than assume.** Sizes, ratios, complexities: run it. A
comment claiming `sizeof` is 112 does not make it 112.

**Weaken absolutes.** "Forces" is usually "makes cheap". "Is
exponential" is usually "is exponential in the worst case". "Enforced by
the type system" is often "enforced by the API as it is meant to be
used". Each softening is small; together they are the difference between
a document that survives scrutiny and one that does not.

**Distinguish what a mechanism guarantees from how it is spelled.** A
comparison may be semantically right while its implementation relies on
an unwarranted assumption; say both.

**Cross-read.** Independent passes over the same lines, motivated by
different questions, find different defects. Trying to explain *why* a
line exists finds different bugs than reviewing it for correctness. Use
a second reviewer (human or model) on the drafted prose, and verify its
claims too — a reviewer's confident correction can itself be wrong.

**Fix the code, do not paper over it in the prose.** If the document has
to describe a defect to stay honest, that is the signal to repair the
source. The natural temptation is the opposite: a well-turned paragraph
about a wart is satisfying to write, and it leaves the wart in place.

---

## Keeping it true

Verification at drafting time is only half the problem. A tour full of
`file.ext:line` references and measured figures starts rotting at the
next refactoring, and it rots **silently** — nothing fails, the document
simply becomes fiction. Plan for that from the first concept.

**Make the examples executable, as a build target.** The method above
asks for examples that look runnable; that recommendation is a trap
unless they *are* run. Put them in a compiled file (`tour-examples.cpp`)
wired into the test runner, and quote from it by line reference. An
example that is only prose will drift, and — the failure mode to fear —
it will drift while the test suite stays green, because nothing was
watching it. A demo or benchmark left out of `ctest` can assert a false
claim for weeks without anyone noticing; a tour is exactly as fragile.

**Stamp each concept with what it was verified against.** A line per
chapter — *code references verified at `<sha>`* — tells a reader how much
history separates the text from the current source, and turns
re-verification into a targeted diff instead of a full re-read.

**Name the channel for findings.** The author of a tour is often not the
maintainer of the code, and almost never the maintainer of every
vendored copy of it. Decide, before the first finding, where findings
go: to the owner, to a journal, to an issue tracker. And close them by a
stated rule — *a finding is closed when every copy is fixed, built and
tested*, not when it has been reported, and not when the local copy
compiles.

**Never let the tour be the sole holder of an invariant.** Drafting
regularly unearths an invariant that is real, load-bearing, and written
down nowhere. Writing it beautifully in the tour is not enough: put it
at the code site too, as a comment or an assertion, and let the tour
point at it. **The code asserts; the tour explains.** Otherwise the
knowledge migrates into a document the maintainers do not open, which is
how the stale comments this method keeps finding were created in the
first place.

---

## Checklist per concept

- [ ] `The idea` enters through something the reader already does
- [ ] The reframing sentence is present and memorable
- [ ] `Its role` states the boundary and justifies later concepts as demands
- [ ] Every formula has a prose paraphrase
- [ ] No notation spent that a later chapter needs more
- [ ] Consequences stated separately from statements
- [ ] Code claims verified against the source, with line references
- [ ] At least one apparently arbitrary line explained
- [ ] Measurements measured
- [ ] Non-goals written against the likely misreading
- [ ] Absolutes weakened where they are not absolute
- [ ] `Origins` attributions verified, not recalled
- [ ] Every term used before its chapter has a footnote
- [ ] Examples exist as a compiled, tested target — not prose only
- [ ] Invariants discovered while writing are also written at the code site
- [ ] Concept stamped with the commit its code references were verified against
- [ ] Findings routed to their owner, and closed only once every copy is fixed and tested
