/* ***** BEGIN LICENSE BLOCK *****
 * This file is part of openfx-io <https://github.com/MrKepzie/openfx-io>,
 * Copyright (C) 2013-2017 INRIA
 *
 * openfx-io is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * openfx-io is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with openfx-io.  If not, see <http://www.gnu.org/licenses/gpl-2.0.html>
 * ***** END LICENSE BLOCK ***** */

/*
 * OFX GenericOCIO plugin add-on.
 * Adds OpenColorIO functionality to any plugin.
 */

#include "GenericOCIO.h"

#include <cstring>
#include <cstdlib>
#ifdef DEBUG
#include <cstdio>
#define DBG(x) x
#else
#define DBG(x) (void)0
#endif
#include <string>
#include <stdexcept>
#include <ofxsParam.h>
#include <ofxsImageEffect.h>
#include <ofxsLog.h>
#include <ofxNatron.h>
#include <ofxsOGLUtilities.h>

// Use OpenGL function directly, no need to use ofxsOGLFunctions.h directly because we don't use OSMesa
#include <glad.h>

#ifdef OFX_IO_USING_OCIO
namespace OCIO = OCIO_NAMESPACE;
#endif

using std::string;

NAMESPACE_OFX_ENTER
    NAMESPACE_OFX_IO_ENTER

#if defined(OFX_SUPPORTS_OPENGLRENDER)

static const int LUT3D_EDGE_SIZE = 32;
static const char * g_fragShaderText = ""
                                       "\n"
                                       "uniform sampler2D tex1;\n"
                                       "uniform sampler3D tex2;\n"
                                       "\n"
                                       "void main()\n"
                                       "{\n"
                                       "    vec4 col = texture2D(tex1, gl_TexCoord[0].st);\n"
                                       "    gl_FragColor = OCIODisplay(col, tex2);\n"
                                       "}\n";


OCIOOpenGLContextData::OCIOOpenGLContextData()
    : procLut3D()
    , procShaderCacheID()
    , procLut3DCacheID()
    , procLut3DID(0)
    , procShaderProgramID(0)
    , procFragmentShaderID(0)
{
    if ( !ofxsLoadOpenGLOnce() ) {
        // We could use an error message here
        throwSuiteStatusException(kOfxStatFailed);
    }
}

OCIOOpenGLContextData::~OCIOOpenGLContextData()
{
    if (procLut3DID != 0) {
        glDeleteTextures(1, &procLut3DID);
    }
    if (procFragmentShaderID != 0) {
        glDeleteShader(procFragmentShaderID);
    }
    if (procShaderProgramID != 0) {
        glDeleteProgram(procShaderProgramID);
    }
}

static GLuint
compileShaderText(GLenum shaderType,
                  const char *text)
{
    GLuint shader;
    GLint stat;

    shader = glCreateShader(shaderType);
    glShaderSource(shader, 1, (const GLchar **) &text, NULL);
    glCompileShader(shader);
    glGetShaderiv(shader, GL_COMPILE_STATUS, &stat);

    if (!stat) {
        GLchar log[1000];
        GLsizei len;
        glGetShaderInfoLog(shader, 1000, &len, log);
        DBG( std::fprintf(stderr, "Error: problem compiling shader: %s\n", log) );

        return 0;
    }

    return shader;
}

static GLuint
linkShaders(GLuint fragShader)
{
    if (!fragShader) {
        return 0;
    }

    GLuint program = glCreateProgram();

    if (fragShader) {
        glAttachShader(program, fragShader);
    }

    glLinkProgram(program);

    /* check link */
    {
        GLint stat;
        glGetProgramiv(program, GL_LINK_STATUS, &stat);
        if (!stat) {
            GLchar log[1000];
            GLsizei len;
            glGetProgramInfoLog(program, 1000, &len, log);
            DBG( std::fprintf(stderr, "Shader link error:\n%s\n", log) );

            return 0;
        }
    }

    return program;
}

static void
allocateLut3D(GLuint* lut3dTexID,
              std::vector<float>* lut3D)
{
    glGenTextures(1, lut3dTexID);

    int num3Dentries = 3 * LUT3D_EDGE_SIZE * LUT3D_EDGE_SIZE * LUT3D_EDGE_SIZE;
    lut3D->resize(num3Dentries);
    memset(&(*lut3D)[0], 0, sizeof(float) * num3Dentries);

    glEnable(GL_TEXTURE_3D);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, *lut3dTexID);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
    glTexImage3D(GL_TEXTURE_3D, 0, GL_RGB32F_ARB,
                 LUT3D_EDGE_SIZE, LUT3D_EDGE_SIZE, LUT3D_EDGE_SIZE,
                 0, GL_RGB, GL_FLOAT, &(*lut3D)[0]);
}

#if defined(OFX_IO_USING_OCIO)

