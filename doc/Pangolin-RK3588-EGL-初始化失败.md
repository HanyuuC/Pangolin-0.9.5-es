# 1. 问题排查报告：Pangolin RK3588 EGL 初始化失败

## 1.1. 问题现象

在 RK3588 (ARM64) 平台上运行 Pangolin OpenGL 示例程序时出现错误：

EGL API 绑定失败（设置 DISPLAY=:0 后）：
```
error: eglBindAPI(0x30a2) failed: EGL_BAD_PARAMETER (300c)
```

## 1.2. 排查方法

### 1.2.1. 分析错误码含义

通过查阅 EGL 规范，确认错误码含义：

| 代码 | 符号常量 | 含义 |
|------|----------|------|
| `0x30A2` | `EGL_OPENGL_API` | 桌面 OpenGL API |
| `0x300C` | `EGL_BAD_PARAMETER` | EGL 实现不支持该参数 |

初步判断：EGL 实现不接受 `EGL_OPENGL_API`，说明该实现不支持桌面 OpenGL。

### 1.2.2. 定位源码中错误触发点

在 Pangolin 源码中搜索 `eglBindAPI` 调用，定位到 `display_x11.cpp`：

```cpp
// 文件: components/pango_windowing/src/display_x11.cpp, 第 159 行
ok = eglBindAPI(EGL_OPENGL_API);
if (!ok)
    error_fatal("eglBindAPI(0x%x) failed: %s", EGL_OPENGL_API, getEGLErrorString().c_str());
```

同时发现 EGL 配置属性也要求桌面 OpenGL：

```cpp
// 第 193 行
EGL_RENDERABLE_TYPE, EGL_OPENGL_BIT,
```

### 1.2.3. 检查系统 EGL 实现

```bash
# 查看系统中所有 EGL 库
ldconfig -p | grep -i egl

# 查看具体文件
ls -la /usr/lib/aarch64-linux-gnu/libEGL*
ls -la /usr/lib/aarch64-linux-gnu/mali/libEGL*
```

发现系统存在 **两个 EGL 实现**：

| 实现 | 路径 | 特点 |
|------|------|------|
| **Mali (Rockchip 专有)** | `/usr/lib/aarch64-linux-gnu/mali/libEGL.so.1` | 硬件加速，仅支持 OpenGL ES |
| **Mesa** | `/lib/aarch64-linux-gnu/libEGL.so.1.1.0` | 支持桌面 OpenGL（通过 LLVMpipe 软渲染） |

### 1.2.4. 检查动态链接器优先级

```bash
# 查看 ldconfig 缓存中 EGL 库的优先级顺序
/sbin/ldconfig -p | grep libEGL.so.1

# 查找 Mali 配置
cat /etc/ld.so.conf.d/00-aarch64-mali.conf
```

确认 `/usr/lib/aarch64-linux-gnu/mali` 被加入 ldconfig 路径，且 Mali EGL 在缓存中排在首位，因此动态链接器优先加载 Mali EGL。

### 1.2.5. 验证程序链接的 EGL 库

```bash
ldd ./tutorial_1_gl_intro_classic_triangle | grep -i egl
```

确认程序运行时动态加载 `libEGL.so.1`。

### 1.2.6. 通过 LD_LIBRARY_PATH 验证假设

```bash
# 强制使用 Mesa EGL
LD_LIBRARY_PATH=/lib/aarch64-linux-gnu ./tutorial_1_gl_intro_classic_triangle
```

程序成功运行，验证了判断：Mali EGL 不支持桌面 OpenGL，而 Mesa EGL 可以。

### 1.2.7. 检查 Pangolin 内置的 GLES 兼容层

初步方案曾担心"Pangolin 大量使用桌面固定管线，适配工作量较大"。为核实，在源码中检查 `HAVE_GLES` 宏与兼容层目录：

```bash
grep -rn "HAVE_GLES" components/ --include="*.cpp" --include="*.h"
ls components/pango_opengl/include/pangolin/gl/compat/
```

发现 Pangolin **已内置完整的 OpenGL ES 2 兼容层**：

| 文件 | 作用 |
|------|------|
| `components/pango_opengl/include/pangolin/gl/compat/gl2engine.h` | 在 GLES2 shader 之上模拟固定管线（`glColor4f`/`glMatrixMode`/`glPushMatrix`/`glLoadMatrixf`/`glVertexPointer`/`glTranslatef`/`glOrtho` 等），由 `class GlEngine` 持有 shader program |
| `components/pango_opengl/include/pangolin/gl/compat/gl_es_compat.h` | 宏别名（`glColor3f→glColor4f`、`glClearDepth→glClearDepthf`、`GLdouble→GLfloat` 等），并 include gl2engine.h |
| `components/pango_opengl/src/compat/gl2engine.cpp` | `glEngine()` 单例定义 |

