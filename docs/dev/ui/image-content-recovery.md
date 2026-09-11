# Exact image bytes for cold recovery

The renderer can now capture and reconstruct two bounded CPU image cohorts:
direct compressed DDS files and previously loaded `.bimage` cache files. This is
a foundation for settings recovery. It does not enable additional SYSTEM Apply
effects or implement a complete image/material recovery transaction.

## What the records prove

`imageFileContent_t` identifies a virtual filesystem qpath, exact byte length,
kind and SHA-256 of the entire owned file read. The existing first-party
`idCrypto::SHA256` implementation supplies the digest. The DDS loader hashes the
same owned buffer that supplies compressed mip views; generated-image loaders
hash the same backing allocation that supplies their parsed mip views. A second
read or a filename/timestamp lookup cannot substitute for those bytes. Unsupported
source/kind pairs and absent file identities are refused before output hashing.

`imageBinaryContent_t` identifies every admitted CPU output mip. Version 1 hashes
an explicit little-endian domain/header followed by ordered layer/level metadata
and SHA-256 of each complete mip payload. Header format, color format, texture
type, dimensions and level/layer counts participate. Physical `.bimage` record
order and timestamps do not change this output identity; they still participate
in the separate whole-file identity. Compressed bytes, including block padding,
are hashed without decoding. This proves identical admitted bytes, not identical
rendering on different drivers.

`idImage::GetPortableContent` first requires the existing checked consumed-policy
receipt. Complete mip/layer upload admission, the bulk GL/Vulkan completion
watermark, current instance/storage/observation epoch, device and failure state
must still match. An active load, pending uploads, a default image or a stale
record cannot produce a portable descriptor. Capturing does not perform a native
wait. The caller may separately complete already submitted uploads through the
existing checked bulk boundary.

Two scopes remain intentionally distinct:

| Scope | Authority |
| --- | --- |
| `IPC_DIRECT_SOURCE` | Exact DDS source bytes, the captured immutable reduction policy and exact authored-mip selection, and the complete selected CPU output. |
| `IPC_CACHE_PIXELS_ONLY` | Exact previously admitted cache bytes and output. No original decoded-source dimensions, source-content lineage, or original policy authority. |

Cache descriptors contain no reduction evidence and carry the default empty
resolved policy. Supplying source-policy fields in a cache descriptor is refused.
Such a descriptor cannot justify generating a target under a different policy.
No current CVar is read to reconstruct either scope.

## Checked CPU reconstruction

`R_ReconstructImageContent` freezes the complete descriptor before opening its
qpath through the normal VFS. It opens that exact name without source-extension
search, cache-name regeneration or fallback. Whole-file identity is checked
before parsing the captured source. The actual DDS or `.bimage` parser validates
the layout; DDS selection must reproduce the captured reduction result. Every
output header and mip then has to match the captured output digest.

All fallible work uses a separate `idBinaryImage`. VFS handles close before the
final callback-free ownership swap. Refusal or an allocation/parser exception
preserves the caller's original CPU image, including its mip bytes and backing
allocation. The caller must keep its output object alive through VFS callbacks;
this API is not a native callback lifetime coordinator. It does not allocate GPU
storage, upload data, publish a settings result or modify CVars.

The content identity deliberately follows selected bytes rather than the archive
or loose-file container that happened to supply them. Normal VFS precedence and
restrictions still apply. Identical bytes from another selected container can
satisfy the record; different bytes at the same qpath cannot, even if timestamps
match. Removed or regenerated caches therefore cause a refusal. This slice does
not pin files durably or guarantee their availability after a crash.

Version 1 accepts stock ASCII relative qpaths shorter than 512 bytes and files
up to 1 GiB. Invalid or unsupported paths get no portable receipt; ordinary asset
loading retains its existing behavior. The output digest is bounded to 32 levels,
one or six layers and 32768-pixel axes; actual format/byte-layout checks also apply.
These bounds are not permission to accept larger assets than the existing loader
supports. Records contain no pointers or process/device tokens. The C++ structs
are not a wire format and must never be serialized as raw memory.

## Remaining integration

Cold reconstruction of decoded images, cube-face sources, image programs,
material source/dependency cohorts, defaults and heterogeneous historical policy
sets still needs explicit source lineage and reconstruction rules. Unobserved or
unsupported paths refuse capture. Neither default status nor generated-cache
classification creates content identity. A future cohort controller must validate
every member before any policy mutation and bind logical image/sampler identity,
material dependencies, actual restore policy, native uploads and journal bounds.
The per-image CPU function alone is not a cohort preflight or rollback protocol.

The settings journal codec, durable file retention, host/controller integration
and effect activation remain separate work. Format semantics are versioned;
changing enum or decoder meanings requires an explicit compatibility decision.

## Verification scope

`tools/tests/renderer_image_content.py` compiles the actual identity, loader and
reconstruction methods with counted VFS/allocator boundaries. It covers pristine
rollback on failure, changed same-path content, truncations, malformed mip headers,
allocation exceptions, frozen request callbacks, ordered output digests and
rejected compiled mutations. Format enums and byte-size functions come from the
production source. Test-generated assets are first-party byte fixtures.

The observer and reduction suites retain their existing coverage; observer tests
also distinguish completed native admission from portable scope eligibility.
The fake native functions qualify method ordering and refusal, not driver behavior.
Full translation-unit compilation checks engine headers and renderer variants.
No real asset, game launch, native GPU reconstruction, cross-platform visual
equivalence or live settings recovery is claimed by these tests.
