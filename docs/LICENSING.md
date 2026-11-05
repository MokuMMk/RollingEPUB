# Licensing

RollingEPUB is released under the **MIT License** (see
[`licenses/MIT.txt`](../licenses/MIT.txt)). It consolidates code from two
projects with compatible permissive licenses.

## Provenance summary

| Source | License | Used for |
| --- | --- | --- |
| CrossSIGO / CrossMux | MIT | CSS model, hyphenation (algorithm + EN patterns), entity table, OPF/NCX/NAV structure, page model concepts, image dithering concepts, ZIP conventions |
| Read Pico / kiikoread | Apache-2.0 (only Apache-2.0 files) | ZIP access policy, OPF/spine/cover pipeline, HTML→blocks state machine, footnote/anchor extraction, CJK/vertical pagination, TTF glyph cache concepts, image header probing |
| miniz | MIT | ZIP inflate / central directory |
| stb_image, stb_truetype | Public domain / MIT | image decode, TTF rasterization |
| Noto Sans subset (tests only) | SIL OFL 1.1 | TTF rendering tests/demo |

Every adapted source file carries an SPDX identifier and a `Provenance:` comment
naming the original project and file. The full attributions are in
[`NOTICE`](../NOTICE).

## Apache-2.0 compliance

Apache-2.0 code is used inside an MIT-licensed work. The Apache-2.0 license is
compatible with MIT (MIT is a permissive license with no copyleft), and the
obligations are:

1. A copy of the Apache-2.0 license is distributed:
   [`licenses/Apache-2.0.txt`](../licenses/Apache-2.0.txt).
2. Copyright / patent / attribution notices are preserved:
   see `NOTICE` and the headers of the adapted files.
3. Modified files carry a statement of modification: each adapted file says it
   was "adapted from" the original, which is the recorded change notice.
4. Trademark rules are respected (no use of licensor marks to endorse).

If you redistribute RollingEPUB, keep `NOTICE`, `licenses/` and the SPDX headers
intact. You may relicense your own additions under MIT/Apache-2.0-compatible
terms; you cannot remove the upstream notices.

## What is NOT used

- Read Pico files marked
  `SPDX-License-Identifier: LicenseRef-Kiikoread-Personal-Study-1.0`
  (non-commercial). No code from those files was copied. Only Apache-2.0
  headers/files were reused, and the pipeline was reimplemented in C++ where a
  direct port would have mixed license scopes.
- CrossMux assets/fonts/third-party binaries (only the MIT source modules were
  used).
- Any DRM circumvention code (only `encryption.xml` presence detection).

## Third-party license texts

- [`licenses/MIT.txt`](../licenses/MIT.txt) — RollingEPUB MIT text
- [`licenses/CrossMux-MIT.txt`](../licenses/CrossMux-MIT.txt) — CrossMux MIT text
- [`licenses/Apache-2.0.txt`](../licenses/Apache-2.0.txt) — Apache-2.0
- [`licenses/OFL-Noto.txt`](../licenses/OFL-Noto.txt) — SIL OFL for the test font
- miniz and stb licenses are embedded in their source headers
  (`third_party/miniz.c`, `third_party/stb_image.h`,
  `third_party/stb_truetype.h`).

## Commercial use

Because only MIT and Apache-2.0 code is reused, RollingEPUB can be used in
commercial products provided the notices are preserved. If you later import code
from the personal-study portions of Read Pico, that is your responsibility and
requires the rightsholder's permission; do not assume the MIT/Apache-2.0
licenses cover it.
