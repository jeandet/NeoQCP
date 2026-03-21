# Item Creation Mode — Design Spec

Interactive item creation for NeoQCP. Users draw items directly on the plot via click-move-click gestures.

## Motivation

SciQLop's event catalog workflow currently requires clicking a button to add a span in the middle of the view, then manually editing it. Drawing the range directly on the plot is more natural and faster.

## Interaction Model

Two trigger modes, same gesture:

### Quick Creation (modifier key)

1. Hold Shift (configurable) + click on plot
2. Move mouse — live preview follows cursor
3. Click again to commit
4. Returns to normal interaction

### Batch Creation (mode toggle)

1. SciQLop toggles creation mode ON via API
2. Click to start, move, click to commit
3. Stays in mode — immediately ready for next item
4. Toggled OFF explicitly by SciQLop

### Cancellation

- Escape key: cancels in-progress creation, deletes the preview item
- Right-click: same as Escape

### Visual Feedback (creation mode only)

- Cursor changes to crosshair over the axis rect
- Small badge in bottom-right corner of axis rect: "Create" label

Quick creation (modifier) does not show the badge — the held Shift key is sufficient indication.

## State Machine

```
Idle ──(shift+click / click in mode)──▶ Drawing
  │
  ├── mouse move → update item (right edge for VSpan, etc.)
  ├── click → commit: emit itemCreated, replot
  ├── Escape → cancel: delete item, emit itemCanceled
  └── right-click → cancel: delete item, emit itemCanceled

After commit/cancel:
  modifier trigger → back to Idle, creation ends (normal interaction resumes)
  toggle mode → back to Idle, but creation mode stays active (next click starts a new item)
```

## API

All new API lives on `QCustomPlot`.

### Types

```cpp
using ItemCreator = std::function<QCPAbstractItem*(QCustomPlot* plot, QCPAxis* keyAxis, QCPAxis* valueAxis)>;
```

The callback creates and returns a new item. NeoQCP handles positioning it during the drag. The `keyAxis` and `valueAxis` are the default axes of the axis rect where the click occurred: `axisRect->axis(QCPAxis::atBottom)` for key and `axisRect->axis(QCPAxis::atLeft)` for value. If the axis rect has a different primary orientation, the first axis of each type is used.

### Methods

```cpp
void setItemCreator(ItemCreator creator);   // nullptr disables creation
ItemCreator itemCreator() const;

void setCreationModeEnabled(bool enabled);  // toggle batch mode
bool creationModeEnabled() const;

void setCreationModifier(Qt::KeyboardModifier mod);  // default: Qt::ShiftModifier
Qt::KeyboardModifier creationModifier() const;
```

### Signals

```cpp
void itemCreated(QCPAbstractItem* item);  // item committed
void itemCanceled();                       // creation aborted
```

### Item Positioning During Drag

NeoQCP positions the item automatically during the click-move-click gesture. The logic inspects the item's named positions:

- **VSpan** (`lowerEdge`, `upperEdge`): first click sets `lowerEdge` key coord, mouse move updates `upperEdge` key coord
- **HSpan** (`lowerEdge`, `upperEdge`): first click sets `lowerEdge` value coord, mouse move updates `upperEdge` value coord
- **RSpan** (`leftEdge`, `rightEdge`, `topEdge`, `bottomEdge`): first click sets `leftEdge`+`topEdge`, mouse move updates `rightEdge`+`bottomEdge`

Detection: check which named positions exist on the item (via `QCPAbstractItem::positions()`). The position names (`lowerEdge`/`upperEdge` for VSpan/HSpan, `leftEdge`/`rightEdge`/`topEdge`/`bottomEdge` for RSpan) determine the update strategy.

Fallback for unknown items: if the item has exactly 2 positions, treat the first as the anchor and the second as the tracking point (set both to `ptPlotCoords`).

## Implementation

### New Files

| File | Description |
|---|---|
| `src/item-creation-state.h` | `QCPItemCreationState` — state machine, holds in-progress item |
| `src/item-creation-state.cpp` | State machine implementation |

### Modified Files

| File | Change |
|---|---|
| `src/core.h` | Add `ItemCreator`, creation mode members, signals, new methods |
| `src/core.cpp` | Wire mouse/key events through creation state machine |
| `src/qcp.h` | Add `#include "item-creation-state.h"` |
| `meson.build` | Add new source files |

### QCPItemCreationState

Internal class (not exported as public API). Owned by QCustomPlot.

