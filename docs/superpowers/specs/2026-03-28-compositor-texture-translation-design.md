# Compositor-Level Texture Translation (Phase B)

## Problem

During pan, `markAffectedLayersDirty()` marks the "main" layer dirty. `replot()` then calls `drawToPaintBuffer()` — every plottable re-rasterizes into the CPU staging QImage. But Graph2/MultiGraph already have cached line geometry that just needs a pixel offset. The staging buffer repaint is wasted work.

On macOS Retina (4x pixels), this is especially costly: a 1920x1080 widget produces a 3840x2160 staging buffer (33MB ARGB32). Skipping the clear + upload saves ~66MB of memory bandwidth per frame.

## Design

### Core Idea

Skip the CPU staging buffer repaint for data layers when all plottables report a valid GPU translation offset. The compositor already shifts the old texture by `pixelOffset()`. The plottable RHI layer is cleared (no stale geometry rendered). Everything comes from the offset texture until a full repaint triggers.

### Skip Eligibility

A layer's paint buffer repaint is skipped when ALL of:

1. **`layer->pixelOffset()` is non-null** — all plottable children agree on a translation offset (same axis rect, all return valid `stallPixelOffset()`)
2. **No `QCPAbstractItem` children** on the layer — items default to "main" and their anchor types vary (data coords, pixel coords, other items), so they can't be reliably GPU-translated
3. **Buffer is not `invalidated()`** — only `contentDirty` (from pan), not a structural change (resize, reallocation, layer mode change)

### Modified `replot()` Flow

Today:
```
for each layer:
    if buffer dirty:
        drawToPaintBuffer()    // CPU repaint — always runs
```

After:
```
for each layer:
    if buffer dirty AND NOT skip-eligible:
        drawToPaintBuffer()    // CPU repaint — skipped on pan frames
```

The plottable RHI layer clearing (before the draw loop) is unchanged — it clears all dirty-buffered RHI layers, including skip-eligible ones. This ensures stale GPU geometry is not rendered.

### Frame-by-Frame Behavior

**Frame N (full repaint):**
- `drawToPaintBuffer()` runs, QPainter rasterizes all plottables
- `donePainting()` sets `mNeedsUpload = true`
- `render()` uploads staging image to GPU texture, clears `mNeedsUpload`

**Frame N+1..K (pan, skip eligible):**
- `markAffectedLayersDirty()` marks main buffer `contentDirty`
- `replot()`: main layer is skip-eligible, skip `drawToPaintBuffer()`
- `donePainting()` never called, `mNeedsUpload` stays `false`
- Plottable RHI layer was cleared (buffer was dirty), `hasGeometry()` = false
- `render()`: no texture upload, old texture still in GPU memory
- `executeRenderPass()`: composites old texture with current `pixelOffset()`
- `contentDirty` cleared as usual

**Frame N+K+1 (full repaint trigger):**
- Cache bounds exceeded / async data arrives / zoom / resize
- `stallPixelOffset()` returns `{}`, skip check fails, normal full repaint

### Visual Quality

During skip frames, the visual comes entirely from the paint buffer texture (QPainter-rendered, offset):
- Lines at QPainter quality instead of GPU-extruded — indistinguishable at interactive frame rates
- Scatters, fills, decorations — all in the texture, all correctly offset
- No flickering: transition from offset texture to fresh repaint is seamless (both produce the same visual at the current viewport)

### Performance Saved Per Skip Frame

- QImage staging buffer clear (memset, DPR^2-sized)
- All plottable `draw()` calls (adaptive sampling, line cache eval, GPU geometry extrusion + upload)
- Staging image GPU texture upload (DMA or copy, DPR^2-sized)

### Edge Cases

| Case | Behavior |
|------|----------|
| Mixed Graph + Graph2 on same layer | Graph's `stallPixelOffset()` returns `{}` -> skip fails |
| Items on "main" layer | Item child detected -> skip fails |
| QCPColorMap2 on same layer | `stallPixelOffset()` returns `{}` -> skip fails |
| Multiple axis rects, plottables disagree | `pixelOffset()` returns `{}` -> skip fails |
| `ensureAtLeastOneBufferDirty()` fallback | Grid/axes buffer already dirty -> returns early, no interference |
| Resize during pan | `invalidated()` = true -> skip fails |
| Empty layer (no plottables) | `pixelOffset()` returns `{}` -> skip fails |
| Zoom (not pan) | `evaluateLineCache()` detects ratio change -> `stallPixelOffset()` returns `{}` -> skip fails |

## Implementation

### Files Changed

1. **`src/layer.h/.cpp`** — Add `bool canSkipRepaintForTranslation() const` that encapsulates the eligibility check (pixelOffset non-null, no item children, buffer not invalidated)
2. **`src/core.cpp`** — Gate `drawToPaintBuffer()` behind the skip check in the `replot()` draw loop

### `canSkipRepaintForTranslation()` Logic

```cpp
bool QCPLayer::canSkipRepaintForTranslation() const
{
    auto pb = mPaintBuffer.toStrongRef();
    if (!pb || pb->invalidated())
        return false;

    if (pixelOffset().isNull())
        return false;

    for (auto* child : mChildren)
    {
        if (qobject_cast<QCPAbstractItem*>(child))
            return false;
    }
    return true;
}
```

### `replot()` Change

```cpp
// Before:
if (pb && pb->contentDirty())
    layer->drawToPaintBuffer();

// After:
if (pb && pb->contentDirty() && !layer->canSkipRepaintForTranslation())
    layer->drawToPaintBuffer();
```

### Testing

- Unit test: create QCustomPlot with QCPGraph2, pan via `setRange()`, verify that the main layer's paint buffer `mNeedsUpload` stays false on pan frames (skip worked)
- Unit test: add a QCPItemLine to the main layer, verify skip is disabled (falls back to full repaint)
- Unit test: mix QCPGraph + QCPGraph2 on same layer, verify skip is disabled
- Visual/manual test: verify no flickering during pan, correct rendering after pan stops