并且 `gldraw.cpp`、`view.cpp`、`widgets.cpp`、`plotter.cpp`、`opengl_render_state.cpp`、`gltext.cpp`、`glfont.cpp` 等均已有 `#ifndef HAVE_GLES` / `#ifdef HAVE_GLES_2` 守卫的 GLES 代码路径（Emscripten / Android 一直在使用）。

进一步查 `components/pango_opengl/CMakeLists.txt:42-44`，发现该兼容层**仅对 EMSCRIPTEN 开启**：

```cmake
if(EMSCRIPTEN)
    target_compile_definitions(${COMPONENT} PUBLIC HAVE_GLES HAVE_GLES_2 HAVE_GLEW)
    target_sources( ${COMPONENT} PRIVATE ${CMAKE_CURRENT_LIST_DIR}/src/compat/gl2engine.cpp)
```

Linux 分支则走桌面 GL + epoxy，从未定义 `HAVE_GLES`。这表明真正要做的不是"全面适配固定管线"，而是**把已有 GLES 路径接到 Linux 构建**。

## 1.3. 根因分析

### 1.3.1. 直接原因

Pangolin 在 `display_x11.cpp:159` 调用 `eglBindAPI(EGL_OPENGL_API)` 绑定桌面 OpenGL API，但 RK3588 的 Mali-G610 GPU EGL 驱动只支持 `EGL_OPENGL_ES_API`（OpenGL ES），不支持桌面 OpenGL，返回 `EGL_BAD_PARAMETER`。

### 1.3.2. 根本原因

| 层面     | 问题                                                  |
| ------ | --------------------------------------------------- |
| **硬件** | Mali-G610 是 OpenGL ES 级别的 GPU，不提供桌面 OpenGL 硬件支持     |
| **驱动** | Rockchip 专有 Mali EGL 驱动仅实现了 OpenGL ES 接口            |
| **应用** | Pangolin 的 X11/EGL 后端与 Linux CMake 硬编码为桌面 OpenGL；虽内置 GLES 兼容层，但仅对 Emscripten/Android 开启，未对 Linux 接线 |
| **链接** | 系统 ldconfig 配置让 Mali EGL 优先于 Mesa EGL 被加载           |

### 1.3.3. 动态链接流程

```
直接运行:
  LD_LIBRARY_PATH 未设置
  → 查 ld.so.cache → 找到 /usr/lib/.../mali/libEGL.so.1 (Mali)
  → eglBindAPI(EGL_OPENGL_API) → EGL_BAD_PARAMETER ✗

LD_LIBRARY_PATH=/lib/aarch64-linux-gnu:
  → LD_LIBRARY_PATH 优先级高于 ld.so.cache
  → 先搜 /lib/aarch64-linux-gnu/ → 找到 libEGL.so.1 (Mesa)
  → eglBindAPI(EGL_OPENGL_API) → 成功 ✓（但走 LLVMpipe 软渲染，卡顿）
```

## 1.4. 解决方案

### 1.4.1. 临时方案：使用 Mesa EGL 软渲染

```bash
export DISPLAY=:0
LD_LIBRARY_PATH=/lib/aarch64-linux-gnu ./BasicOpenGL/tutorial_1_gl_intro_classic_triangle
```

**优点**：无需修改代码，快速验证
**缺点**：Mesa 通过 LLVMpipe 软件渲染，性能差，且出现 `DRI2: failed to create dri screen` 警告

### 1.4.2. 推荐方案：启用 Pangolin 已有的 OpenGL ES 2 路径

**兼容性策略：构建时切换。** 同一份源码通过 CMake 选项 `PANGOLIN_USE_GLES2` 在桌面 GL 与 GLES2 间切换——Windows 开发机（`msvc-build`）保持 `OFF`，桌面 GL 行为完全不变；RK3588（`linux-arm64-build`）置 `ON` 走 GLES2 + Mali 硬件加速。源码两侧靠既有的 `HAVE_GLES` 守卫同时维护、互不影响，即"源码级同时兼容 OpenGL 与 OpenGL ES"。不做单一二进制运行时双支持（那需要始终启用 shader 兼容层并放弃桌面端原生固定管线，改动大且无必要）。

基于 1.2.7 的发现，**Pangolin 已内置完整的 OpenGL ES 2 兼容层**（Emscripten / Android 一直在使用），并非需要从零适配固定管线。唯一的缺口是：这套 ES 基础设施在 `components/pango_opengl/CMakeLists.txt:42-44` 中仅对 Emscripten 开启；Linux 分支硬编码为桌面 GL + epoxy，且 `display_x11.cpp` 硬编码 `EGL_OPENGL_API`。

因此真正的工作是"把已有 GLES 路径接到 Linux 构建"，而非重写。**该方案已实现并验证通过**，具体改动（10 个文件）：

