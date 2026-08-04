# Pangolin RK3588 OpenGL ES 2 适配 README

在 RK3588（ARM64, Mali-G610 GPU）上启用 Pangolin 已有的 OpenGL ES 2 路径，实现 Mali 硬件加速渲染；同时保持桌面 OpenGL 路径源码完全不变。

---

## 1. 背景

原始 Pangolin 0.9.5 的 Linux 构建硬编码为桌面 OpenGL + epoxy，在 RK3588 上运行报错：

```
error: eglBindAPI(0x30a2) failed: EGL_BAD_PARAMETER (300c)
```

原因：Mali-G610 是 OpenGL ES 级 GPU，其 EGL 驱动只支持 `EGL_OPENGL_ES_API`，不支持桌面 `EGL_OPENGL_API`。临时方案 `LD_LIBRARY_PATH=/lib/aarch64-linux-gnu` 可走 Mesa LLVMpipe 软渲染，但卡顿严重。

排查发现 Pangolin **已内置完整的 OpenGL ES 2 兼容层**（`compat/gl2engine.h`、`gl_es_compat.h`，Emscripten/Android 一直在用），只是从未对 Linux 开启。本次改造的核心工作是**把这条已有 GLES 路径接到 Linux 构建**。

## 2. 方案：构建时切换

同一份源码通过 CMake 选项 `PANGOLIN_USE_GLES2` 在桌面 GL 与 GLES2 间切换，不做单一二进制运行时双支持：

| 平台 | `PANGOLIN_USE_GLES2` | GL 后端 | 链接 |
|------|---------------------|--------|------|
| Windows（`msvc-build`） | `OFF`（默认） | 桌面 OpenGL | epoxy + `OpenGL::OpenGL` |
| 桌面 Linux x86_64 | `OFF`（默认） | 桌面 OpenGL | epoxy + `OpenGL::OpenGL` |
| RK3588（`linux-arm64-build`） | `ON` | OpenGL ES 2/3 | 直连 `libGLESv2.so` + `libEGL.so` |

源码两侧通过 `#ifdef HAVE_GLES` / `#ifndef HAVE_GLES` 守卫同时维护、互不影响。

## 3. 构建方法

### 3.1 RK3588（GLES2 + Mali 硬件加速）

```bash
# 前提：系统已安装 Mali libGLESv2 / libEGL（ldconfig 优先指向 mali/ 目录）
#       及 GLES3 头文件 /usr/include/GLES2/gl2.h
cmake -S . -B out/build/linux-arm64-build \
    -DPANGOLIN_USE_GLES2=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/linux-arm64-build -j
```

运行（无需 `LD_LIBRARY_PATH`，ldconfig 自动解析到 Mali 库）：
```bash
DISPLAY=:0 ./out/build/linux-arm64-build/examples/SimpleDisplay/SimpleDisplay
```

### 3.2 桌面 GL（Windows / 桌面 Linux，回归验证）

```bash
cmake -S . -B out/build/desktop -DPANGOLIN_USE_GLES2=OFF
cmake --build out/build/desktop -j
```

## 4. 改动文件清单

### 4.1 构建系统（构建时切换骨架）

| 文件 | 改动 |
|------|------|
| `CMakeLists.txt` | 新增 `option(PANGOLIN_USE_GLES2 ...)`；GCC 版本判断包裹 `-Wno-null-pointer-*`（GCC 9.4 无此选项，`-Werror` 会硬失败） |
| `components/pango_opengl/CMakeLists.txt` | `_LINUX_` 分支按 `PANGOLIN_USE_GLES2` 二分：GLES 用 `find_library` 直连 `libGLESv2`/`libEGL` + `HAVE_GLES HAVE_GLES_2` + 编译 `gl2engine.cpp`；桌面保留 epoxy |
| `components/pango_windowing/CMakeLists.txt` | X11 段 GLES 模式链接 `${OPENGL_egl_LIBRARY}` 而非 `OpenGL::EGL` target |
| `CMakePresets.json` | `linux-arm64-build` 置 `PANGOLIN_USE_GLES2=ON` |

### 4.2 GLES 路径接线

