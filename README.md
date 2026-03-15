# NeoQCP, a QCustomPlot Modernized Fork
[![Tests](https://github.com/SciQLop/NeoQCP/actions/workflows/tests.yml/badge.svg)](https://github.com/SciQLop/NeoQCP/actions/workflows/tests.yml)

[http://www.qcustomplot.com/images/logo.png](http://www.qcustomplot.com/images/logo.png)

This fork aims at modernizing the excellent [QCustomPlot](https://www.qcustomplot.com/) library with:

## ✨ Key Improvements

- **Qt6 Exclusive Support**
  Dropped legacy Qt4/Qt5 support - focused on Qt6 only to reduce complexity and leverage modern Qt features

- **QRhi Rendering Backend**
  Replaced the QWidget + OpenGL FBO rendering path with QRhiWidget. Qt's Rendering Hardware Interface maps to the native graphics API on each platform (Metal on macOS, Vulkan on Linux, D3D on Windows). Layer textures are composited directly on the GPU with no GPU-to-CPU readback.

- **GitHub Actions CI**
  Integrated GitHub Actions for continuous integration, ensuring code quality and build stability across platforms

- **Modern Build System**
  Replaced qmake with [Meson](https://mesonbuild.com/) for faster, more reliable builds

- **[Tracy](https://github.com/wolfpld/tracy) Profiler Integration**
  Integrated Tracy for real-time performance profiling, allowing you to visualize and optimize your plotting code and
  NeoQCP itself.

- **Zero-Copy Data Source (QCPGraph2)**
  New plottable that plots directly from user-owned containers without copying or converting data. Supports `std::vector`, `QVector`, `std::span`, and raw pointers with any numeric type (`double`, `float`, `int`). Uses C++20 concepts and SoA layout — algorithms run in the container's native type and only cast to `double` at the final pixel-coordinate step. Supports all line styles from QCPGraph (line, step-left/right/center, impulse), scatter symbols, and scatter skip.

  ```cpp
  // Owning — move your data in, no copy
  std::vector<double> keys = /* ... */;
  std::vector<float> values = /* ... */;
  graph->setData(std::move(keys), std::move(values));

  // Zero-copy view — plot directly from your buffer
  graph->viewData(keyPtr, valuePtr, count);

  // Shared — multiple graphs, one data source
  auto src = std::make_shared<QCPSoADataSource<std::vector<double>, std::vector<float>>>(
      std::move(keys), std::move(values));
  graph1->setDataSource(src);
  graph2->setDataSource(src);

  // Line styles and scatter symbols
  graph->setLineStyle(QCPGraph2::lsStepCenter);
  graph->setScatterStyle(QCPScatterStyle(QCPScatterStyle::ssCircle, 8));
  graph->setScatterSkip(2);  // draw every 3rd scatter point
  ```

  **QCPGraph2 vs QCPGraph:**

  | Feature | QCPGraph | QCPGraph2 |
  |---------|----------|-----------|
  | Data source | `QCPDataContainer` (copies data) | Zero-copy pluggable `QCPAbstractDataSource` |
  | Container types | `double` only | Any numeric type (`float`, `int`, etc.) |
  | Line styles | All 6 | All 6 |
  | Scatter symbols | All 17+ shapes | All 17+ shapes |
  | Fill under graph | Yes | No |
  | Channel fill | Yes | No |
  | `addData()` incremental | Yes | Not yet |
  | Selection decoration | Full (pen + scatter) | Basic |

- **GPU Plottable Rendering**
  QCPGraph and QCPCurve solid-line strokes and baseline fills rendered via QRhi shaders with polyline extrusion and 4x MSAA antialiasing.

- **Zero-Copy Colormap (QCPColorMap2)**
  New colormap plottable with async resampling and zero-copy data sources. Data is resampled on a background thread with request coalescing — only the latest viewport matters, so rapid zooming/panning stays fluid. Supports gap detection and logarithmic Y axes.

  ```cpp
  auto* cm = new QCPColorMap2(xAxis, yAxis);

  // Owning — move your data in
  cm->setData(std::move(xVec), std::move(yVec), std::move(zVec));

  // Zero-copy view — plot directly from your buffer
  cm->viewData(xSpan, ySpan, zSpan);
  ```

- **Theming**
  `QCPTheme` with named color roles (background, foreground, grid, sub-grid, selection, legend). Ships with `QCPTheme::light()` and `QCPTheme::dark()` factories. A shared theme can be applied to multiple plots and live-updated — all axes, grids, legends, ticks, labels, and items react automatically. Theme colors are exposed as Qt properties for QSS integration.

  ```cpp
  auto* theme = QCPTheme::dark();
  plot->setTheme(theme);  // applies and connects for live updates
  ```

  Theme colors are exposed as Qt properties on `QCustomPlot`, so you can drive them entirely from QSS:

  ```css
  /* Dark theme via stylesheet */
  QCustomPlot {
      qproperty-themeBackground:       #1e1e1e;
      qproperty-themeForeground:       #d4d4d4;
      qproperty-themeGrid:             #3c3c3c;
      qproperty-themeSubGrid:          #2a2a2a;
      qproperty-themeSelection:        #264f78;
      qproperty-themeLegendBackground: #252526;
      qproperty-themeLegendBorder:     #3c3c3c;
  }

  /* Light theme via stylesheet */
  QCustomPlot[objectName="lightPlot"] {
      qproperty-themeBackground:       #ffffff;
      qproperty-themeForeground:       #1e1e1e;
      qproperty-themeGrid:             #d0d0d0;
      qproperty-themeSubGrid:          #e8e8e8;
      qproperty-themeSelection:        #0078d4;
      qproperty-themeLegendBackground: #f5f5f5;
      qproperty-themeLegendBorder:     #d0d0d0;
  }
  ```

  This lets you switch plot themes along with your application stylesheet — no C++ theme code needed.

- **Bug Fixes**
  Patched critical issues from upstream:
    - Fixed DPI scaling issues on macOS and Windows

- **Planned Features**
    - Incremental `addData()` for QCPGraph2
    - Fill support for QCPGraph2 (under graph / channel fill)
    - C++ modernization

## 📥 Installation

### Prerequisites

- Qt 6.7+ (Core, Gui, Widgets, Svg, PrintSupport modules)
- Meson 1.1.0+
- Ninja build system
- C++20 compatible compiler

### Build Steps

```bash
git clone https://github.com/jeandet/NeoQCP.git
cd NeoQCP
meson setup build --buildtype=release
meson compile -C build
```

### Integration in Your Meson Project

Create a `subprojects/NeoQCP.wrap` file with the following content:

```meson
[wrap-git]
url = https://github.com/SciQLop/NeoQCP.git
revision = HEAD
depth = 1

[provide]
NeoQCP = NeoQCP_dep

```

Then, in your `meson.build` file, add:

```meson
project('my-plot-app', 'cpp',
        default_options: ['cpp_std=c++20'])

qtmod = import('qt6')
qtdeps = dependency('qt6', modules : ['Core','Widgets','Gui','Svg','PrintSupport'], method:'qmake')

neoqcp = dependency('NeoQCP')

executable('myapp',
           'main.cpp',
           dependencies: [qtdeps, neoqcp],
           gui_app: true)
```

## 🆚 Migration from QCustomPlot

TBD

## 🧪 Testing

Run test suite with:

```bash
meson setup builddir
meson test -C builddir
```

## 🤝 Contributing

We welcome contributions! Please follow these guidelines:

1. Fork the repository
2. Create a new branch for your feature or bug fix
3. Write tests for new functionality or bug fixes
4. Submit pull request with description of changes

## 📄 License

GNU GPL v3 (same as upstream QCustomPlot)

---

**Report Issues**: [GitHub Issues](https://github.com/SciQLop/NeoQCP/issues)
