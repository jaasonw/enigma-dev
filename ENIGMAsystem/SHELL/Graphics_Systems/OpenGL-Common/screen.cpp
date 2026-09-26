/** Copyright (C) 2008-2014 Josh Ventura, Harijs Grinbergs
*** Copyright (C) 2010-2013 Alasdair Morrison
*** Copyright (C) 2013-2014 Robert B. Colton
***
*** This file is a part of the ENIGMA Development Environment.
***
*** ENIGMA is free software: you can redistribute it and/or modify it under the
*** terms of the GNU General Public License as published by the Free Software
*** Foundation, version 3 of the license or any later version.
***
*** This application and its source code is distributed AS-IS, WITHOUT ANY
*** WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
*** FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
*** details.
***
*** You should have received a copy of the GNU General Public License along
*** with this code. If not, see <http://www.gnu.org/licenses/>
**/

#include "profiler.h"
#include "screen.h"
#include "OpenGLHeaders.h"
#include "Graphics_Systems/General/GSscreen.h"

#include "Platforms/General/PFwindow.h"

using namespace enigma;

namespace enigma {

unsigned int bound_framebuffer = 0; //Shows the bound framebuffer, so glGetIntegerv(GL_FRAMEBUFFER_BINDING_EXT, &fbo); don't need to be called (they are very slow)

void graphics_set_viewport(float x, float y, float width, float height) {
  //NOTE: OpenGL viewports are bottom left unlike Direct3D viewports which are top left
  y = enigma_user::window_get_height() - y - height;
  glViewport(x,y,width,height);
  glScissor(x,y,width,height);
}

void scene_begin() {}

void scene_end() {
  gpuprof.end_frame();
  msaa_fbo_blit();
}

unsigned char* graphics_copy_screen_pixels(int x, int y, int width, int height, bool* flipped) {
  if (flipped) *flipped = true;

  const int bpp = 4; // bytes per pixel
  using namespace enigma_user;
  const double sx = double(window_get_region_width_scaled()) / window_get_region_width(),
               sy = double(window_get_region_height_scaled()) / window_get_region_height();
  const int ox = (window_get_width() - window_get_region_width_scaled()) / 2,
            oy = (window_get_height() - window_get_region_height_scaled()) / 2;
  const int rw = width*sx < 1 ? 1 : int(width*sx + .5), rh = height*sy < 1 ? 1 : int(height*sy + .5);
  const int rx = ox + int(x*sx + .5), ry = window_get_height() - (oy + int(y*sy + .5)) - rh;
  unsigned char* raw = new unsigned char[rw*rh*bpp];
  unsigned char* pxdata = new unsigned char[width*height*bpp];

  GLint prevFbo;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevFbo);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
  glReadPixels(rx,ry,rw,rh,GL_BGRA,GL_UNSIGNED_BYTE,raw);
  for (int j = 0; j < height; j++)
    for (int i = 0; i < width; i++) {
      const int si = int(i*sx) < rw ? int(i*sx) : rw - 1, sj = int(j*sy) < rh ? int(j*sy) : rh - 1;
      for (int c = 0; c < bpp; c++) pxdata[(j*width + i)*bpp + c] = raw[(sj*rw + si)*bpp + c];
    }
  delete[] raw;
  for (int i = 3; i < width*height*bpp; i += bpp) pxdata[i] = 255;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, prevFbo);
  return pxdata;
}

unsigned char* graphics_copy_screen_pixels(unsigned* fullwidth, unsigned* fullheight, bool* flipped) {
  if (flipped) *flipped = true;

  const int fw = enigma_user::window_get_region_width(),
            fh = enigma_user::window_get_region_height();

  *fullwidth = fw;
  *fullheight = fh;
  return graphics_copy_screen_pixels(0,0,fw,fh,flipped);
}

} // namespace enigma
