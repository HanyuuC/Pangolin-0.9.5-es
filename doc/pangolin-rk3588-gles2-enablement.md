# 适配 RK3588 Mali GLES2：启用 Pangolin 已有的 OpenGL ES 路径

## Context（背景）

在 RK3588 (ARM64, Mali-G610 GPU) 上运行 Pangolin 的 `SimpleDisplay` 时：

- 直接运行 `DISPLAY=:0 ./SimpleDisplay` 报 `eglBindAPI(0x30a2) failed: EGL_BAD_PARAMETER (300c)`。原因是 `display_x11.cpp:159` 绑定桌面 `EGL_OPENGL_API`，而 Mali EGL 驱动只支持 OpenGL ES。
- 临时方案 `LD_LIBRARY_PATH=/lib/aarch64-linux-gnu ./SimpleDisplay` 可运行，但走的是 Mesa LLVMpipe **软件渲染**，所以"比较卡"。

排查文档（`doc/0.1 Pangolin RK3588 EGL 初始化失败.md`）曾悲观估计"需要全面适配固定管线，工作量较大"。但实际代码审查发现：**Pangolin 已内置完整的 OpenGL ES 2 兼容层**（Emscripten/Android 一直在用），只是从未对 Linux 开启。本方案的目标是**启用这条已有 GLES 路径**，让 RK3588 直接使用 Mali 硬件加速的 `libGLESv2`，从而消除软件渲染卡顿。

### 兼容性策略（已与用户确认）

采用**构建时切换**，不做单一二进制运行时双支持。即：同一份源码通过 CMake 选项 `PANGOLIN_USE_GLES2` 在桌面 GL 与 GLES2 间切换：

- Windows 开发机（`msvc-build` preset）：`PANGOLIN_USE_GLES2=OFF`（默认），桌面 GL 行为完全不变。
- RK3588（`linux-arm64-build` preset）：`PANGOLIN_USE_GLES2=ON`，走 GLES2 + Mali 硬件加速。

源码两侧通过既有的 `HAVE_GLES` / `HAVE_GLES_2` 守卫同时维护、互不影响。用户本就在两个平台分别构建，无需单一二进制运行时双支持。

## 已有可复用的 ES 基础设施（无需新写）

- `components/pango_opengl/include/pangolin/gl/compat/gl2engine.h` — 在 GLES2 shader 之上模拟固定管线（`glColor4f`/`glMatrixMode`/`glPushMatrix`/`glLoadMatrixf`/`glVertexPointer`/`glTranslatef`/`glOrtho` 等），`class GlEngine` 持有 shader program。
- `components/pango_opengl/include/pangolin/gl/compat/gl_es_compat.h` — 宏别名（`glColor3f→glColor4f`、`glClearDepth→glClearDepthf`、`GLdouble→GLfloat` 等），并 include gl2engine.h。
- `components/pango_opengl/src/compat/gl2engine.cpp` — `glEngine()` 单例定义（当前仅 EMSCRIPTEN 编译）。
- 全仓库已布满 `#ifndef HAVE_GLES` / `#ifdef HAVE_GLES_2` 守卫（gldraw、view、widgets、plotter、opengl_render_state、gltext、glfont 等）——GLES 代码路径已存在且经 Emscripten 验证。

## 关键约束 / 设计决策

1. **不能用 epoxy**：`<epoxy/gl.h>` 会声明 `glColor4f` 等为真实函数，与 gl2engine.h 中的 inline 定义冲突。GLES 模式下改用直接 `<GLES2/gl2.h>` + 链接 `libGLESv2`（与 Emscripten/Android 一致）。
2. **CMake 选项控制**：新增 `PANGOLIN_USE_GLES2`（默认 OFF，保留桌面 GL 行为）。在 `linux-arm64-build` preset 中置 ON；`msvc-build`（Windows 开发机）保持 OFF。
3. **系统已具备**：Mali `libGLESv2.so`（硬件）、`/usr/include/GLES2/`、`libepoxy`、Mali `libEGL.so.1`。ldconfig 优先选 Mali 的 `libGLESv2`/`libEGL`，所以运行时**无需** `LD_LIBRARY_PATH`，直接硬件加速。

## 修改清单

### 1. 顶层 `CMakeLists.txt`
在已有 `option(...)` 区块附近新增：
```cmake
option( PANGOLIN_USE_GLES2 "Use OpenGL ES 2 instead of desktop OpenGL on Linux (e.g. Mali GPU on RK3588)" OFF )
```

