<!-- 已解决（2026-08-04）：根因为 libglvnd 的 libOpenGL.so.0 调度桩符号覆盖 Mali 真实 GL 实现，
     导致 glGetString 返回 NULL、glCreateShader 返回 0。
     修复：GLES 模式下用 find_library 直连 libEGL.so / libGLESv2.so，不使用 OpenGL::EGL CMake target。
     详见 doc/0.1 Pangolin RK3588 EGL 初始化失败.md 第 1.6 节。 -->

ubuntu@ubuntu:~/cpp_proj_hanyu/libs_cpp/third_party/Pangolin-0.9.5-es/out/build/linux-arm64-build/examples$ ./SimpleDisplay/SimpleDisplay

arm_release_ver: g13p0-01eac0, rk_so_ver: 6

GLSL Shader compilation failed: <string>:

In source code:
1: attribute vec4 a_position;
2: attribute vec4 a_color;
3: attribute vec3 a_normal;
4: attribute vec2 a_texcoord;
5: uniform vec4 u_color;
6: uniform mat4 u_modelViewProjectionMatrix;
7: varying vec4 v_frontColor;
8: varying vec2 v_texcoord;
9: void main() {
10:     gl_Position = u_modelViewProjectionMatrix * a_position;
11:     v_frontColor = u_color;
12:     v_texcoord = a_texcoord;
13: }
14:
GLSL Shader compilation failed: <string>:
In source code:
1: precision mediump float;
2: varying vec4 v_frontColor;
3: varying vec2 v_texcoord;
4: uniform sampler2D u_texture;
5: uniform bool u_textureEnable;
6: void main() {
7:   gl_FragColor = v_frontColor;
8:   if(u_textureEnable) {
9:     gl_FragColor *= texture2D(u_texture, v_texcoord);
10:   }
11: }
12:
GLSL Program link failed: No details provided.

ubuntu@ubuntu:~/cpp_proj_hanyu/libs_cpp/third_party/Pangolin-0.9.5-es/out/build/linux-arm64-build/examples$ ./BasicOpenGL/tutorial_1_gl_intro_classic_triangle 
arm_release_ver: g13p0-01eac0, rk_so_ver: 6
GLSL Shader compilation failed: <string>:

In source code:
1: attribute vec4 a_position;
2: attribute vec4 a_color;
3: attribute vec3 a_normal;
4: attribute vec2 a_texcoord;
5: uniform vec4 u_color;
6: uniform mat4 u_modelViewProjectionMatrix;
7: varying vec4 v_frontColor;
8: varying vec2 v_texcoord;
9: void main() {
10:     gl_Position = u_modelViewProjectionMatrix * a_position;
11:     v_frontColor = u_color;
12:     v_texcoord = a_texcoord;
13: }
14: 

GLSL Shader compilation failed: <string>:

In source code:
1: precision mediump float;
2: varying vec4 v_frontColor;
3: varying vec2 v_texcoord;
4: uniform sampler2D u_texture;
5: uniform bool u_textureEnable;
6: void main() {
7:   gl_FragColor = v_frontColor;
8:   if(u_textureEnable) {
9:     gl_FragColor *= texture2D(u_texture, v_texcoord);
10:   }
11: }
12: 

GLSL Program link failed: No details provided.

ubuntu@ubuntu:~/cpp_proj_hanyu/libs_cpp/third_party/Pangolin-0.9.5-es/out/build/linux-arm64-build/examples$ ./BasicOpenGL/tutorial_2_gl_intro_pango_triangle_vbo 
arm_release_ver: g13p0-01eac0, rk_so_ver: 6
GLSL Shader compilation failed: <string>:

In source code:
1: attribute vec4 a_position;
2: attribute vec4 a_color;
3: attribute vec3 a_normal;
4: attribute vec2 a_texcoord;
5: uniform vec4 u_color;
6: uniform mat4 u_modelViewProjectionMatrix;
7: varying vec4 v_frontColor;
8: varying vec2 v_texcoord;
9: void main() {
10:     gl_Position = u_modelViewProjectionMatrix * a_position;
11:     v_frontColor = u_color;
12:     v_texcoord = a_texcoord;
13: }
14: 

GLSL Shader compilation failed: <string>:

In source code:
1: precision mediump float;
2: varying vec4 v_frontColor;
3: varying vec2 v_texcoord;
4: uniform sampler2D u_texture;
5: uniform bool u_textureEnable;
6: void main() {
7:   gl_FragColor = v_frontColor;
8:   if(u_textureEnable) {
9:     gl_FragColor *= texture2D(u_texture, v_texcoord);
10:   }
11: }
12: 