#### A. 构建时切换骨架

1. **`components/pango_windowing/src/display_x11.cpp`**（`X11GlContext` 构造，第 159 / 193 / 215 行）：按 `#ifdef HAVE_GLES` 切换
   - 第 159 行 `eglBindAPI`：GLES 用 `EGL_OPENGL_ES_API`（桌面保留 `EGL_OPENGL_API`）
   - 第 193 行 `EGL_RENDERABLE_TYPE`：GLES 用 `EGL_OPENGL_ES2_BIT`（桌面保留 `EGL_OPENGL_BIT`）
   - 第 215-217 行 `egl_context_attribs`：GLES 追加 `EGL_CONTEXT_CLIENT_VERSION, 2`

2. **顶层 `CMakeLists.txt`**：新增 `option(PANGOLIN_USE_GLES2 "Use OpenGL ES 2 instead of desktop OpenGL on Linux (e.g. Mali GPU on RK3588)" OFF)`（默认 OFF 保留桌面 GL 行为）；同时把 `-Wno-null-pointer-arithmetic/-subtraction` 用 GCC≥12 版本判断包起来——这两个选项仅 GCC 12+ 存在，在 RK3588 的 GCC 9.4 下会被 `-Werror` 升级为硬错误。

3. **`components/pango_opengl/CMakeLists.txt`**（`if(EMSCRIPTEN) ... else() ... endif()`）：把 `_LINUX_` 分支按 `PANGOLIN_USE_GLES2` 二分
   - **GLES 分支**：定义 `HAVE_GLES HAVE_GLES_2`、编译 `src/compat/gl2engine.cpp`、`find_library(GLESv2)` + `find_library(EGL)` + include `<GLES2/gl2.h>` / `<EGL/egl.h>`、**直接按路径链接 `libEGL.so` 与 `libGLESv2.so`**，**不**用 epoxy，也**不**用 `OpenGL::EGL` CMake target（详见 1.6 节）
   - **桌面分支**：保留 epoxy + `OpenGL::OpenGL` + `HAVE_EPOXY` 不变
   - 注：**不能用 epoxy**——`<epoxy/gl.h>` 会声明 `glColor4f` 等为真实函数，与 `gl2engine.h` 中的 inline 定义冲突；必须像 Emscripten/Android 那样直接用 `<GLES2/gl2.h>` + `libGLESv2`

4. **`components/pango_opengl/include/pangolin/gl/glplatform.h`**：`_LINUX_` 分支按 `HAVE_GLES_2` 二分——GLES 模式 include `<GLES3/gl32.h>` + `<GLES3/gl3ext.h>`（Mali-G610 支持 GLES 3.2，GLES3 头文件把 VAO、`glDrawBuffers`、`glReadBuffer`、sized internal format、`GL_HALF_FLOAT`、`GL_DEPTH_COMPONENT24` 等作为 core 提供，减少兼容代码量；gl2engine 的 GLES2-GLSL shader 仍可在 GLES3 context 下运行）；桌面保留 epoxy 路径。同时移除这里的 `#include <EGL/egl.h>`——它在 Linux 上会经 `eglplatform.h` 引入 X11，X11 的 `#define Success 0` 会破坏 Eigen 的枚举。

5. **`CMakePresets.json`**：`linux-arm64-build` preset 的 `cacheVariables` 中置 `"PANGOLIN_USE_GLES2": "ON"`；`msvc-build`（Windows 开发机）保持 OFF。

#### B. GLES 路径编译期补全（兼容层缺口）

既有 GLES 兼容层是为 Emscripten/Android 维护的，接到 Linux + GLES3 头文件后暴露出若干缺口，逐一补齐（这些改动均在 `#ifdef HAVE_GLES` / `#ifndef HAVE_GLES` 守卫内，**不影响桌面 GL 路径**）：

6. **`components/pango_opengl/include/pangolin/gl/compat/gl_es_compat.h`**：补充桌面 GL token 映射——`GL_BGR→GL_RGB`、`GL_BGRA→GL_RGBA`、`GL_LUMINANCE8/12/16/32F_ARB→GL_LUMINANCE`（sized luminance 在 GLES 不存在，映射为 unsized 才能被 `glTexImage2D` 接受）、`GL_RGB10/12→GL_RGB8`；`GL_RG16/GL_RGB16/GL_RGBA16` 保留桌面数值（0x822C/0x8054/0x805B）以保证 switch case 互异；`GL_UNSIGNED_INT64_NV=0x8F52`；`GL_*_EXT` framebuffer token → unsized core。另补 `GL_RENDER/GL_SELECT/GL_FEEDBACK`（0x1C00/0x1C02/0x1C01）及 name-stack 选择 API 的 no-op 桩函数（`glSelectBuffer`/`glRenderMode`（返回 0 hits）/`glInitNames`/`glPushName`/`glPopName`/`glLoadName`），使 `pango_scene` 的 GL_SELECT 拾取代码可编译（GLES 无选择/反馈模式，拾取功能在 GLES 下禁用）。

