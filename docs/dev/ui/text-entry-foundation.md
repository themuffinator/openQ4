# Retained text-entry foundation

This increment starts from engine `e3e65887480368191df154a9b52cce69d8e72140`
and companion `1cd33980f072ac3d78978a07b388b4b6fe6b5eb2`. It provides
tested editing and transport primitives for the forthcoming precise SYSTEM
fields. It does not yet enable an editable production control, native text
delivery, IME, or clipboard commands in a retained menu.

## Implemented contract

- `TextInput` validates complete UTF-8 commits and preedit records. UTF-8 byte,
  Unicode scalar and UTF-16 selection offsets normalize to scalar boundaries.
  Malformed, NUL-containing or oversized input fails atomically. The transport
  accepts at most 65,536 bytes; controls can impose a smaller policy.
- `TextEditBuffer` owns text, selection and transient preedit independently of
  accepted application state. Empty native commits clear preedit without
  deleting selected text. Explicit empty replacements delete the selection.
  Committing composition replaces the captured selection in one undo operation.
  Failed operations preserve the prior buffer, selection and history.
- Single-line fields reject ASCII and Unicode line separators. Multiline
  replacements normalize CRLF/CR to LF. Undo and redo share a 64-entry,
  1 MiB text-payload budget; this is payload accounting, not a resident-memory
  measurement. Scalar offsets are not a grapheme or bidi navigation contract.
- Decimal parsing is locale independent, finite and exact within binary64.
  Incomplete signs, decimal points and exponent prefixes remain editable but
  cannot become proposals. Bounds do not imply slider-step quantization.
  Formatting round-trips finite readbacks and honors the exponent syntax policy.
- Checked clipboard operations enforce the SDL main thread, UTF-8 validation,
  bounded accepted copies and explicit failure. They are enabled only for the
  patched bundled Windows provider. External SDL and unqualified native
  backends return unsupported without SDL calls. No live clipboard is accessed
  by the regression suites.
- Windows and POSIX event queues release only pending owned payloads on clear
  and overflow. Dequeued payloads remain caller-owned. Discarded console bytes
  are erased before release. Queue order and the public event ABI are unchanged.

The [owned SDL patch and attribution](../../../subprojects/packagefiles/sdl3/README.openq4.md)
document retry, allocation, clear, lock, transfer and conversion failures.
Windows requests Unicode for legacy clipboard formats using the operating
system's conversion. Failed writes may already have changed the native
clipboard; no clipboard rollback is promised. Native newline conversion and
SDL's internal read allocation remain outside the accepted-copy limit.

## Qualification

Windows and WSL standalone runs pass 2,228,818 scalar codec checks, 119 checked
clipboard checks and four unsupported-provider configurations. The text-edit
model passes 1,078 checks and rejects seven compiled source mutations. The
actual patched SDL method bodies pass 254 checks and reject ten compiled
mutations using counted platform substitutes. Linux execution of those
substitutes does not qualify X11, Wayland or Cocoa clipboard behavior.

Both real platform queue implementations pass 23,303 ownership checks, with
1,218 allocations/releases each; six compiled mutations are rejected. These
are compiled method tests, not a full POSIX engine or concurrent-producer test.

The integrated Windows engine build and staging pass. Both new native suites
and adjacent value interaction/runtime suites pass. Existing SDL input parity,
SYSTEM Session routing, character-set and language-table checks pass. The
generic event-delivery test needed its isolated Session double updated for the
already-existing SYSTEM owner fields; it now passes. No production Session
behavior changed for that test repair.

A hidden, windowed Windows SP/OpenGL run entered `airdefense1` gameplay before
opening SYSTEM. It exited successfully after 55.610 seconds with the same 39
stock warning occurrences as the reviewed baseline. All nine ordinary UI scale
samples retain exact RGB equality to the native reference. Four representative
engine images were inspected: both gameplay samples, the native SYSTEM page
and the explicitly exempt legacy crop. This smoke checks loading/queue/renderer
integration; it does not exercise a new text field or clipboard exchange.

Generated-HTML validation also found eleven existing source-file citations
whose relative targets are not packaged. They now link to verified immutable
repository files. The original failing documentation log is retained.

Local records are under `.tmp/ui/text-input-foundation/`. The frozen isolated
handoff remains separately bound at
`.tmp/ui-text-entry-worktree/.tmp/text-entry-handoff.json`; subsequent integration
changes and source provisioning are recorded separately. The initial native
test invocation used an obsolete test name and ran no tests; its failed log is
retained beside the successful invocation.

## Required next integration

The first vertical slice pairs an exact brightness field with its slider as
sibling semantic controls, sharing the existing typed draft proposal. It must
include authored field/validation visuals, caret and selection geometry from
the same text layout used for display, ordered native delivery, immutable
proposal acknowledgement, and local dirty-edit guards on Apply and exit.

Native events must retain their owner across collection and ordered Session
dispatch. Preedit presentation clearing does not release composition-result
affinity. Focus, selection, modal, window and resource lifetimes must invalidate
stale input. Stock SDL events alone do not prove native composition provenance;
the native adapter and its cancellation behavior need separate implementation
and qualification. Event-journal length/pointer validation is also required
before introducing another payload route.

General shaping, fallback, grapheme and bidi editing, candidate positioning,
physical device/IME/platform qualification and the full authoring application
remain required. No requirement, GUI migration, milestone or final product
gate is accepted by this foundation.
