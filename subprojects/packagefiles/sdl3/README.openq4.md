# openQ4 SDL3 package adjustments

The Meson package builds [SDL 3.4.10](https://github.com/libsdl-org/SDL/tree/release-3.4.10),
copyright Sam Lantinga and the SDL contributors, under its zlib license. That
license permits the modifications below and is compatible with openQ4's GPLv3
distribution. SDL's original license notice remains in the source distribution.

`clipboard-status.patch` is an openQ4-authored modification, not an upstream SDL
release change. It makes Windows clipboard retry, clear, allocation, lock and
ownership-transfer failures observable to the caller, preserving the primary
error and freeing storage whose ownership did not transfer. The shared text
setter also checks its copy allocation before publishing callback data. The
Windows read fallback tolerates failure to allocate its empty result.

Text reads always request Unicode, including when only ANSI or OEM text was
advertised, and report failed retrieval/conversion. This follows Windows'
[synthesized clipboard formats](https://learn.microsoft.com/en-us/windows/win32/dataxchg/clipboard-formats#synthesized-clipboard-formats)
and [clipboard locale conversion](https://learn.microsoft.com/en-us/windows/win32/dataxchg/standard-clipboard-formats)
contracts. The patch removes raw legacy-byte guesses; Windows owns the conversion
using the clipboard's locale rather than an assumed process code page.

This supports checked UTF-8 clipboard primitives for future retained text fields.
An unsuccessful write may already have cleared the native clipboard; the patch
does not promise clipboard rollback. Native platform newline conversion remains.
It does not enable a live text field, input route, IME, shaping or editor feature.
Only the bundled Windows provider verified to contain this patch enables the private
`OPENQ4_SDL3_CHECKED_CLIPBOARD=1` build capability; external/system SDL with the
same version is not assumed patched. X11, Wayland and Cocoa native failure
paths still require qualification. Running the counted Windows/SDL tests on
Linux does not qualify those native providers. Without the capability the new checked
primitives report unsupported and make no SDL calls. Legacy clipboard routes
remain available with their existing behavior.

`text-provenance.patch` and the adjacent `SDL_openq4_text_provenance` sources are
also openQ4-authored zlib-licensed modifications. They provide a private Windows
text observer, advertised only by this bundled provider through
`OPENQ4_SDL3_TEXT_PROVENANCE=1`. They do not change public SDL event layouts or
the existing native IME UI hint. The Windows dispatcher still runs its original
IME handler and default window procedure with their original return behavior.

The observer puts bounded, payload-free markers in the same SDL event queue.
Each actual admitted text commit has a private immutable UTF-8 record and an
explicit token in the public text event's reserved field. Association occurs
after event filters/watchers and before queue admission, so a filter rejection
or transformation is respected. Native scope is claimed once for the exact
`SDL_SendKeyboardText` event before those callbacks. Reentrant public pushes,
including the same event pointer, and nested `SDL_SendKeyboardText` calls receive
Unknown origin; they cannot inherit the outer native admission. A public queue-admission failure removes its
private commit. `Result` notifications describe IMM state and never insert
characters independently; the actual commit is the only insertion record.
Synthetic or off-thread text has no native composition affinity. Consumers must
copy and release markers, suppress only their explicitly associated public
duplicates, and reserve unassociated/failure text for validated legacy fallback.

The copy/release API is main-thread-only. It bounds storage to 256 records,
1 MiB of text including terminators, and 65,536 UTF-8 bytes per record. Allocation,
encoding, admission, and counter exhaustion failures poison retained affinity.
The independently queried health flag does not depend on delivery of a failure
marker. Filters that change a marker's type, window, token, reserved field or
data pointers are rejected before queue insertion and poison the observation;
SDL-owned timestamp changes remain permitted. Flushes/filters can strand only bounded slots, released by explicit reset,
disable, or video shutdown. Reset never reuses tokens and does not restore trust
in existing native windows; callers must reconcile their own queues. Arbitrary
filters that remove or mutate already queued markers/public events cannot be
assumed lossless. Token exhaustion fails closed without pointer truncation.

This is deliberately a partial IMM observation contract. SDL 3.4.10 has no active
TSF text store or composition sink. A native Begin establishes an observed
interval; preedit clearing and synchronous default-result character generation
retain that interval. Unmarked `WM_CHAR` is identified as a native character
path, **not** as proven physical-key causation. Missing/overlapping Begin,
cancellation, context changes, and asynchronous result characters are unknown.
After End or cancellation, the observer conservatively keeps that native window
lifetime tainted: a later Begin cannot prove that a late `GCS_RESULTSTR` belongs
to it. Thus this hook cannot qualify repeated production IME editing. Native
legacy characters remain available; an ordered retained broker must reject
unknown origins rather than adopt them into the currently focused field.

Microsoft documents the underlying [default IME result translation](https://learn.microsoft.com/en-us/windows/win32/intl/status--composition--and-candidates-windows),
[IMM message parameters](https://learn.microsoft.com/en-us/windows/win32/intl/wm-ime-composition),
and separate [TSF composition identity and sink](https://learn.microsoft.com/en-us/windows/win32/tsf/compositions).
The counted native-method tests validate observer, queue, and ownership logic;
they do not qualify an installed IME, native candidate positioning, TSF,
retained composition rendering, cross-platform input, or a completed text field.
