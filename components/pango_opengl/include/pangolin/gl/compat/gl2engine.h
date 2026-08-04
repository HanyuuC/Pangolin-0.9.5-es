/* This file is part of the Pangolin Project.
 * http://github.com/stevenlovegrove/Pangolin
 *
 * Copyright (c) 2014 Steven Lovegrove
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#pragma once

#include <stack>
#include <cmath>
#include <cstring>

#include <pangolin/gl/opengl_render_state.h>
#include <pangolin/gl/glsl.h>

namespace pangolin {

class GlEngine
{
public:
    const char* vert =
        // Explicit GLSL ES 1.00 version: some Mali drivers default a shader
        // with no #version directive to GLSL ES 3.00 inside a GLES3 context,
        // where attribute/varying/gl_FragColor are removed and compilation
        // fails silently (empty info log). #version 100 forces GLSL ES 1.00,
        // which GLES3 contexts are required to support.
        #ifdef HAVE_GLES_2
            "#version 100\n"
        #endif
            "attribute vec4 a_position;\n"
            "attribute vec4 a_color;\n"
            "attribute vec3 a_normal;\n"
            "attribute vec2 a_texcoord;\n"
            "uniform vec4 u_color;\n"
            "uniform mat4 u_modelViewProjectionMatrix;\n"
            "varying vec4 v_frontColor;\n"
            "varying vec2 v_texcoord;\n"
            "void main() {\n"
            "    gl_Position = u_modelViewProjectionMatrix * a_position;\n"
            "    v_frontColor = u_color;\n"
            "    v_texcoord = a_texcoord;\n"
            "}\n";

    const char* frag =
        #ifdef HAVE_GLES_2
            "#version 100\n"
            "precision mediump float;\n"
        #endif // HAVE_GLES_2
            "varying vec4 v_frontColor;\n"
            "varying vec2 v_texcoord;\n"
            "uniform sampler2D u_texture;\n"
            "uniform bool u_textureEnable;\n"
            "void main() {\n"
            "  gl_FragColor = v_frontColor;\n"
            "  if(u_textureEnable) {\n"
            "    gl_FragColor *= texture2D(u_texture, v_texcoord);\n"
            "  }\n"
            "}\n";

    GlEngine()
    {
        // Initialise default state
        projection.push(IdentityMatrix());
        modelview.push(IdentityMatrix());
        currentmatrix = &modelview;

        // Set GL_TEXTURE0 as default active texture
        glActiveTexture(GL_TEXTURE0);

        // Compile and link shaders
        prog_fixed.AddShader(GlSlVertexShader, vert);
        prog_fixed.AddShader(GlSlFragmentShader, frag);
        prog_fixed.BindPangolinDefaultAttribLocationsAndLink();

        // Save locations of uniforms
        u_color = prog_fixed.GetUniformHandle("u_color");
        u_modelViewProjectionMatrix = prog_fixed.GetUniformHandle("u_modelViewProjectionMatrix");
        u_texture = prog_fixed.GetUniformHandle("u_texture");
        u_textureEnable = prog_fixed.GetUniformHandle("u_textureEnable");

        // Initialise default uniform values
        UpdateMatrices();
        SetColor(1.0,1.0,1.0,1.0);
    }

    void UpdateMatrices()
    {
        OpenGlMatrix pmv = projection.top() * modelview.top();
        prog_fixed.SaveBind();
        glUniformMatrix4fv( u_modelViewProjectionMatrix, 1, false, pmv.m );
        prog_fixed.Unbind();
    }

    void SetColor(float r, float g, float b, float a)
    {
        prog_fixed.SaveBind();
        glUniform4f( u_color, r, g, b, a);
        prog_fixed.Unbind();
    }

    void EnableTexturing(GLboolean v)
    {
        prog_fixed.SaveBind();
        glUniform1i( u_textureEnable, v);
        prog_fixed.Unbind();
    }

//protected:
    std::stack<OpenGlMatrix> projection;
    std::stack<OpenGlMatrix> modelview;
    std::stack<OpenGlMatrix>* currentmatrix;

    GLenum matrixmode;

    float color[4];

    GlSlProgram  prog_fixed;

    GLint u_color;
    GLint u_modelViewProjectionMatrix;
    GLint u_texture;
    GLint u_textureEnable;
};

GlEngine& glEngine();

}

///////////////////////////////////////////////////////////////////////////////
// OpenGL 1.0 compatibility - Emulate fixed pipeline
///////////////////////////////////////////////////////////////////////////////

// Missing defines that we'll be using
#define GL_MODELVIEW                      0x1700
#define GL_PROJECTION                     0x1701
#define GL_SHADE_MODEL                    0x0B54
#define GL_POINT_SIZE                     0x0B11

#define GL_MULTISAMPLE                    0x809D

#define GL_LIGHTING                       0x0B50
#define GL_POINT_SMOOTH                   0x0B10
#define GL_LINE_SMOOTH                    0x0B20
#define GL_SCISSOR_TEST                   0x0C11
#define GL_COLOR_MATERIAL                 0x0B57

#define GL_FLAT                           0x1D00
#define GL_SMOOTH                         0x1D01

#define GL_MODULATE                       0x2100
#define GL_DECAL                          0x2101
#define GL_ADD                            0x0104
#define GL_TEXTURE_ENV_MODE               0x2200
#define GL_TEXTURE_ENV_COLOR              0x2201
#define GL_TEXTURE_ENV                    0x2300

#define GL_PERSPECTIVE_CORRECTION_HINT    0x0C50
#define GL_POINT_SMOOTH_HINT              0x0C51
#define GL_LINE_SMOOTH_HINT               0x0C52

#define GL_VERTEX_ARRAY                   0x8074
#define GL_NORMAL_ARRAY                   0x8075
#define GL_COLOR_ARRAY                    0x8076
#define GL_TEXTURE_COORD_ARRAY            0x8078

inline void glEnableClientState(GLenum cap)
{
    pangolin::GlEngine& gl = pangolin::glEngine();
    if(cap == GL_VERTEX_ARRAY) {
        glEnableVertexAttribArray(pangolin::DEFAULT_LOCATION_POSITION);
    }else if(cap == GL_COLOR_ARRAY) {
        glEnableVertexAttribArray(pangolin::DEFAULT_LOCATION_COLOUR);
    }else if(cap == GL_NORMAL_ARRAY) {
        glEnableVertexAttribArray(pangolin::DEFAULT_LOCATION_NORMAL);
    }else if(cap == GL_TEXTURE_COORD_ARRAY) {
        glEnableVertexAttribArray(pangolin::DEFAULT_LOCATION_TEXCOORD);
        gl.EnableTexturing(true);
    }else{
        pango_print_error("Not Implemented: %s, %s, %d", __FUNCTION__, __FILE__, __LINE__);
    }
}

inline void glDisableClientState(GLenum cap)
{
    pangolin::GlEngine& gl = pangolin::glEngine();
    if(cap == GL_VERTEX_ARRAY) {
        glDisableVertexAttribArray(pangolin::DEFAULT_LOCATION_POSITION);
    }else if(cap == GL_COLOR_ARRAY) {
        glDisableVertexAttribArray(pangolin::DEFAULT_LOCATION_COLOUR);
    }else if(cap == GL_NORMAL_ARRAY) {
        glDisableVertexAttribArray(pangolin::DEFAULT_LOCATION_NORMAL);
    }else if(cap == GL_TEXTURE_COORD_ARRAY) {
        glDisableVertexAttribArray(pangolin::DEFAULT_LOCATION_TEXCOORD);
        gl.EnableTexturing(false);
    }else{
        pango_print_error("Not Implemented: %s, %s, %d", __FUNCTION__, __FILE__, __LINE__);
    }
}

inline void glVertexPointer( GLint size, GLenum type, GLsizei stride, const GLvoid * pointer)
{
    glVertexAttribPointer(pangolin::DEFAULT_LOCATION_POSITION, size, type, GL_FALSE, stride, pointer);
}

inline void glTexCoordPointer( GLint size, GLenum type, GLsizei stride, const GLvoid * pointer)
{
    glVertexAttribPointer(pangolin::DEFAULT_LOCATION_TEXCOORD, size, type, GL_FALSE, stride, pointer);
}

inline void glColorPointer( GLint size, GLenum type, GLsizei stride, const GLvoid * pointer)
{
    glVertexAttribPointer(pangolin::DEFAULT_LOCATION_COLOUR, size, type, GL_FALSE, stride, pointer);
}

inline void glNormalPointer( GLenum type, GLsizei stride, const GLvoid * pointer)
{
    glVertexAttribPointer(pangolin::DEFAULT_LOCATION_NORMAL, 3, type, GL_FALSE, stride, pointer);
}

inline void glMatrixMode(GLenum mode)
{
    pangolin::GlEngine& gl = pangolin::glEngine();
    gl.currentmatrix = (mode == pangolin::GlProjectionStack) ? &gl.projection : &gl.modelview;
}

inline void glLoadIdentity()
{
    pangolin::GlEngine& gl = pangolin::glEngine();
    gl.currentmatrix->top() = pangolin::IdentityMatrix();
    gl.UpdateMatrices();
}

inline void glLoadMatrixf(const GLfloat* m)
{
    pangolin::GlEngine& gl = pangolin::glEngine();
    pangolin::GLprecision* cm = gl.currentmatrix->top().m;
    for(int i=0; i<16; ++i) cm[i] = (pangolin::GLprecision)m[i];
    gl.UpdateMatrices();
}

inline void glLoadMatrixd(const GLdouble* m)
{
    pangolin::GlEngine& gl = pangolin::glEngine();
    pangolin::GLprecision* cm = gl.currentmatrix->top().m;
    for(int i=0; i<16; ++i) cm[i] = (pangolin::GLprecision)m[i];
    gl.UpdateMatrices();
}

inline void glMultMatrixf(const GLfloat* /*m*/)
{
//    pangolin::GlEngine& gl = pangolin::glEngine();
//    float res[16];
//    pangolin::MatMul<4,4,4,float>(res, m, gl.currentmatrix->m );
//    std::memcpy(gl.currentmatrix->m, res, sizeof(float) * 16 );
    pango_print_error("Not Implemented: %s, %s, %d", __FUNCTION__, __FILE__, __LINE__);
}