7. **`components/pango_opengl/include/pangolin/gl/compat/gl2engine.h`**：补 `glColorPointer`、`glNormalPointer`（inline，转 `glVertexAttribPointer(DEFAULT_LOCATION_COLOUR/NORMAL,...)`，供 `glvbo.h` 使用）；新增 `glRotatef`（构造轴角旋转矩阵后 `M = M * R`，复用 `OpenGlMatrix::operator*`）与 `glScalef`（按列缩放当前矩阵），供 `handler_image.cpp` 等使用。

8. **`components/pango_opengl/include/pangolin/gl/gl.hpp`**：
   - `GlTexture::Download(TypedImage&)` 的 switch 用 `#ifndef HAVE_GLES` 守护——因 sized luminance 宏都映射到 `GL_LUMINANCE`，会产生重复 case；而该 switch 最终调用的 `glGetTexImage` 在 GLES 不可用（已 throw），整段属死代码，GLES 分支直接 throw。
   - `GlBufferData::Download` 的 `glGetBufferSubData`（GLES 无此函数）改为 `glMapBufferRange` + `memcpy` 回退（GLES3 core）。

9. **`components/pango_opengl/include/pangolin/gl/glsl.hpp`**：所有 `glUniform*d` / `glUniformMatrix*dv`（double 精度）设置器用 `#ifndef HAVE_GLES ... #else` 包裹，GLES 分支降级为 float 版本（`glUniform*f` / `glUniformMatrix*fv`，矩阵逐元素 downcast），覆盖标量 double、`Eigen::Vector*d`、`Eigen::Matrix*d` 全部重载。

10. **`components/pango_opengl/include/pangolin/gl/glchar.h`** 与 **`components/pango_scene/include/pangolin/scene/interactive.h`**：把 `#include <pangolin/gl/glplatform.h>` 改为 `<pangolin/gl/glinclude.h>`，使下游（`glchar.cpp`、`renderable.cpp` 经 `renderable.h`→`interactive.h`、`scenehandler.h`）的固定管线函数与 `GL_RENDER` token 经 `gl_es_compat.h` → `gl2engine.h` 获得 inline 定义。

**系统侧已具备全部前提**：

```bash
ls /usr/lib/aarch64-linux-gnu/mali/libGLESv2.so   # Mali 硬件 GLES2
ls /usr/include/GLES2/gl2.h                        # GLES2 头文件
```

由于 ldconfig 优先选 Mali 的 `libGLESv2` / `libEGL`，运行时**无需** `LD_LIBRARY_PATH`，直接走 Mali 硬件加速，消除 Mesa 软渲染卡顿，也不再出现 `DRI2: failed to create dri screen` 警告。

#### C. 验证结果

```bash
# RK3588 上以 GLES2 构建（全部目标）
cmake -S ... -B out/build/linux-arm64-build -DPANGOLIN_USE_GLES2=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build out/build/linux-arm64-build -j
```

- 全量编译成功（退出码 0）：`pango_opengl` / `pango_windowing` / `pango_display` / `pango_scene` / 全部 examples（SimpleDisplay、SimpleScene、SimpleVideo、HelloPangolinOffscreen …）/ 全部 tools（VideoViewer、ModelViewer、VideoConvert …）。
- `compile_commands.json` 确认 `HAVE_GLES` / `HAVE_GLES_2` 已定义。
- `readelf -d libpango_opengl.so.0` 的 `NEEDED` 仅含 `libGLESv2.so.2`，**无** `libOpenGL.so` / `libepoxy`。
- `ldd SimpleDisplay` 确认运行时绑定到 Mali 硬件库，且**无** `libOpenGL.so.0`（libglvnd 调度器，详见 1.6 节）：
  ```
  libGLESv2.so.2 => /usr/lib/aarch64-linux-gnu/mali/libGLESv2.so.2
  libEGL.so.1    => /usr/lib/aarch64-linux-gnu/mali/libEGL.so.1
  ```
- `glGetString(GL_VERSION)` 返回 `OpenGL ES 3.2 v1.g13p0-...`，`glGetString(GL_RENDERER)` 返回 `Mali-G610`；`glCreateShader` / `glCreateProgram` 返回有效句柄（非 0），固定管线模拟 shader 编译链接成功。
- 桌面 GL 路径回归：另起构建目录 `-DPANGOLIN_USE_GLES2=OFF` 编译 `pango_opengl` / `pango_scene` 同样通过，确认两侧源码级同时兼容、互不影响。

**前述"工作量较大"的判断不成立**——`HAVE_GLES` 宏与 GLES 代码路径早已存在，仅需接线。已核查的高风险点均为死代码或已正确守护：

