#pragma once

#include <pangolin/platform.h>

#define GLdouble     GLfloat
#define glClearDepth glClearDepthf
#define glFrustum    glFrustumf

#define glColor4fv(a)       glColor4f(a[0], a[1], a[2], a[3])
#define glColor3fv(a)       glColor4f(a[0], a[1], a[2], 1.0f)
#define glColor3f(a,b,c)    glColor4f(a, b, c, 1.0f)

#define glGenFramebuffersEXT        glGenFramebuffers
#define glDeleteFramebuffersEXT     glDeleteFramebuffers
#define glBindFramebufferEXT        glBindFramebuffer
#define glFramebufferTexture2DEXT   glFramebufferTexture2D

#define glGetDoublev                glGetFloatv

// Desktop GL tokens not present in OpenGL ES, mapped to the closest GLES
// equivalent. GLES3 headers already provide the common sized internal formats
// (GL_RGBA8, GL_RGB8, GL_RG, GL_R32F, GL_DEPTH_COMPONENT24, GL_HALF_FLOAT,
// GL_RGB16F, GL_RGB32F, ...), VAOs, glDrawBuffers and glReadBuffer as core.
//
// Luminance sized formats are mapped to the unsized GL_LUMINANCE so that
// glTexImage2D accepts them as the internal format on GLES. They therefore
// share the same value and must NOT appear together in a single switch (the
// GlTexture::Download switch is guarded out under HAVE_GLES for that reason).
#define GL_DOUBLE                   GL_FLOAT
#define GL_BGR                      GL_RGB
#define GL_BGRA                     GL_RGBA
#define GL_LUMINANCE8               GL_LUMINANCE
#define GL_LUMINANCE12              GL_LUMINANCE
#define GL_LUMINANCE16              GL_LUMINANCE
#define GL_LUMINANCE32F_ARB         GL_LUMINANCE
#define GL_LUMINANCE_INTEGER_EXT    GL_LUMINANCE
#define GL_LUMINANCE32UI_EXT        GL_LUMINANCE
#define GL_LUMINANCE32I_EXT         GL_LUMINANCE
#define GL_RGB10                    GL_RGB8
#define GL_RGB12                    GL_RGB8
// 16-bit unorm sized formats are not core in GLES3 (only the float/int/uint
// 16-bit variants exist). Keep the desktop numeric values so they stay distinct
// tokens (used as GlFormatTraits::glinternalformat for unsigned-short types);
// GLES drivers that reject them will only be exercised for those rare types.
#define GL_RG16                     0x822C
#define GL_RGB16                    0x8054
#define GL_RGBA16                   0x805B
// 64-bit integer pixel type (desktop / NV extension). Kept as a distinct token;
// 64-bit integer textures are not supported by GLES anyway.
#define GL_UNSIGNED_INT64_NV        0x8F52
// Desktop framebuffer-extension tokens (GLES has them core, unsuffixed)
#define GL_FRAMEBUFFER_EXT          GL_FRAMEBUFFER
#define GL_COLOR_ATTACHMENT0_EXT    GL_COLOR_ATTACHMENT0
#define GL_DEPTH_ATTACHMENT_EXT     GL_DEPTH_ATTACHMENT
#define GL_RENDERBUFFER_EXT         GL_RENDERBUFFER

// Desktop GL render-mode tokens (GL_RENDER / GL_SELECT / GL_FEEDBACK) and the
// name-stack selection API. GLES has no selection/feedback mode at all; these
// are provided as no-op stubs so pango_scene (which uses GL_SELECT picking)
// compiles. Picking is intentionally disabled under GLES and returns 0 hits.
#define GL_RENDER                   0x1C00
#define GL_FEEDBACK                 0x1C01
#define GL_SELECT                   0x1C02

#include <pangolin/gl/compat/gl2engine.h>

inline void glRectf(GLfloat x1, GLfloat y1, GLfloat x2, GLfloat y2)
{
    GLfloat verts[] = { x1,y1,  x2,y1,  x2,y2,  x1,y2 };    
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
}

inline void glRecti(int x1, int y1, int x2, int y2)
{
    GLfloat verts[] = { (float)x1,(float)y1,  (float)x2,(float)y1,
                        (float)x2,(float)y2,  (float)x1,(float)y2 };
    glEnableClientState(GL_VERTEX_ARRAY);
    glVertexPointer(2, GL_FLOAT, 0, verts);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);
    glDisableClientState(GL_VERTEX_ARRAY);
}

// Desktop GL selection-mode name stack. GLES has no equivalent; stubbed out so
// that pango_scene's GL_SELECT picking compiles. These are no-ops and
// glRenderMode always returns 0 hits, so picking is effectively disabled.
inline void glSelectBuffer(GLsizei /*size*/, GLuint* /*buffer*/) {}
inline GLint glRenderMode(GLenum /*mode*/) { return 0; }
inline void glInitNames(void) {}
inline void glPushName(GLuint /*name*/) {}
inline void glPopName(void) {}
inline void glLoadName(GLuint /*name*/) {}