inline void glMultMatrixd(const GLdouble* /*m*/)
{
    pango_print_error("Not Implemented: %s, %s, %d", __FUNCTION__, __FILE__, __LINE__);
}

inline void glPushMatrix(void)
{
    pangolin::GlEngine& gl = pangolin::glEngine();
    gl.currentmatrix->push(gl.currentmatrix->top());
}

inline void glPopMatrix(void)
{
    pangolin::GlEngine& gl = pangolin::glEngine();
    gl.currentmatrix->pop();
    gl.UpdateMatrices();
}

inline void glTranslatef(GLfloat x, GLfloat y, GLfloat z )
{
    pangolin::GlEngine& gl = pangolin::glEngine();
    pangolin::GLprecision* cm = gl.currentmatrix->top().m;
    cm[12] += x;
    cm[13] += y;
    cm[14] += z;
    gl.UpdateMatrices();
}

inline void glScalef(GLfloat x, GLfloat y, GLfloat z)
{
    pangolin::GlEngine& gl = pangolin::glEngine();
    pangolin::GLprecision* cm = gl.currentmatrix->top().m;
    for(int r=0; r<3; ++r) {
        cm[0*4+r] *= x;
        cm[1*4+r] *= y;
        cm[2*4+r] *= z;
    }
    gl.UpdateMatrices();
}

