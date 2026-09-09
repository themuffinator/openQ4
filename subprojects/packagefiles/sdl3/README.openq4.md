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
