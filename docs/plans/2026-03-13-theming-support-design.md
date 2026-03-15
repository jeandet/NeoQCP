# QCPTheme: Theming Support for NeoQCP

## Goal

Add a theme system so plot colors (background, axes, grids, legend, selection) can be set via Qt Style Sheets (QSS) targeting `QCustomPlot` properties. Primary use case: applying dark palettes from SciQLop down to the plots.

## Design

### QCPTheme class

New `QCPTheme` class (`src/theme.h`, `src/theme.cpp`) — a `QObject` with `Q_PROPERTY`s for each semantic color role:

| Property | Controls | Light default | Dark example |
|---|---|---|---|
| `background` | Widget/plot area fill | `#ffffff` | `#1e1e1e` |
| `foreground` | Axis lines, ticks, sub-ticks, tick labels, axis labels, title text, legend text | `#000000` | `#e0e0e0` |
| `grid` | Major grid lines, zero line | `#c8c8c8` | `#3a3a3a` |
| `subGrid` | Minor grid lines | `#dcdcdc` | `#2a2a2a` |
| `selection` | Selection highlight, selected elements, selection decorator, selection rect | `#0000ff` | `#4a9eff` |
| `legendBackground` | Legend fill | `#ffffff` | `#2a2a2a` |
| `legendBorder` | Legend border pen | `#000000` | `#555555` |

Each setter emits `changed()`. Multiple rapid property changes (e.g. QSS applying 7 properties at once) are coalesced: `changed()` triggers a deferred `applyTheme()` via `QTimer::singleShot(0, ...)` with a dirty flag, so only one replot occurs per event loop cycle.

Static factories: `QCPTheme::light(parent)`, `QCPTheme::dark(parent)`. These return new heap-allocated instances; the caller (or the plot via `setTheme()`) takes ownership. Default constructor produces the current light defaults (backward compatible).

### Integration with QCustomPlot

`QCustomPlot` creates a default `QCPTheme` (`mOwnedTheme`, parented to `this`) and points `mTheme` at it. For external shared themes, `mTheme` is a `QPointer<QCPTheme>` — the plot does not own externally-provided themes; the caller retains ownership.

**QSS proxy properties** on `QCustomPlot` delegate to `mTheme`:
- `themeBackground`, `themeForeground`, `themeGrid`, `themeSubGrid`, `themeSelection`, `themeLegendBackground`, `themeLegendBorder`

Each proxy setter calls the corresponding `QCPTheme` setter, which emits `changed()`, which triggers the coalesced `applyTheme()`.

**QSS usage:**
```css
QCustomPlot {
    qproperty-themeBackground: #1e1e1e;
    qproperty-themeForeground: #e0e0e0;
    qproperty-themeGrid: #3a3a3a;
    qproperty-themeSubGrid: #2a2a2a;
    qproperty-themeSelection: #4a9eff;
    qproperty-themeLegendBackground: #2a2a2a;
    qproperty-themeLegendBorder: #555555;
}
```

**`setTheme(QCPTheme*)`** — disconnects old theme's `changed()`, connects new one, calls `applyTheme()`. If `nullptr` is passed, reverts to `mOwnedTheme`. Enables sharing one theme across multiple plots.

**`applyTheme()`** — walks all owned elements and applies colors. **Only pen colors are changed; pen style, width, and cap are preserved** (i.e. `pen.setColor(...)` on existing pens, not `setPen(QPen(color))`):
- `mBackgroundBrush` color ← `background`
- All `QCPAxis`: base pen, tick pen, sub-tick pen color ← `foreground`; label color, tick label color ← `foreground`
- All `QCPGrid`: grid pen color ← `grid`; sub-grid pen color ← `subGrid`; zero line pen color ← `grid`
- `QCPLegend`: brush ← `legendBackground`; border pen color ← `legendBorder`; text color ← `foreground`
- `QCPTextElement` (titles): text color ← `foreground`
- `QCPColorScale` internal axis: same as regular axes
- Selection rect pen color ← `selection`; selection decorators ← `selection`
- Triggers `replot()`

**Axis rect background:** `QCPAxisRect` defaults to `Qt::NoBrush`, so the widget background shows through. No separate role needed.

**Export paths:** since `applyTheme()` modifies element properties directly (pens, brushes, colors), PDF/SVG/pixmap exports naturally use the themed colors without additional work.

### Color precedence

Theme is "last write wins" for the roles it covers. Per-element setters still work after theme application but will be overwritten on next theme change. This keeps the mental model simple.

### Backward compatibility

- Default `QCPTheme` uses today's hardcoded values — existing code unchanged
- Individual element constructors keep their current defaults
- No API removed

## File changes

### New files
- `src/theme.h` / `src/theme.cpp` — `QCPTheme` class

### Modified files
- `src/core.h` / `src/core.cpp` — `mTheme`, `mOwnedTheme`, proxy `Q_PROPERTY`s, `setTheme()`, `applyTheme()`
- `src/qcp.h` — `#include "theme.h"`
- `meson.build` — add new source files

## Out of scope
- SciQLopPlots / SciQLop changes (downstream consumers)
- Polar-specific theme roles (polar axes reuse `foreground`/`grid` via `applyTheme()`)
- Changes to individual element constructors
- **Items** (`QCPItemText`, `QCPItemLine`, etc.) — items represent user-added annotations with intentional colors; theming them would overwrite user choices. Can be added as a future role if needed.
- **Plottable data colors** (graph pen, bar brush, etc.) — these are data-encoding colors (often from a palette), not chrome. Theming them would conflict with per-series color assignment.
- **Dynamic theme changes during active interaction** (e.g. mid-drag) — may cause momentary visual inconsistency; not worth engineering around.

## Tests

New test in `tests/auto/` covering:
- Default theme matches current hardcoded colors
- `applyTheme()` propagates to axes, grid (including zero line), legend, text elements, color scale
- Pen style/width preserved after theme application
- Theme sharing: one `QCPTheme` across two `QCustomPlot` instances
- Per-element override works after theme application
- QSS proxy properties work (set via `setProperty()`, verify propagation)
- `QCPTheme::dark()` factory produces expected colors
- Coalescing: setting multiple properties triggers only one replot
