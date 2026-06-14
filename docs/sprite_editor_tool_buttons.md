# Sprite Editor Tool Button Template

This document lists the drawing-tool buttons in the sprite editor and provides
dimensions for designing replacement icons.

Source: `src/editor/panels/sprite_editor_panel.cpp`

## Shared Button Dimensions

| Property | Dimension |
|---|---:|
| Clickable button | 42 x 38 px |
| Glyph area inside button | 34 x 32 px |
| Recommended icon source canvas | 32 x 32 px |
| Recommended visible artwork area | 26 x 26 px |
| Horizontal glyph inset | 4 px |
| Vertical glyph inset | 3 px |
| Space between buttons | 6 px |
| Toolbar layout | 2 columns |
| Left editor rail width | 220 px |

Create each icon on a transparent `32 x 32 px` canvas. Keep the visible design
inside the centered `26 x 26 px` safe area. Tool names and shortcuts are shown
in hover tooltips.

For pixel-art icons, use hard edges, whole-pixel coordinates, and no
anti-aliasing. A `24 x 24 px` visible icon is also suitable when a tool needs
more surrounding space.

## Tool Button List

All tools use the same `42 x 38 px` clickable area and `32 x 32 px` icon source
template.

| Order | Tool | Key | Suggested icon concept | Icon file name |
|---:|---|:---:|---|---|
| 1 | Pen | `P` | Diagonal pencil with a visible tip | `tool_pen.png` |
| 2 | Mirror | `Y` | Pencil or shape reflected across a vertical axis | `tool_mirror.png` |
| 3 | Bucket | `B` | Tilted paint bucket with a paint drop | `tool_bucket.png` |
| 4 | Eraser | `E` | Angled block eraser | `tool_eraser.png` |
| 5 | Stroke | `D` | Thick horizontal stroke with round endpoints | `tool_stroke.png` |
| 6 | Line | `L` | Diagonal line with endpoint handles | `tool_line.png` |
| 7 | Rect | `R` | Rectangle outline | `tool_rect.png` |
| 8 | Circle | `C` | Circle outline | `tool_circle.png` |
| 9 | Polygon | `G` | Triangle or polygon outline with vertex handles | `tool_polygon.png` |
| 10 | Move | `M` | Four-direction arrow | `tool_move.png` |
| 11 | Select | `S` | Selection rectangle with corner handles | `tool_select.png` |
| 12 | Picker | `I` | Eyedropper | `tool_picker.png` |
| 13 | Shade | `H` | Shaded triangle or light-to-dark ramp | `tool_shade.png` |

## Mouse Controls

- Left-click draws or applies the selected tool.
- Right-click erases with pen, mirror, stroke, line, rectangle, circle, bucket,
  and shade tools.
- Right-click with Picker selects the secondary colour.
- Right-click while creating a polygon cancels the polygon.

## Grid Arrangement

```text
┌────────────┬────────────┐
│ Pen        │ Mirror     │
│ Bucket     │ Eraser     │
│ Stroke     │ Line       │
│ Rect       │ Circle     │
│ Polygon    │ Move       │
│ Select     │ Picker     │
│ Shade      │            │
└────────────┴────────────┘
```

Each cell is `42 x 38 px`, with a `6 px` gap between cells and rows.

## Colour States

| State | Button background | Icon |
|---|---|---|
| Normal | RGB `56, 61, 67` | RGB `240, 242, 245` |
| Selected | RGB `236, 203, 49` | RGB `24, 25, 28` |

Replacement icons should be single-colour masks or shapes so the editor can
apply the normal and selected icon colours consistently. Avoid baking the
button background into the icon image.

## Blank Icon Template

```text
32 x 32 px transparent image

┌──────────────────────────────┐
│                              │  3 px top margin
│  ┌────────────────────────┐  │
│  │                        │  │
│  │   26 x 26 px artwork   │  │
│  │       safe area        │  │
│  │                        │  │
│  └────────────────────────┘  │
│                              │  3 px bottom margin
└──────────────────────────────┘
   3 px                    3 px
```

The safe area is a design guide rather than a crop boundary. Narrow icons such
as the pen and picker can use less width, while the move and select tools can
use the full safe area.
