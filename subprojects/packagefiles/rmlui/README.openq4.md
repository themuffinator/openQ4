# openQ4 RmlUi patches

The bundled [RmlUi 6.3](https://github.com/mikke89/RmlUi/tree/6.3) is distributed under its [MIT license](https://github.com/mikke89/RmlUi/blob/6.3/LICENSE.txt). Existing upstream ownership and licensing remain unchanged.

`focus-reveal-geometry.patch` adds a first-party openQ4 exact projected border-quad query, distributed under the same MIT license. It uses the existing accumulated transform and depth-plane conventions, rejects unavailable/nonfinite/nonpositive-w projections without changing output, and leaves `GetBoundingBox` unchanged. It does not clip the quad or guarantee that authored scroll ranges can reveal all ink. Callers must refresh geometry after layout before querying.

The separate existing `projection-geometry.patch` supplies that layout/transform refresh without rendering. `positioned-overflow.patch` retains positioned descendants in authored scroll extents. The new helper does not change either patch's semantics or add scroll range.