| 疑似风险 | 核查结论 |
|------|------|
| `stb_truetype.h:285` 的 `my_stbtt_print`（含 `glBegin/glEnd`） | 被 `#endif` 守护的 demo 函数；`glfont.cpp` 仅调用 `stbtt_InitFont`，不在编译/调用路径 |
| `gldraw.h:322` 的 `glVertex3dv` | 已被 `#ifndef HAVE_GLES` 正确守护 |
| `components/pango_opengl/include/pangolin/gl/cg.h`（含 `glBegin/glEnd`） | 依赖 `<Cg/cg.h>`（NVIDIA Cg），项目中无人 include |

### 1.4.3. 其他方案

- 尝试在系统中安装完整的 Mesa 桌面 OpenGL 硬件驱动（RK3588 Panfrost 驱动目前主要支持 OpenGL ES）
- 使用基于 OpenGL ES 的替代可视化库（如 GLFW + GLES）
- 使用 Vulkan 后端（RK3588 支持 Vulkan 1.2）

## 1.5. 经验总结

| 经验 | 说明 |
|------|------|
| **错误码分析** | EGL 错误码直接指明了 API 不兼容，是定位问题的关键线索 |
| **源码追踪** | 通过搜索 `eglBindAPI` 快速定位问题代码位置 |
| **系统环境检查** | 检查 `ldconfig -p` 和 `/etc/ld.so.conf.d/` 发现两个 EGL 实现竞争 |
| **动态链接机制** | 理解 `LD_LIBRARY_PATH` 优先级高于 ld.so.cache 是解决问题的关键 |
| **ARM 平台特性** | ARM64 平台的 Mali GPU 通常只支持 OpenGL ES，桌面 OpenGL 需要软件回退 |
| **已有抽象的再发现** | 改源码前先查 `HAVE_GLES` 等宏与 `compat/` 目录，发现 GLES 兼容层早已存在（Emscripten/Android 在用），避免误判"全面适配、工作量较大"；真正的缺口只是 CMake 未对 Linux 开启这条路径 |
| **依赖冲突排查** | epoxy 的 `<epoxy/gl.h>` 会声明桌面 GL 真实函数，与 GLES 兼容层的 inline 定义冲突，故 GLES 路径必须直连 `<GLES2/gl2.h>` + `libGLESv2` |
| **libglvnd 调度阴影** | `OpenGL::EGL` CMake target 经 `INTERFACE_LINK_LIBRARIES` 链式拉入 `OpenGL::OpenGL`（`libOpenGL.so.0`），其 GL 调度桩函数会覆盖 Mali 真实实现，导致 `glGetString` 返回 NULL、`glCreateShader` 返回 0；GLES 模式必须按路径直连 `libEGL.so` / `libGLESv2.so`，绕开 libglvnd（详见 1.6） |

## 1.6. 后续问题：GLSL 着色器编译失败（libOpenGL.so.0 调度阴影）

### 1.6.1. 问题现象

GLES2 路径编译通过后，运行示例出现着色器编译失败，且 info log 为空：
```
GLSL Shader compilation failed: <string>:
In source code:
1: #version 100
2: attribute vec4 a_position;
...
GLSL Program link failed: No details provided.
```

添加诊断后发现关键线索：`eglMakeCurrent` 返回成功（`EGL_SUCCESS`），但 `glGetString(GL_VERSION)` 返回 **NULL**，`glCreateShader` / `glCreateProgram` 返回 **0**，且 `glGetError()` 为 `GL_NO_ERROR`（0x0）—— GL 函数静默失败，不报错。

### 1.6.2. 根因分析

`ldd SimpleDisplay` 显示二进制同时加载了三类 GL 库：
```
libOpenGL.so.0  => /lib/aarch64-linux-gnu/libOpenGL.so.0          ← libglvnd 桌面 GL 调度器
libGLESv2.so.2  => /usr/lib/aarch64-linux-gnu/mali/libGLESv2.so.2  ← Mali 真实实现
libEGL.so.1     => /usr/lib/aarch64-linux-gnu/mali/libEGL.so.1     ← Mali 真实实现
```

`libOpenGL.so.0`（libglvnd）导出 `glGetString` / `glCreateShader` / ... 等 GL 函数符号为**调度桩**（dispatch stub），在符号解析中优先于 Mali 的 `libGLESv2.so.2`。这些桩函数依据 libglvnd 内部追踪的"当前上下文"分发调用；但 EGL 上下文由 **Mali 的 libEGL** 创建，libglvnd 并未登记该上下文，因此桩函数直接返回 NULL / 0 且不置错误码。

