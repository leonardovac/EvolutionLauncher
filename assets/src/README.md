# Ornament sources

The PNGs in `assets/` are baked from these SVGs. Keep both in step.

| source | output | size |
|---|---|---|
| `title-leaf.svg` | `assets/soulframe-leaf.png` | 232x195 |
| `button-primary-end-cap.svg` | `assets/soulframe-endcap.png` | 184x220 |

Both are 4x their design size, matching how the 128x128 rail marks oversample for high DPI.

Unlike the rail marks, these are **not** flattened to a white alpha mask. They are two-tone and
are drawn full-colour, because the dark outline is what makes them legible over bright hero art.
Flattening them would erase it.

To re-bake, render each SVG onto a transparent surface at the size above and save as RGBA PNG.
Any rasteriser will do; note that `cairosvg` needs a native `libcairo-2.dll` that is not present
on a stock Windows box, so Inkscape or a browser canvas is usually the shorter path.

A correct bake reports alpha extrema `(0, 255)` and roughly 22300 opaque pixels for the leaf,
11900 for the end cap.
