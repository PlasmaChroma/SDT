# Aurora Namespaced Import Requirements (Draft)

## Goal
Add an import mechanism that allows an `.au` file to reuse definitions (starting with instrument/patch designs) from other `.au` files without naming collisions.

Primary requirement:
- Every import applies a namespace identifier so imported symbols cannot conflict with local symbols unless explicitly allowed.

## Scope
In scope for first iteration:
1. Importing `patch` definitions from another `.au` file.
2. Optional import of additional top-level elements (`bus`, `pattern`) behind explicit allow-lists.
3. Alias/namespace-based symbol qualification in score/events/automation/send references.

Out of scope for first iteration:
1. Importing `globals` or `outputs`.
2. Runtime dynamic loading.
3. Network/URL imports.

## Design Principles
1. Deterministic: identical project tree + source text must produce identical outputs.
2. Explicitness over magic: imported symbols must be clearly qualified.
3. Safe composition: no accidental override of local symbols.
4. Backward compatibility: non-import files continue working unchanged.

## Syntax Requirements
Support an `imports` block at top level:

```au
imports {
  use "./instruments/strings.au" as strings
  use "./kits/drums.au" as drums only [Kick, Snare, HatClosed, HatOpen]
}
```

Required syntax elements:
1. Source path (string).
2. Namespace alias via `as <identifier>`.
3. Optional `only [...]` selector list to import specific symbols.

Optional future syntax:
1. `except [...]` selectors.
2. Multi-element selectors by kind (`patch: [...]`, `bus: [...]`, `pattern: [...]`).

## Namespace Semantics
1. Imported symbols are referenced as `<alias>.<symbol>`.
2. Local (non-imported) symbols remain unqualified.
3. No implicit merging between namespaces.
4. Nested imports flatten to the importing file using namespace chains:
   - recommended behavior: `rootAlias.childAlias.Symbol`.

Example references:
1. `play strings.LegatoLead { ... }`
2. `send: { bus: "fx.Space" }` where `fx` is an import alias and `Space` is imported bus.
3. `automate patch.strings.LegatoLead.filt.cutoff linear { ... }`

## Symbol Eligibility (v1)
Required:
1. `patch`

Optional if implemented in same phase:
1. `bus`
2. `pattern`

Not importable:
1. `globals`
2. `outputs`
3. `score` sections/events (unless explicitly added in a later phase)

## Path Resolution Rules
1. Relative import paths resolve against the importing file directory.
2. Absolute paths are allowed but discouraged.
3. Path traversal outside project root should be configurable:
   - default deny for safety in CLI mode.
4. Canonicalize paths before cycle checks to avoid duplicates via symlink/path variants.

## Validation Requirements
Emit validation errors for:
1. Missing import file.
2. Parse/validation failure inside imported file.
3. Duplicate import alias in same file.
4. Unknown symbol in `only [...]`.
5. Reference to unqualified imported symbol (when qualification required).
6. Illegal import of non-importable top-level element.
7. Import cycle detected (`A -> B -> A`).

Emit warnings for:
1. Imported symbol declared but unused.
2. Large import file when only few symbols are used (optimization hint).

## Conflict Rules
1. Local symbols and imported symbols can share base names because imported symbols are always namespaced.
2. Two imports may share symbol names if aliases differ.
3. Alias cannot equal an existing local top-level symbol name.
4. Explicit unqualified references always bind local symbols only.

## Runtime/Compilation Model
1. Imports are resolved before score expansion and semantic validation.
2. Imported ASTs are copied into compilation context with namespaced symbol IDs.
3. Internal node IDs inside imported patches remain patch-local and do not require global namespacing.
4. Automation/reference parsing must accept namespaced patch names as first-class identifiers.

## Determinism and Caching
1. Resolver must produce stable import order (source order).
2. Hash key for render determinism should include:
   - importing file content,
   - imported file contents,
   - resolved canonical paths.
3. Optional compile cache may memoize parsed imports by canonical path + file hash.

## Error Reporting Requirements
Diagnostics should include:
1. Importing file path and line/column.
2. Imported file path.
3. Symbol and alias where relevant.
4. Cycle chain details for cycle errors.

## Security Constraints
1. No network fetch in default CLI mode.
2. No command execution during import resolution.
3. Optional `--allow-absolute-imports` CLI flag for stricter environments.

## Examples
### Example 1: Basic patch import
```au
imports {
  use "./lib/strings.au" as strings
}

score {
  section A at 0s dur 8s {
    play strings.LegatoLead { at: 0s, dur: 8s, vel: 0.8, pitch: C4 }
  }
}
```

### Example 2: Selective drum import
```au
imports {
  use "./lib/drumkit.au" as kit only [Kick, Snare, HatClosed, HatOpen]
}
```

## Implementation Phases
### Phase 1 (minimum viable)
1. Parser support for `imports` block and `use ... as ...`.
2. Resolve/import `patch` only.
3. Namespaced patch references in `play/trigger/gate/seq` and automation targets.
4. Cycle detection and deterministic resolution.

### Phase 2
1. `only [...]` selectors.
2. Optional `bus` and `pattern` imports.
3. Unused import warnings and import cache.

### Phase 3
1. Extended selector forms (`except`, per-kind selectors).
2. Tooling/diagnostics improvements.

## Acceptance Criteria
1. A file can import two libraries with overlapping patch names using different aliases.
2. Imported patch playback works via `play alias.PatchName`.
3. Automation targets correctly resolve for namespaced patches.
4. Import cycles fail with actionable diagnostics.
5. Determinism tests pass with imports enabled.