`libOpenGL.so.0` 的来源：CMake 的 `find_package(OpenGL COMPONENTS EGL)` 创建的 `OpenGL::EGL` imported target 在 `FindOpenGL.cmake` 中设置了：
```cmake
set_target_properties(OpenGL::EGL PROPERTIES
    INTERFACE_LINK_LIBRARIES OpenGL::OpenGL)   # ← 链式拉入 libOpenGL.so.0
```
因此任何链接 `OpenGL::EGL` 的目标（`pango_opengl` PUBLIC、`pango_windowing` PRIVATE）都会传递性地引入 `libOpenGL.so.0`。

系统侧 libglvnd 的 EGL vendor 配置只有 Mesa（`/usr/share/glvnd/egl_vendor.d/50_mesa.json`），无 Mali ICD——即使加载 libglvnd 的 EGL 也会走 Mesa 软渲染，进一步印证 libglvnd 与 Mali 不兼容。

### 1.6.3. 修复方案

**GLES 模式下不使用 `OpenGL::EGL` CMake target，改为 `find_library` 按路径直连 `libEGL.so` / `libGLESv2.so`。** ldconfig 在运行时将 `libEGL.so.1` / `libGLESv2.so.2` 解析到 Mali 的副本（`/usr/lib/aarch64-linux-gnu/mali/`），二者均依赖 `libmali.so.1` 同一后端，GL 符号全部解析到 Mali 真实实现，`libOpenGL.so.0` 完全不进入进程。

涉及两处 CMakeLists：

1. **`components/pango_opengl/CMakeLists.txt`**（GLES 分支）：移除 `find_package(OpenGL REQUIRED COMPONENTS EGL)` 与 `target_link_libraries(... OpenGL::EGL ...)`，改为：
   ```cmake
   find_library(GLES2_LIBRARY NAMES GLESv2 REQUIRED)
   find_library(EGL_LIBRARY  NAMES EGL  REQUIRED)
   target_link_libraries(${COMPONENT} PUBLIC ${EGL_LIBRARY} ${GLES2_LIBRARY})
   ```

2. **`components/pango_windowing/CMakeLists.txt`**（X11 EGL 段）：GLES 模式下将 `target_link_libraries(... OpenGL::EGL)` 替换为 `target_link_libraries(... ${OPENGL_egl_LIBRARY})`（`find_package(OpenGL QUIET COMPONENTS EGL)` 仍用于检测 EGL 是否可用，但只取其库路径变量，不链接 imported target）。桌面分支保留 `OpenGL::EGL` 不变。

### 1.6.4. 验证

修复并全量重新编译后：
- `readelf -d` 确认 SimpleDisplay / libpango_opengl.so / libpango_windowing.so / libpango_display.so 的 `NEEDED` 中**均无** `libOpenGL.so.0`，仅有 `libGLESv2.so.2`（及 windowing 的 `libEGL.so.1`）。
- `ldd` 确认运行时仅加载 Mali 的 `libGLESv2.so.2` / `libEGL.so.1`。
- `glGetString(GL_VERSION)` 返回 `OpenGL ES 3.2 v1.g13p0-01eac0...`，`glGetString(GL_RENDERER)` 返回 `Mali-G610`。
- `glCreateProgram` 返回 1，`glCreateShader` 返回 2（vertex）/ 3（fragment），着色器编译链接成功。
- SimpleDisplay、tutorial_1_gl_intro_classic_triangle、tutorial_2_gl_intro_pango_triangle_vbo 均无错误运行。

> **注意**：修改 CMakeLists 后必须**全量重新编译所有目标**（`cmake --build . -j`），仅重编库文件不够——旧的可执行文件自身的 `NEEDED` 列表仍残留 `libOpenGL.so.0`，运行时仍会加载它并触发同样的符号阴影问题。

## 1.7. 后续问题：画面空白（prog_fixed 未绑定 + GLSL 版本缺失）

### 1.7.1. 问题现象

1.6 修复后程序运行无报错、着色器编译链接成功，但**画面完全空白**（无任何图形绘制）。

### 1.7.2. 根因分析

两个独立问题叠加：

**A. 固定管线模拟着色器未在绘制时保持绑定**

GLES 没有桌面 GL 的固定功能管线，所有绘制必须有一个活跃的着色器 program。Pangolin 的 `GlEngine` 类用 `prog_fixed`（一个简单的 MVP + uniform-color 着色器）模拟固定管线。但 `UpdateMatrices()` / `SetColor()` / `EnableTexturing()` 使用 `SaveBind()` / `Unbind()` 模式更新 uniform：

```cpp
void UpdateMatrices() {
    prog_fixed.SaveBind();                              // 保存当前 program（初始为 0）
    glUniformMatrix4fv(u_modelViewProjectionMatrix, …); // 设 uniform
    prog_fixed.Unbind();                                // 恢复 program 0 ← 无活跃 program！
}
```