inline void glRotatef(GLfloat angle, GLfloat x, GLfloat y, GLfloat z)
{
    // Post-multiply a rotation (M = M * R), matching desktop OpenGL semantics.
    pangolin::GlEngine& gl = pangolin::glEngine();
    float n = std::sqrt(x*x + y*y + z*z);
    if(n == 0.0f) return;
    x /= n; y /= n; z /= n;
    float r = angle * (float)(M_PI / 180.0);
    float c = std::cos(r), s = std::sin(r), t = 1.0f - c;
    pangolin::OpenGlMatrix R;
    std::memset(R.m, 0, sizeof(R.m));
    R(0,0) = t*x*x + c;     R(0,1) = t*x*y - s*z;  R(0,2) = t*x*z + s*y;
    R(1,0) = t*y*x + s*z;   R(1,1) = t*y*y + c;    R(1,2) = t*y*z - s*x;
    R(2,0) = t*z*x - s*y;   R(2,1) = t*z*y + s*x;  R(2,2) = t*z*z + c;
    R(3,3) = 1.0f;
    gl.currentmatrix->top() = (*gl.currentmatrix).top() * R;
    gl.UpdateMatrices();
}

inline void glOrtho(
    GLdouble l, GLdouble r,
    GLdouble b, GLdouble t,
    GLdouble n, GLdouble f)
{
    pangolin::GlEngine& gl = pangolin::glEngine();
    gl.currentmatrix->top() = pangolin::ProjectionMatrixOrthographic(l,r,b,t,n,f);
    gl.UpdateMatrices();
}

inline void glColor4f(	GLfloat red, GLfloat green, GLfloat blue, GLfloat alpha)
{
    pangolin::glEngine().SetColor(red,green,blue,alpha);
}

inline void glShadeModel( GLenum /*mode*/)
{
    pango_print_error("Not Implemented: %s, %s, %d", __FUNCTION__, __FILE__, __LINE__);
}

inline void glPointSize(GLfloat /*size*/)
{
    pango_print_error("Not Implemented: %s, %s, %d", __FUNCTION__, __FILE__, __LINE__);
}

inline void glTexEnvf(	GLenum /*target*/,
    GLenum /*pname*/,
    GLfloat /*param*/)
{
    pango_print_error("Not Implemented: %s, %s, %d", __FUNCTION__, __FILE__, __LINE__);
}
