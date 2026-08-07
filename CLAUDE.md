# Working in tlib

## Read the coordination channel first

Two Claude sessions work on this codebase: one in the sources (`faust`,
`signals`, `tlib/`), one on the documentation. They coordinate through two
append-only journals, and **neither is notified of the other's writes** — a
message once sat unread for four days because nothing prompted a read.

So, **at the start of any task touching tlib, and before answering "any news?":**

```
tail -80 ~/Documents/Install/faust-migration/DIALOG.md     # what the sources side wrote
tail -40 DIALOG.md                                          # what this side last said
```

Write replies at the **end** of `tlib/DIALOG.md`, under a dated header
`## AAAA-MM-JJ — tlib`, never inside an older entry. Each item carries a status:
**INFO** (nothing expected), **ACTION** (something is expected of the other
side), **OPEN** (finding not yet closed), **CLOSED** (with what closed it). A
finding is closed when *every copy is fixed, built and tested* — not when it has
been reported. `DIALOG.md` is gitignored: it is live scratch, not a project
artifact.

## Territory

Sources are the other session's, documentation is this one's.

| | |
| :--- | :--- |
| theirs | `tlib/*.hh`, `tlib/*.cpp` |
| ours | `*.md`, `tour-examples.cpp`, and its `CMakeLists.txt` target |

Need a source change? Write it in `DIALOG.md` as an **ACTION**; they apply it.
The one exception, agreed by both sides: a **literal port** of a fix they have
already validated and asked to be carried over — copy it without rewriting, and
report any line where you diverge.

## Three vendored copies

`tlib/` is duplicated into `faust/compiler/tlib/` and `signals/tlib/`. They must
stay byte-identical:

```
diff -rq tlib ~/Documents/Install/faust/compiler/tlib
diff -rq tlib ~/Documents/Install/signals/tlib
```

## Build and test

```
cmake --build build && (cd build && ctest)
```

Four tests must pass. `tlib-tour-examples` is the guided tour's claims made
executable — if it fails, a documented behaviour has changed.

## The documents

- `A-GUIDED-TOUR-OF-TLIB.md` — the guided tour, thirteen concepts in dependency
  order. Each ends with the commit its line references were checked against;
  re-verification is a diff against that sha, not a re-read.
- `CONCEPT-TOUR-AUTHORING.md` — how the tour is written and verified. **Read it
  before editing the tour.** It carries the rules that cost the most to learn:
  the code wins over the comment, beware the *faithful lie* (prose false
  *because* faithful to a stale comment — search the call sites), weaken
  absolutes, and the code asserts while the tour explains.
- `REWRITE-SPEC.md`, `SIGNATURE-SPEC.md` — specifications, in French.
- Rendered by [markpage](https://markpage.org); follow
  `~/Documents/Install/markpage/AI-AUTHORING.md` for its constructs.

After editing a document, check it mechanically: every `file:line` reference in
range, no inline `$…$` split across a line break, `::: toc+` entries matching
the headings, containers and fences balanced.