`Unbind()` 恢复的是 `SaveBind()` 之前保存的 program，初始值为 0（无 program）。因此构造完成后、以及每次矩阵/颜色更新后，**当前 program 为 0**，后续 `glDrawArrays` 在 GLES 下是无操作（静默丢弃，不报错）。

桌面 GL 不受影响——它有硬件固定管线，program=0 时仍可渲染。

**B. 着色器缺少 `#version` 指令**

Mali GLES 3.x context 下，着色器若不指定 `#version`，驱动默认使用 GLSL ES 3.00，其中 `attribute` / `varying` / `gl_FragColor` 关键字已被移除，编译失败且 info log 为空。

### 1.7.3. 修复方案

**A. `gl2engine.h` — 构造函数末尾绑定 `prog_fixed`：**

```cpp
// GlEngine 构造函数末尾
prog_fixed.Bind();   // 使 prog_fixed 成为活跃 program 并保持
```

`Bind()` 设置 `prev_prog = 0` 并 `glUseProgram(prog_fixed)`。此后 `SaveBind` / `Unbind` 会正确保存和恢复 `prog_fixed`（而非 0），自定义着色器的 `SaveBind` / `Unbind` 也会在用完后恢复 `prog_fixed`。

**B. `gl2engine.h` — 着色器添加 `#version 100`：**

```cpp
const char* vert =
    #ifdef HAVE_GLES_2
        "#version 100\n"
    #endif
        "attribute vec4 a_position;\n" …;
```

`#version 100` 强制 GLSL ES 1.00（GLES 3.x context 必须向后支持），`attribute` / `varying` / `gl_FragColor` 可用。

**C. `glsl_utilities.h` — `UseNone()` 在 GLES 下重新绑定 `prog_fixed`：**

```cpp
inline static void UseNone() {
#ifdef HAVE_GLES
    pangolin::glEngine().prog_fixed.Bind();  // 而非 glUseProgram(0)
#else
    glUseProgram(0);
#endif
}
```

桌面 GL 的 `glUseProgram(0)` 切回固定管线；GLES 无固定管线，等价操作是重新绑定 `prog_fixed`。`image_view.cpp` 在用自定义着色器渲染纹理后调用 `UseNone()` 切回，若不修复会再次丢失活跃 program。

### 1.7.4. 验证

修复后用户确认：
- `tutorial_1_gl_intro_classic_triangle`：正常显示天蓝色三角形，淡紫色背景。
- `tutorial_2_gl_intro_pango_triangle_vbo`：同上，VBO 路径正常。
- `SimpleDisplay`：正常运行，多视图渲染。

## 1.8. 开发约束与工程约定（持久化备忘）

> 本节集中沉淀 RK3588 GLES2 适配过程中确立的硬性约束、工程约定与技术陷阱，供后续维护 Pangolin GLES 路径时快速参考。各项的详细论述见对应正文章节。本节内容与项目本地记忆（`project_memory.md`）保持同步，但以此处为准——doc 在 git 仓库内，会被版本控制；记忆文件不在仓库内，仅作 IDE 本地缓存。

### 1.8.1. 硬约束（不可违反）

| 约束 | 说明 | 参见 |
|------|------|------|
| **构建时切换 `PANGOLIN_USE_GLES2`** | Pangolin 库通过构建时选项 `PANGOLIN_USE_GLES2` 实现 OpenGL 与 OpenGL ES 2 兼容切换，不做单一二进制运行时双支持 | 1.4.2 |
| **GLES 代码必须守卫** | GLES 相关代码一律用 `#ifdef HAVE_GLES` / `#ifndef HAVE_GLES` 守卫，确保桌面 OpenGL 路径源码不受任何影响（两侧源码级同时兼容、互不影响） | 1.4.2 B |
| **禁用 epoxy** | GLES 路径不能使用 epoxy 库——`<epoxy/gl.h>` 会声明 `glColor4f` 等桌面 GL 真实函数，与 `gl2engine.h` 中的 inline 定义冲突；必须像 Emscripten/Android 那样直接用 `<GLES2/gl2.h>` + 链接系统 `libGLESv2.so` | 1.4.2 A.3 |
| **禁用 `OpenGL::EGL` CMake target** | GLES 模式下**禁止**使用 CMake 的 `OpenGL::EGL` target。它经 `INTERFACE_LINK_LIBRARIES` 链式拉入 `OpenGL::OpenGL`=`libOpenGL.so.0`，其调度桩符号覆盖 Mali 真实 GL 实现（`glGetString` 返回 NULL、`glCreateShader` 返回 0 且 `glGetError` 为 `GL_NO_ERROR`）。必须用 `find_library` 按路径直连 `libEGL.so` / `libGLESv2.so` | 1.6 |
| **GLES 构建排除 Wayland 后端** | GLES 构建只提供 X11 窗口后端——`pango_windowing/CMakeLists.txt` 的 Wayland 段带 `NOT PANGOLIN_USE_GLES2` 条件，`PANGO_DEFAULT_WIN_URI` 回落 `"x11"`。原因：`display_wayland.cpp` 无 `HAVE_GLES` 守卫，装饰按钮用立即模式（`glBegin`/`glVertex2f`/`glEnd`，GLES 头无、兼容层未模拟）绘制，装有 wayland 开发包时编译期报错；其 EGL 配置亦硬编码桌面 GL。新增/接线窗口后端时须同步检查此项 | doc/README.md § 8 |

