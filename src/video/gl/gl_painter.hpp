//  SuperTux
//  Copyright (C) 2014 Ingo Ruhnke <grumbel@gmail.com>
//
//  This program is free software: you can redistribute it and/or modify
//  it under the terms of the GNU General Public License as published by
//  the Free Software Foundation, either version 3 of the License, or
//  (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program.  If not, see <http://www.gnu.org/licenses/>.

#ifndef HEADER_SUPERTUX_VIDEO_GL_GL_PAINTER_HPP
#define HEADER_SUPERTUX_VIDEO_GL_GL_PAINTER_HPP

#include "video/painter.hpp"

#ifdef USE_GLBINDING
#include <glbinding/gl/gl.h>
using namespace gl;
#else
#include <GL/glew.h>

#include "SDL_opengl.h"
#endif

class GLVideoSystem;
struct DrawingRequest;

class GLPainter : public Painter
{
private:
  static GLuint s_last_texture;

private:
  GLVideoSystem& m_video_system;

public:
  GLPainter(GLVideoSystem& video_system);

  void draw_texture(const DrawingRequest& request);
  void draw_texture_batch(const DrawingRequest& request);
  void draw_gradient(const DrawingRequest& request);
  void draw_filled_rect(const DrawingRequest& request);
  void draw_inverse_ellipse(const DrawingRequest& request);
  void draw_line(const DrawingRequest& request);
  void draw_triangle(const DrawingRequest& request);

private:
  GLPainter(const GLPainter&) = delete;
  GLPainter& operator=(const GLPainter&) = delete;
};

#endif

/* EOF */
