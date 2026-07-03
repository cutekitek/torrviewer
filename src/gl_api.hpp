#pragma once

#include "opengl.hpp"

namespace torrview {

using GlViewportProc = void(APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
using GlScissorProc = void(APIENTRY*)(GLint, GLint, GLsizei, GLsizei);
using GlClearColorProc = void(APIENTRY*)(GLfloat, GLfloat, GLfloat, GLfloat);
using GlClearProc = void(APIENTRY*)(GLbitfield);
using GlEnableProc = void(APIENTRY*)(GLenum);
using GlDisableProc = void(APIENTRY*)(GLenum);

struct BasicGlApi {
  GlViewportProc viewport = nullptr;
  GlScissorProc scissor = nullptr;
  GlClearColorProc clear_color = nullptr;
  GlClearProc clear = nullptr;
  GlEnableProc enable = nullptr;
  GlDisableProc disable = nullptr;
};

} // namespace torrview