### 1.8.2. 工程约定（编码规范）

| 约定 | 说明 | 参见 |
|------|------|------|
| **GCC 版本判断包裹警告选项** | CMakeLists.txt 中 `-Wno-null-pointer-arithmetic` / `-Wno-null-pointer-subtraction` 仅 GCC 12+ 存在，须用 GCC 版本判断包裹；在 RK3588 的 GCC 9.4 下会被 `-Werror` 升级为硬错误 | 1.4.2 A.2 |
| **`glGetBufferSubData` 替代实现** | GLES 环境下 `glGetBufferSubData` 不存在，`GlBufferData::Download` 改用 `glMapBufferRange` + `memcpy` 回退实现（GLES3 core） | 1.4.2 B.8 |
| **Eigen double uniform 降级** | Eigen `Vector*d` / `Matrix*d` 类型的 `glUniform*d` / `glUniformMatrix*dv` 调用在 GLES 分支降级为 float 版本（`glUniform*f` / `glUniformMatrix*fv`，矩阵逐元素 downcast），覆盖标量 double 及全部重载 | 1.4.2 B.9 |
| **改 GL 链接 CMakeLists 后全量重编** | 修改 GL 链接相关 CMakeLists 后必须全量重编所有目标——仅重编库文件不够，旧可执行文件自身的 `NEEDED` 列表仍残留被禁用的库（如 `libOpenGL.so.0`），运行时仍会加载它并触发符号阴影问题 | 1.6.4 |

### 1.8.3. 技术陷阱（Lessons Learned）

| 陷阱 | 现象与规避 | 参见 |
|------|------|------|
| **GLES 兼容层默认不对 Linux 开启** | Pangolin 内置的 GLES 兼容层（`compat/gl2engine.h`、`gl_es_compat.h`）默认仅对 Emscripten/Android 开启，需手动为 Linux 平台接线（CMake 分支 + `HAVE_GLES` 定义）；改源码前先查 `HAVE_GLES` 宏与 `compat/` 目录，避免误判"需从零适配固定管线、工作量较大" | 1.2.7, 1.4.2 |
| **libglvnd 调度桩静默失败** | RK3588 上 libglvnd 的 `libOpenGL.so.0` 调度桩不识别 Mali EGL 创建的上下文，导致 `glGetString` 返回 NULL、`glCreateShader` 返回 0 且 `glGetError` 为 `GL_NO_ERROR`（静默失败，不报错）；症状是 GLSL 着色器编译失败且 info log 为空。规避：GLES 模式按路径直连 Mali 的 `libEGL.so` / `libGLESv2.so`，使 `libOpenGL.so.0` 完全不进入进程 | 1.6 |
| **`OpenGL::EGL` 传递性依赖** | `OpenGL::EGL` CMake target 的 `INTERFACE_LINK_LIBRARIES` 包含 `OpenGL::OpenGL`，任何链接它的目标都会传递性引入 `libOpenGL.so.0`；GLES 模式须绕开此 target，改用 `find_library` 直连 | 1.6 |
| **`prog_fixed` 未保持绑定致画面空白** | GLES 无固定功能管线，`GlEngine::prog_fixed` 必须在构造函数末尾 `Bind()` 保持绑定。否则 `SaveBind`/`Unbind` 模式会将当前 program 恢复为 0，导致 `glDrawArrays` 静默丢弃所有绘制（画面空白但不报错）。桌面 GL 因有硬件固定管线，program=0 仍可渲染，故不受影响 | 1.7 |
| **`UseNone()` 不能 `glUseProgram(0)`** | `GlSlUtilities::UseNone()` 在 GLES 下不能 `glUseProgram(0)`（等于丢失活跃 program），必须改为重新绑定 `glEngine().prog_fixed`。`image_view.cpp` 等在用自定义着色器渲染后会调 `UseNone()` 切回，若不修复会再次丢失活跃 program | 1.7 |
| **GLSL 必须显式 `#version 100`** | Mali GLES 3.x context 下着色器不指定 `#version` 时默认 GLSL ES 3.00，`attribute`/`varying`/`gl_FragColor` 关键字失效，编译失败且 info log 为空；必须显式 `#version 100` 强制 GLSL ES 1.00（GLES 3.x 向后支持） | 1.7 |