### 2. `components/pango_opengl/CMakeLists.txt`（第 42-64 行 `if(EMSCRIPTEN) ... else() ... endif()`）
把 `_LINUX_` 分支拆成 GLES2 与桌面两条路径：
- **GLES2 分支**（`PANGOLIN_USE_GLES2`）：
  - `find_package(OpenGL REQUIRED COMPONENTS EGL)`（仍需 EGL）
  - `find_library(GLES2_LIBRARY NAMES GLESv2 REQUIRED)` + `find_path(GLES2_INCLUDE_DIR GLES2/gl2.h)`
  - `target_include_directories(... PUBLIC ${GLES2_INCLUDE_DIR})`
  - `target_link_libraries(... PUBLIC OpenGL::EGL ${GLES2_LIBRARY})`
  - `target_compile_definitions(... PUBLIC HAVE_GLES HAVE_GLES_2)`
  - `target_sources(... PRIVATE src/compat/gl2engine.cpp)`
  - **不**定义 `HAVE_EPOXY`，**不** `find_package(epoxy)`
- **桌面分支**（else）：保留现有 epoxy + `OpenGL::OpenGL` + `HAVE_EPOXY` 逻辑不变。

### 3. `components/pango_opengl/include/pangolin/gl/glplatform.h`（第 64-78 行 `#ifdef HAVE_GLES`）
在 `_APPLE_IOS_` 分支后新增 `_LINUX_` 分支，include GLES2 头：
```cpp
#elif defined(_LINUX_)
    #include <EGL/egl.h>
    #ifdef HAVE_GLES_2
        #include <GLES2/gl2.h>
        #include <GLES2/gl2ext.h>
    #else
        #include <GLES/gl.h>
        #define GL_GLEXT_PROTOTYPES
        #include <GLES/glext.h>
    #endif
#endif
```
（`_LINUX_` 由 `pango_core/CMakeLists.txt:10` PUBLIC 定义，对 pango_opengl 可见。）

### 4. `components/pango_windowing/src/display_x11.cpp`（`X11GlContext` 构造，第 159/193/215-217 行）
按 `#ifdef HAVE_GLES` 切换 EGL 绑定：
- 第 159 行 `eglBindAPI`：GLES 用 `EGL_OPENGL_ES_API`，否则 `EGL_OPENGL_API`。
- 第 193 行 `EGL_RENDERABLE_TYPE`：GLES 用 `EGL_OPENGL_ES2_BIT`，否则 `EGL_OPENGL_BIT`。
- 第 215-217 行 `egl_context_attribs`：GLES 追加 `EGL_CONTEXT_CLIENT_VERSION, 2`。

### 5. `CMakePresets.json`
在 `linux-arm64-build` preset 的 `cacheVariables` 中加 `"PANGOLIN_USE_GLES2": "ON"`。

## 不修改的项（已确认安全）

- `stb_truetype.h:285` 的 `my_stbtt_print`（含 glBegin/glEnd）是 demo 函数，被 `#endif` 守护且 glfont.cpp 仅调用 `stbtt_InitFont`，不在编译/调用路径。
- `gldraw.h:322` `glVertex3dv` 已被 `#ifndef HAVE_GLES` 守护。
- `cg.h` 依赖 `<Cg/cg.h>`，项目中无人 include。
- `PangolinConfig.cmake.in` 不传播 epoxy 依赖（仅构建期 `Findepoxy.cmake`），树内构建切换安全。

## 验证步骤（end-to-end）

1. 重新配置（因新增 CMake 选项）：
   ```bash
   cmake --preset linux-arm64-build
   ```
   确认输出含 `PANGOLIN_USE_GLES2=ON`、找到 `GLESv2`。
2. 编译 SimpleDisplay：
   ```bash
   cmake --build out/build/linux-arm64-build --target SimpleDisplay -j
   ```
3. 确认链接到 Mali GLES（而非 Mesa）：
   ```bash
   ldd out/build/linux-arm64-build/examples/SimpleDisplay/SimpleDisplay | grep -iE 'GLES|EGL'
   ```
   期望指向 `/usr/lib/aarch64-linux-gnu/mali/libGLESv2.so` 与 mali `libEGL.so.1`。
4. 直接运行（**不带** LD_LIBRARY_PATH）：
   ```bash
   DISPLAY=:0 ./out/build/linux-arm64-build/examples/SimpleDisplay/SimpleDisplay
   ```
   期望：窗口正常弹出，**不再**出现 `eglBindAPI ... EGL_BAD_PARAMETER`，**不再**出现 `DRI2: failed to create dri screen`（那是 Mesa 软渲染的特征），且交互流畅（硬件加速）。
5. 对照旧软件渲染体验：同样场景下帧率/流畅度应有明显改善。

## 回退

若 GLES 路径在某个 example 上暴露兼容问题，可在 preset 中将 `PANGOLIN_USE_GLES2` 改回 `OFF` 回到桌面 GL（继续用 Mesa 软渲染临时方案），不影响 Windows 构建。