| 文件 | 改动 |
|------|------|
| `components/pango_opengl/include/pangolin/gl/glplatform.h` | `_LINUX_` + `HAVE_GLES_2` 分支 include `<GLES3/gl32.h>`；移除 `#include <EGL/egl.h>`（避免 X11 `#define Success 0` 破坏 Eigen 枚举） |
| `components/pango_windowing/src/display_x11.cpp` | `#ifdef HAVE_GLES` 切换 `eglBindAPI`(`EGL_OPENGL_ES_API`)、`EGL_RENDERABLE_TYPE`(`EGL_OPENGL_ES2_BIT`)、`EGL_CONTEXT_CLIENT_VERSION=2` |

### 4.3 GLES 兼容层补全（编译期缺口）

| 文件 | 改动 |
|------|------|
| `compat/gl_es_compat.h` | 补桌面 GL token 映射（`GL_BGR`/`GL_BGRA`/sized luminance→unsized 等）+ GL_SELECT 拾取 API no-op 桩 |
| `compat/gl2engine.h` | 补 `glColorPointer`/`glNormalPointer`/`glRotatef`/`glScalef`；**着色器加 `#version 100`**；**构造函数末尾 `prog_fixed.Bind()`** |
| `glsl_utilities.h` | `UseNone()` 在 GLES 下重绑 `prog_fixed` 而非 `glUseProgram(0)` |
| `gl/gl.hpp` | `GlTexture::Download` 死代码守护；`GlBufferData::Download` 用 `glMapBufferRange`+`memcpy` 替代 `glGetBufferSubData` |
| `gl/glsl.hpp` | Eigen double uniform 设置器在 GLES 降级为 float 版本 |
| `gl/glchar.h` + `scene/interactive.h` | include 改为 `glinclude.h` 以获取兼容层 inline 定义 |

## 5. 解决的关键问题

本次改造过程中依次解决三个叠加问题，详细排查见 [`0.1 Pangolin RK3588 EGL 初始化失败.md`](./0.1%20Pangolin%20RK3588%20EGL%20初始化失败.md)：

| 阶段 | 问题 | 根因 | 修复 |
|------|------|------|------|
| 1.1-1.5 | `eglBindAPI(EGL_OPENGL_API)` 失败 | Mali EGL 不支持桌面 OpenGL | 构建时切换 `PANGOLIN_USE_GLES2`，绑定 `EGL_OPENGL_ES_API` |
| 1.6 | GLSL 着色器编译失败（info log 空） | `OpenGL::EGL` target 传递性拉入 `libOpenGL.so.0`（libglvnd 调度桩），覆盖 Mali 真实实现，GL 函数静默返回 NULL/0 | 改用 `find_library` 直连 `libEGL.so`/`libGLESv2.so`，绕开 libglvnd |
| 1.7 | 画面空白（无报错无渲染） | ①`prog_fixed` 未保持绑定，`SaveBind/Unbind` 恢复 program=0；②着色器缺 `#version`，Mali GLES3 context 默认 GLSL ES 3.00 致 `attribute`/`gl_FragColor` 失效 | 构造函数 `prog_fixed.Bind()`；着色器加 `#version 100`；`UseNone()` 重绑 `prog_fixed` |

## 6. 验证结果

RK3588 上 `PANGOLIN_USE_GLES2=ON` 全量编译通过（pango_opengl / pango_windowing / pango_display / pango_scene / 全部 examples / 全部 tools）。

运行时验证：
- `readelf -d` / `ldd` 确认 `NEEDED` 仅含 `libGLESv2.so.2` + `libEGL.so.1`，**无** `libOpenGL.so.0` / `libepoxy`
- 绑定到 Mali 硬件库：`/usr/lib/aarch64-linux-gnu/mali/libGLESv2.so.2` / `libEGL.so.1`
- `glGetString(GL_VERSION)` 返回 `OpenGL ES 3.2 v1.g13p0-...`，`GL_RENDERER` 返回 `Mali-G610`
- 示例程序渲染正常：
  - `tutorial_1_gl_intro_classic_triangle`：天蓝色三角形 + 淡紫色背景 ✓
  - `tutorial_2_gl_intro_pango_triangle_vbo`：同上，VBO 路径正常 ✓
  - `SimpleDisplay`：多视图渲染正常 ✓

桌面 GL 路径回归：`PANGOLIN_USE_GLES2=OFF` 编译 `pango_opengl` / `pango_scene` 通过，桌面行为不变。

## 7. 兼容性与可分发性

### 7.1 相对原始版本