void
GenericOCIO::applyGL(const Texture* srcImg,
                     const OCIO::ConstProcessorRcPtr& processor,
                     std::vector<float>* lut3DParam,
                     unsigned int *lut3DTexIDParam,
                     unsigned int *shaderProgramIDParam,
                     unsigned int *fragShaderIDParam,
                     string* lut3DCacheIDParam,
                     string* shaderTextCacheIDParam)
{
    // Step 1: Create a GPU Shader Description
    OCIO::GpuShaderDesc shaderDesc;
    shaderDesc.setLanguage(OCIO::GPU_LANGUAGE_GLSL_1_0);
    shaderDesc.setFunctionName("OCIODisplay");
    shaderDesc.setLut3DEdgeLen(LUT3D_EDGE_SIZE);

    // Either we cache it all, or we don't
    assert( (!lut3DParam && !lut3DTexIDParam && !shaderProgramIDParam && !lut3DCacheIDParam && !shaderTextCacheIDParam) ||
            (lut3DParam && lut3DTexIDParam && shaderProgramIDParam && lut3DCacheIDParam && shaderTextCacheIDParam) );
    if ( (lut3DParam || lut3DTexIDParam || shaderProgramIDParam || lut3DCacheIDParam || shaderTextCacheIDParam) &&
         (!lut3DParam || !lut3DTexIDParam || !shaderProgramIDParam || !lut3DCacheIDParam || !shaderTextCacheIDParam) ) {
        throw std::invalid_argument("GenericOCIO::applyGL: Invalid caching arguments");
    }

    // Allocate CPU lut + init lut 3D texture, this should be done only once
    GLuint lut3dTexID = 0;
    std::vector<float>* lut3D = 0;
    if (lut3DParam) {
        lut3D = lut3DParam;
    } else {
        lut3D = new std::vector<float>;
    }
    if (lut3DTexIDParam) {
        lut3dTexID = *lut3DTexIDParam;
    }
    if (lut3D->size() == 0) {
        // The LUT was not allocated yet or the caller does not want to cache the lut
        // allocating at all
        allocateLut3D(&lut3dTexID, lut3D);
        if (lut3DTexIDParam) {
            *lut3DTexIDParam = lut3dTexID;
        }
    }

    glEnable(GL_TEXTURE_3D);
    // The lut3D texture should be cached to avoid calling glTexSubImage3D again
    string lut3dCacheID;
    if (lut3DCacheIDParam) {
        lut3dCacheID = processor->getGpuLut3DCacheID(shaderDesc);
    }

    if ( !lut3DCacheIDParam || (*lut3DCacheIDParam != lut3dCacheID) ) {
        // Unfortunately the LUT3D is not cached yet, or caller does not want caching
        processor->getGpuLut3D(&(*lut3D)[0], shaderDesc);

        /*for (std::size_t i = 0; i < lut3D->size(); ++i) {
            assert((*lut3D)[i] == (*lut3D)[i] && (*lut3D)[i] != std::numeric_limits<float>::infinity());
           }*/

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_3D, lut3dTexID);
        glTexSubImage3D(GL_TEXTURE_3D, 0,
                        0, 0, 0,
                        LUT3D_EDGE_SIZE, LUT3D_EDGE_SIZE, LUT3D_EDGE_SIZE,
                        GL_RGB, GL_FLOAT, &(*lut3D)[0]);

        // update the cache ID
        if (lut3DCacheIDParam) {
            *lut3DCacheIDParam = lut3dCacheID;
        }
    }

    if (!lut3DParam) {
        // Ensure we free the vector if we allocated it
        delete lut3D;
    }
    lut3D = 0;

    // The shader should be cached, to avoid generating it again
    string shaderCacheID;
    if (shaderTextCacheIDParam) {
        shaderCacheID = processor->getGpuShaderTextCacheID(shaderDesc);
    }

    GLuint programID;
    GLuint fragShaderID;
    if ( !shaderTextCacheIDParam || (*shaderTextCacheIDParam != shaderCacheID) ) {
        // Unfortunately the shader is not cached yet, or caller does not want caching
        string shaderString;
        shaderString += processor->getGpuShaderText(shaderDesc);
        shaderString += "\n";
        shaderString += g_fragShaderText;

        fragShaderID = compileShaderText( GL_FRAGMENT_SHADER, shaderString.c_str() );
        programID = linkShaders(fragShaderID);
        if (shaderProgramIDParam) {
            *shaderProgramIDParam = programID;
        }
        if (fragShaderIDParam) {
            *fragShaderIDParam = fragShaderID;
        }
        // update the cache ID
        if (shaderTextCacheIDParam) {
            *shaderTextCacheIDParam = shaderCacheID;
        }
    } else {
        programID = *shaderProgramIDParam;
        fragShaderID = *fragShaderIDParam;
    }


    // Bind textures and apply texture mapping
    glEnable(GL_TEXTURE_2D);
    glActiveTexture(GL_TEXTURE0);
    int srcTarget = srcImg->getTarget();
    glBindTexture( srcTarget, srcImg->getIndex() );
    glTexParameteri(srcTarget, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(srcTarget, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(srcTarget, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(srcTarget, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, lut3dTexID);

    glUseProgram(programID);
    glUniform1i(glGetUniformLocation(programID, "tex1"), 0);
    glUniform1i(glGetUniformLocation(programID, "tex2"), 1);


    const OfxRectI& srcBounds = srcImg->getBounds();

    glBegin(GL_QUADS);
    glTexCoord2f(0., 0.); glVertex2f(srcBounds.x1, srcBounds.y1);
    glTexCoord2f(0., 1.); glVertex2f(srcBounds.x1, srcBounds.y2);
    glTexCoord2f(1., 1.); glVertex2f(srcBounds.x2, srcBounds.y2);
    glTexCoord2f(1., 0.); glVertex2f(srcBounds.x2, srcBounds.y1);
    glEnd();

    glUseProgram(0);


    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_3D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    if (!lut3DTexIDParam) {
        glDeleteTextures(1, &lut3dTexID);
    }
    if (!shaderProgramIDParam) {
        glDeleteProgram(programID);
        glDeleteShader(fragShaderID);
    }
} // GenericOCIO::applyGL

#endif // defined(OFX_IO_USING_OCIO)

#endif // defined(OFX_SUPPORTS_OPENGLRENDER)

NAMESPACE_OFX_IO_EXIT
NAMESPACE_OFX_EXIT