```cpp
class QCPItemCreationState : public QObject {
    Q_OBJECT
public:
    enum State { Idle, Drawing };

    explicit QCPItemCreationState(QCustomPlot* plot);

    State state() const;

    // Returns true if event was consumed
    bool handleMousePress(QMouseEvent* event);
    bool handleMouseMove(QMouseEvent* event);
    bool handleKeyPress(QKeyEvent* event);

    void cancel();  // programmatic cancel

signals:
    void itemCreated(QCPAbstractItem* item);
    void itemCanceled();

private:
    QCustomPlot* mPlot;
    State mState = Idle;
    QCPAbstractItem* mCurrentItem = nullptr;  // in-progress item, owned by QCustomPlot
    QCPAxis* mKeyAxis = nullptr;
    QCPAxis* mValueAxis = nullptr;

    void commitItem();
    void cancelItem();
    void updateItemPosition(const QPointF& pixelPos);
    QCPAxisRect* axisRectAt(const QPointF& pos) const;
};
```

### Event Flow in QCustomPlot

The creation state machine intercepts events before the normal dispatch. Priority order: creation state machine > selection rect > normal layerable dispatch.

1. `mousePressEvent`: if state is `Drawing` (second click to commit), forward to `handleMousePress()` unconditionally. If state is `Idle` and creation modifier held or creation mode enabled, and `ItemCreator` is set → forward to `handleMousePress()`. If consumed, skip selection rect and normal event dispatch.
2. `mouseMoveEvent`: if state is `Drawing` → forward to `handleMouseMove()`, trigger `replot(rpQueuedReplot)` for live preview. If consumed, skip normal dispatch.
3. `keyPressEvent`: if Escape pressed and state is `Drawing` → forward to `handleKeyPress()`.
4. Context menu (right-click): if state is `Drawing` → cancel, consume event.

In batch mode (creation mode toggled ON), clicking on an existing item while in `Idle` state starts a new creation rather than interacting with the existing item. To adjust existing items, the user must exit creation mode first. This avoids ambiguity about click intent.

### Badge Rendering

When creation mode is enabled (toggle mode), each `QCPAxisRect` draws a small "Create" badge in its bottom-right corner during `draw()`. This is done via a check on `parentPlot()->creationModeEnabled()` in `QCPAxisRect::draw()`.

The badge is purely decorative — not a layerable, not interactive.

## Usage Example

```cpp
// SciQLop sets up VSpan creation
plot->setItemCreator([](QCustomPlot* p, QCPAxis* keyAxis, QCPAxis* valueAxis) {
    auto* span = new QCPItemVSpan(p);
    span->setPen(QPen(QColor(100, 149, 237, 200)));
    span->setBrush(QBrush(QColor(100, 149, 237, 50)));
    return span;
});

// React to committed items
connect(plot, &QCustomPlot::itemCreated, [this](QCPAbstractItem* item) {
    if (auto* vspan = qobject_cast<QCPItemVSpan*>(item)) {
        catalog->addEvent(vspan->range());
    }
});

// Toolbar button toggles batch mode
connect(createButton, &QPushButton::toggled,
        plot, &QCustomPlot::setCreationModeEnabled);
```

## Scope

**In scope:** Creation state machine, callback API, modifier + toggle mode, cursor/badge feedback, signals, generic position-based item updating.

**Out of scope:** Item editing/resizing after creation (already handled by span items' own mouse handling), metadata popups, catalog selection UI, multi-plot synchronization (SciQLop concern), undo/redo.

## Testing

- **State machine unit tests:** Idle→Drawing→commit, Idle→Drawing→cancel (Escape), Idle→Drawing→cancel (right-click)
- **VSpan creation:** simulate click-move-click, verify span range matches click coordinates
- **HSpan creation:** same, verify value-axis range
- **RSpan creation:** verify both key and value ranges
- **Modifier trigger:** verify creation only starts when modifier held, normal interaction otherwise
- **Mode toggle:** verify stays in creation mode after commit, exits on toggle off
- **No creator set:** verify modifier+click does nothing
- **Fallback positioning:** create item with 2 generic positions, verify both get set
- **Cancel cleans up:** verify item is removed from plot on cancel (via `removeItem()`)
- **Priority over selection rect:** verify creation intercepts before zoom/select rect
- **Batch mode ignores existing items:** click on existing item in batch mode starts new creation
- **Multi-axis-rect:** creation in a subplot, verify item is associated with correct axis rect
