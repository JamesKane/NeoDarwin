<!-- SPDX-License-Identifier: BSD-2-Clause -->
# Conventions for normative API specifications

These rules apply to every NeoDarwin API specification that development builds against. They were introduced for the toolkit API and its lower layers (P4-12, P4-16, P4-17, P4-18) and apply to every later specification.

A specification is three things kept together:
1. a normative text;
2. compile-checked headers;
3. a conformance list.

## 1. Files and placement

| Item | Location |
|---|---|
| Normative text | `docs/<area>/<name>.md` |
| C header(s) | beside the text: `docs/<area>/<name>.h` |
| Swift interface (toolkit only) | `docs/<area>/<Module>.swift` |
| Build targets that compile-check the headers | `docs/<area>/BUILD.bazel` |

**The check.** Each header is compiled by an `nd_cc_library`, with a tiny `nd_cc_binary` or test that includes it and `static_assert`s its record layouts. Swift interfaces are type-checked by an `nd_swift_library`. `bazel test //docs/...` must pass.

**Header copies in the implementation.** When an implementation epic starts, it copies the header into its component. The spec's copy stays the reference, and a test compares the two.

## 2. Status and versioning

**Front matter.** Every specification opens with a status block:
- `Status`: `draft` → `review` → `normative`;
- `Version` (integer);
- `Epic`;
- `Evidence` (paths into the study repository, with the commit);
- `Supersedes`, if any.

**Evolution is additive** (toolkit charter P13):
- A normative version is never edited in meaning.
- New calls, event types, record fields (in reserved space or in versioned extensions) and capabilities are added under a new version number.
- Removal is by deprecation only.

**Capability queries.** Every interface exposes a capability query from version 1, so clients can test for later additions without version arithmetic.

**Changelog.** Each document ends with a changelog.

## 3. Normative language and requirement ids

**Keywords.** MUST, MUST NOT, SHOULD, SHOULD NOT and MAY are used as in RFC 2119 and RFC 8174. They count as normative only in capitals.

**Requirement ids.** Every normative sentence carries an id in brackets at its end: `[PREFIX-AREA-NNN]`.

| Spec | Prefix | Example |
|---|---|---|
| Toolkit API | `TK` | `[TK-LOOP-004]` |
| Window protocol revision 2 | `WP` | `[WP-FRAME-002]` |
| Scheduling contract | `SC` | `[SC-WAIT-001]` |
| Audio service | `AU` | `[AU-STREAM-003]` |

**Id rules:**
- Ids are never reused.
- A withdrawn requirement keeps its id, marked withdrawn.

## 4. Structure of a specification

Each specification has these sections, in this order:

0. Status block, and one paragraph on what the document fixes and what it deliberately leaves open.
1. **Scope and non-goals.**
2. **Terms.**
3. **Model**: the objects, their lifetimes and the state machine(s), with a diagram where it helps.
4. **Interfaces**, one subsection per area. Each gives:
   - types and records, with exact layouts for anything that crosses a process or ABI boundary;
   - calls or messages;
   - semantics;
   - errors, and how each is reported;
   - threading;
   - lifetime and ownership.
5. **Versioning and capabilities.**
6. **Security and capabilities**, meaning which namespace path or token grants what. Refer to `docs/architecture/namespaces-agents.md`.
7. **Performance contract**: measurable targets that are requirements, not goals, each tied to a conformance test.
8. **Conformance**: a table with these columns:
   - test id (`<PREFIX>-T-NNN`);
   - requirement ids covered;
   - method (unit test, protocol conformance through `deskconform` or its equivalent, measurement on NeoDarwin, or an S7 prototype);
   - pass criterion.
   Every MUST is covered by at least one test.
9. **Rationale and evidence**: why each major decision, citing friction entries (`F-nnn`), charter principles (`P1`…`P14`), and the study's reports and S7 results.
10. **Open issues.**
11. **Changelog.**

## 5. Headers

- **Language and dependencies:** freestanding C23; only `<stdint.h>`, `<stddef.h>` and `<stdbool.h>`, unless the text justifies more.
- **Also valid C++ (C++20):** many clients are C++ (SDL, Qt, Dawn, engines).
  - Wrap declarations in `extern "C"` guards.
  - Never use C-only syntax in a declaration unless it sits behind a macro. For example, `[static N]` array parameters expand to plain `[N]` in C++.
  - The `BUILD.bazel` compile check includes a C++ translation unit.
- **First lines:** the SPDX line, then `// NeoDarwin-Language: portability: <reason>` (the C ABI is the cross-language boundary, per language-policy.md §3).
- **Layouts:**
  - Fixed-width fields only.
  - Explicit padding and reserved fields.
  - Little-endian on the wire.
  - `static_assert` on every record's size and on the offset of every field that crosses a boundary.
- **Handles:** `uint64_t`, index plus generation (charter P5), with named accessor macros.
- **Versions and errors:**
  - Each header defines `<PREFIX>_API_VERSION`.
  - Errors are an `enum` of codes; functions return `bool` or an error code, with thread-local detail where relevant (charter P12).
- **Mirror of the text:** a header never contradicts its text. Where they differ, the text is normative and the header is a defect.

## 6. Swift interfaces (toolkit)

- **Form:** the public Swift surface is written as a compilable interface file. It declares public protocols, value types, enums and function signatures.
- **Where bodies are needed:** they MUST be `fatalError("interface only")`. The file is type-checked, never linked into a product.
- **Language rules:** it follows language-policy.md §4 (`~Copyable` owners, `Span` views, typed throws, `Sendable`).
- **T2 marking:** functions on the T2 paths (event decode, layout, draw, audio render) are marked in the text and carry the performance-annotation attributes that the pinned toolchain supports.

## 7. Cross-references between specifications

- **Owning layer:** a lower-layer specification owns its records and semantics.
- **Upper layers:**
  - They cite requirement ids and never restate them.
  - The toolkit API specification maps each of its concepts to the lower-layer requirements it relies on.
- **Order of changes:** a change needed in a lower layer is made there first.