兼容性**只增不减**：`OFF` 路径与上游完全等价（所有 GLES 代码均有 `HAVE_GLES` 守卫，OFF 时不定义该宏），`ON` 路径是纯新增能力。

### 7.2 GLES 二进制（`ON` 路径）分发前提

| 前提 | 说明 |
|------|------|
| 架构 | aarch64 ELF，不可跨架构分发（x86_64 / armhf 需重新编译） |
| GLES 版本 | 要求目标设备支持 **GLES 3.x**（编译期用 `<GLES3/gl32.h>`；运行期用了 VAO / `glMapBufferRange` / `glDrawBuffers` 等 GLES 3 core 函数）。纯 GLES 2.0 设备（Mali-400/450）不可用 |
| EGL/GLES vendor | `libEGL.so` 与 `libGLESv2.so` 须来自同一 vendor 且非 libglvnd 调度桩，否则重蹈 1.6 节覆辙 |
| 系统库 | libstdc++ / glibc / X11 ABI 兼容（RK3588 为 Ubuntu 20.04 arm64） |
| 窗口系统 | 当前仅覆盖 X11 后端（见下方已知限制） |

### 7.3 可分发目标

| 设备 | 兼容性 | 说明 |
|------|--------|------|
| RK3588（Mali-G610） | ✓ 已验证 | 开发目标平台 |
| RK3399（Mali-T860）等 Mali GLES 3.x 设备 | ✓ 预期 | 同 Mali 系列 |
| 树莓派 4（V3D, GLES 3.1） | ⚠ 需验证 | Mesa V3D 的 libGLESv2 非 libglvnd 桩，理论可行 |
| 纯 GLES 2.0 设备 | ✗ | GLES 3 函数运行时失败 |
| 桌面 Linux x86_64 / Windows | 应走 `OFF` | 用桌面 GL |

最稳妥的分发方式：分发源码 + CMake 选项，由目标设备本地编译。

## 8. 已知限制

1. **Wayland 后端未覆盖 GLES 守卫**：`display_wayland.cpp` 仍硬编码 `EGL_OPENGL_API`（L380/L892），未加 `#ifdef HAVE_GLES`。RK3588 上 Ubuntu 20.04 默认 X11 会话不受影响；若未来在纯 Wayland 会话（无 XWayland）运行需补此改造（3 处改动，工作量小）。
2. **GLES 3.x 硬性依赖**：当前 GLES 路径要求设备支持 GLES 3.x，不支持纯 GLES 2.0 设备。若需支持，需将 GLES 3 函数调用改为扩展查询守护。
3. **Headless 后端未覆盖**：`display_headless.cpp` 未加 GLES 守卫，离屏渲染路径未验证。

## 9. 开发约束（持久化备忘）

后续维护 Pangolin GLES 路径须遵守的硬约束与工程约定见 [`0.1 Pangolin RK3588 EGL 初始化失败.md` § 1.8](./0.1%20Pangolin%20RK3588%20EGL%20初始化失败.md#18-开发约束与工程约定持久化备忘)，要点：

- GLES 代码必须用 `#ifdef HAVE_GLES` / `#ifndef HAVE_GLES` 守卫，桌面 GL 路径源码不变
- GLES 路径禁用 epoxy（与 `gl2engine.h` inline 定义冲突），直连 `<GLES2/gl2.h>` + `libGLESv2`
- GLES 模式禁用 `OpenGL::EGL` CMake target（传递性拉入 `libOpenGL.so.0` 调度桩），用 `find_library` 直连
- 修改 GL 链接 CMakeLists 后必须全量重编所有目标（旧可执行文件 `NEEDED` 残留）
- GLES 着色器必须显式 `#version 100`；`GlEngine` 构造函数末尾必须 `prog_fixed.Bind()`

## 10. 文档索引

| 文档 | 内容 |
|------|------|
| [README.md](./README.md)（本文） | 改造总览与使用指南 |
| [0.1 Pangolin RK3588 EGL 初始化失败.md](./0.1%20Pangolin%20RK3588%20EGL%20初始化失败.md) | 完整排查报告（1.1-1.8），含根因分析、修复方案、验证结果、开发约束 |
| [bug.md](./bug.md) | Bug 记录与解决标记 |
| [pangolin-rk3588-gles2-enablement.md](./pangolin-rk3588-gles2-enablement.md) | 设计阶段方案文档（注：部分内容已被后续修复超越，以 0.1 文档和本 README 为准） |
