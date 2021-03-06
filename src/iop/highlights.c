/*
   This file is part of darktable,
   copyright (c) 2009--2010 johannes hanika.

   darktable is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   darktable is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with darktable.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#include <stdlib.h>
#include <math.h>
#include <assert.h>
#include <string.h>
#if defined(__SSE__)
#include <xmmintrin.h>
#endif
#include "bauhaus/bauhaus.h"
#include "common/opencl.h"
#include "control/control.h"
#include "develop/develop.h"
#include "develop/imageop.h"
#include "develop/imageop_math.h"
#include "gui/accelerators.h"
#include "gui/gtk.h"
#include "iop/iop_api.h"

#include <gtk/gtk.h>
#include <inttypes.h>


DT_MODULE_INTROSPECTION(2, dt_iop_highlights_params_t)

typedef enum dt_iop_highlights_mode_t
{
  DT_IOP_HIGHLIGHTS_CLIP = 0,
  DT_IOP_HIGHLIGHTS_LCH = 1,
  DT_IOP_HIGHLIGHTS_INPAINT = 2,
} dt_iop_highlights_mode_t;

typedef struct dt_iop_highlights_params_t
{
  dt_iop_highlights_mode_t mode;
  float blendL, blendC, blendh; // unused
  float clip;
} dt_iop_highlights_params_t;

typedef struct dt_iop_highlights_gui_data_t
{
  GtkWidget *clip;
  GtkWidget *mode;
} dt_iop_highlights_gui_data_t;

typedef dt_iop_highlights_params_t dt_iop_highlights_data_t;

typedef struct dt_iop_highlights_global_data_t
{
  int kernel_highlights_1f;
  int kernel_highlights_4f;
} dt_iop_highlights_global_data_t;

const char *name()
{
  return _("highlight reconstruction");
}

int groups()
{
  return IOP_GROUP_BASIC;
}

int flags()
{
  return IOP_FLAGS_ALLOW_TILING | IOP_FLAGS_ONE_INSTANCE;
}

int legacy_params(dt_iop_module_t *self, const void *const old_params, const int old_version,
                  void *new_params, const int new_version)
{
  if(old_version == 1 && new_version == 2)
  {
    memcpy(new_params, old_params, sizeof(dt_iop_highlights_params_t) - sizeof(float));
    dt_iop_highlights_params_t *n = (dt_iop_highlights_params_t *)new_params;
    n->clip = 1.0f;
    return 0;
  }
  return 1;
}

#ifdef HAVE_OPENCL
int process_cl(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, cl_mem dev_in, cl_mem dev_out,
               const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  dt_iop_highlights_data_t *d = (dt_iop_highlights_data_t *)piece->data;
  dt_iop_highlights_global_data_t *gd = (dt_iop_highlights_global_data_t *)self->data;

  cl_int err = -999;
  const int devid = piece->pipe->devid;
  const int width = roi_in->width;
  const int height = roi_in->height;

  size_t sizes[] = { ROUNDUPWD(width), ROUNDUPHT(height), 1 };
  const float clip = d->clip
                     * fminf(piece->pipe->processed_maximum[0],
                             fminf(piece->pipe->processed_maximum[1], piece->pipe->processed_maximum[2]));
  const int filters = dt_image_filter(&piece->pipe->image);
  if(dt_dev_pixelpipe_uses_downsampled_input(piece->pipe) || !filters)
  {
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_4f, 0, sizeof(cl_mem), (void *)&dev_in);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_4f, 1, sizeof(cl_mem), (void *)&dev_out);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_4f, 2, sizeof(int), (void *)&width);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_4f, 3, sizeof(int), (void *)&height);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_4f, 4, sizeof(int), (void *)&d->mode);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_4f, 5, sizeof(float), (void *)&clip);
    err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_highlights_4f, sizes);
    if(err != CL_SUCCESS) goto error;
  }
  else
  {
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_1f, 0, sizeof(cl_mem), (void *)&dev_in);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_1f, 1, sizeof(cl_mem), (void *)&dev_out);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_1f, 2, sizeof(int), (void *)&width);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_1f, 3, sizeof(int), (void *)&height);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_1f, 4, sizeof(int), (void *)&d->mode);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_1f, 5, sizeof(float), (void *)&clip);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_1f, 6, sizeof(int), (void *)&roi_out->x);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_1f, 7, sizeof(int), (void *)&roi_out->y);
    dt_opencl_set_kernel_arg(devid, gd->kernel_highlights_1f, 8, sizeof(int), (void *)&filters);
    err = dt_opencl_enqueue_kernel_2d(devid, gd->kernel_highlights_1f, sizes);
    if(err != CL_SUCCESS) goto error;
  }

  // update processed maximum
  const float m = fmaxf(fmaxf(
        piece->pipe->processed_maximum[0],
        piece->pipe->processed_maximum[1]),
      piece->pipe->processed_maximum[2]);
  for(int k=0;k<3;k++) piece->pipe->processed_maximum[k] = m;

  return TRUE;

error:
  dt_print(DT_DEBUG_OPENCL, "[opencl_highlights] couldn't enqueue kernel! %d\n", err);
  return FALSE;
}
#endif

int output_bpp(dt_iop_module_t *module, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  if(!dt_dev_pixelpipe_uses_downsampled_input(pipe) && (pipe->image.flags & DT_IMAGE_RAW))
    return sizeof(float);
  else
    return 4 * sizeof(float);
}

/* interpolate value for a pixel, ideal via ratio to nearby pixel */
static inline float interp_pix_xtrans(const int ratio_next,
                                      const ssize_t offset_next,
                                      const float clip0, const float clip_next,
                                      const float *const in,
                                      const float *const ratios)
{
  assert(ratio_next != 0);
  // it's OK to exceed clipping of current pixel's color based on a
  // neighbor -- that is the purpose of interpolating highlight
  // colors
  const float clip_val = fmaxf(clip0, clip_next);
  if(in[offset_next] >= clip_next - 1e-5f)
  {
    // next pixel is also clipped
    return clip_val;
  }
  else
  {
    // set this pixel in ratio to the next
    assert(ratio_next != 0);
    if (ratio_next > 0)
      return fminf(in[offset_next] / ratios[ratio_next], clip_val);
    else
      return fminf(in[offset_next] * ratios[-ratio_next], clip_val);
  }
}

static inline void interpolate_color_xtrans(const void *const ivoid, void *const ovoid,
                                            const dt_iop_roi_t *const roi_in,
                                            const dt_iop_roi_t *const roi_out,
                                            int dim, int dir, int other,
                                            const float *const clip,
                                            const uint8_t (*const xtrans)[6],
                                            const int pass)
{
  // In Bayer each row/col has only green/red or green/blue
  // transitions, hence can reconstruct color by single ratio per
  // row. In x-trans there can be transitions between arbitary colors
  // in a row/col (and 2x2 green blocks which provide no color
  // transition information). Hence calculate multiple color ratios
  // for each row/col.

  // Lookup for color ratios, e.g. red -> blue is roff[0][2] and blue
  // -> red is roff[2][0]. Returned value is an index into ratios. If
  // negative, then need to invert the ratio. Identity color
  // transitions aren't used.
  const int roff[3][3] = {{ 0, -1, -2},
                          { 1,  0, -3},
                          { 2,  3,  0}};
  // record ratios of color transitions 0:unused, 1:RG, 2:RB, and 3:GB
  float ratios[4] = {1.0f, 1.0f, 1.0f, 1.0f};

  // passes are 0:+x, 1:-x, 2:+y, 3:-y
  // dims are 0:traverse a row, 1:traverse a column
  // dir is 1:left to right, -1: right to left
  int i = (dim == 0) ? 0 : other;
  int j = (dim == 0) ? other : 0;
  const ssize_t offs = (dim ? roi_out->width : 1) * ((dir < 0) ? -1 : 1);
  const ssize_t offl = offs - (dim ? 1 : roi_out->width);
  const ssize_t offr = offs + (dim ? 1 : roi_out->width);
  int beg, end;
  if(dir == 1)
  {
    beg = 0;
    end = (dim == 0) ? roi_out->width : roi_out->height;
  }
  else
  {
    beg = ((dim == 0) ? roi_out->width : roi_out->height) - 1;
    end = -1;
  }

  float *in, *out;
  if(dim == 1)
  {
    out = (float *)ovoid + (size_t)i + (size_t)beg * roi_out->width;
    in = (float *)ivoid + (size_t)i + (size_t)beg * roi_in->width;
  }
  else
  {
    out = (float *)ovoid + (size_t)beg + (size_t)j * roi_out->width;
    in = (float *)ivoid + (size_t)beg + (size_t)j * roi_in->width;
  }

  for(int k = beg; k != end; k += dir)
  {
    if(dim == 1)
      j = k;
    else
      i = k;

    const uint8_t f0 = FCxtrans(j, i, roi_in, xtrans);
    const uint8_t f1 = FCxtrans(dim ? (j + dir) : j, dim ? i : (i + dir), roi_in, xtrans);
    const uint8_t fl = FCxtrans(dim ? (j + dir) : (j - 1), dim ? (i - 1) : (i + dir), roi_in, xtrans);
    const uint8_t fr = FCxtrans(dim ? (j + dir) : (j + 1), dim ? (i + 1) : (i + dir), roi_in, xtrans);
    const float clip0 = clip[f0];
    const float clip1 = clip[f1];
    const float clipl = clip[fl];
    const float clipr = clip[fr];
    const float clip_max = fmaxf(fmaxf(clip[0], clip[1]), clip[2]);

    if(i == 0 || i == roi_out->width - 1 || j == 0 || j == roi_out->height - 1)
    {
      if(pass == 3) out[0] = fminf(clip_max, in[0]);
    }
    else
    {
      // ratio to next pixel if this & next are unclamped and not in
      // 2x2 green block
      if ((f0 != f1) &&
          (in[0] < clip0 && in[0] > 1e-5f) &&
          (in[offs] < clip1 && in[offs] > 1e-5f))
      {
        const int r = roff[f0][f1];
        assert(r != 0);
        if (r > 0)
          ratios[r] = (3.f * ratios[r] + (in[offs] / in[0])) / 4.f;
        else
          ratios[-r] = (3.f * ratios[-r] + (in[0] / in[offs])) / 4.f;
      }

      if(in[0] >= clip0 - 1e-5f)
      {
        // interplate color for clipped pixel
        float add;
        if(f0 != f1)
          // next pixel is different color
          add =
            interp_pix_xtrans(roff[f0][f1], offs, clip0, clip1, in, ratios);
        else
          // at start of 2x2 green block, look diagonally
          add = (fl != f0) ?
            interp_pix_xtrans(roff[f0][fl], offl, clip0, clipl, in, ratios) :
            interp_pix_xtrans(roff[f0][fr], offr, clip0, clipr, in, ratios);

        if(pass == 0)
          out[0] = add;
        else if(pass == 3)
          out[0] = fminf(clip_max, (out[0] + add) / 4.0f);
        else
          out[0] += add;
      }
      else
      {
        // pixel is not clipped
        if(pass == 3) out[0] = in[0];
      }
    }
    out += offs;
    in += offs;
  }
}

static inline void interpolate_color(const void *const ivoid, void *const ovoid,
                                     const dt_iop_roi_t *const roi_out, int dim, int dir, int other,
                                     const float *clip, const uint32_t filters, const int pass)
{
  float ratio = 1.0f;
  float *in, *out;

  int i = 0, j = 0;
  if(dim == 0)
    j = other;
  else
    i = other;
  ssize_t offs = dim ? roi_out->width : 1;
  if(dir < 0) offs = -offs;
  int beg, end;
  if(dim == 0 && dir == 1)
  {
    beg = 0;
    end = roi_out->width;
  }
  else if(dim == 0 && dir == -1)
  {
    beg = roi_out->width - 1;
    end = -1;
  }
  else if(dim == 1 && dir == 1)
  {
    beg = 0;
    end = roi_out->height;
  }
  else if(dim == 1 && dir == -1)
  {
    beg = roi_out->height - 1;
    end = -1;
  }
  else
    return;

  if(dim == 1)
  {
    out = (float *)ovoid + i + (size_t)beg * roi_out->width;
    in = (float *)ivoid + i + (size_t)beg * roi_out->width;
  }
  else
  {
    out = (float *)ovoid + beg + (size_t)j * roi_out->width;
    in = (float *)ivoid + beg + (size_t)j * roi_out->width;
  }
  for(int k = beg; k != end; k += dir)
  {
    if(dim == 1)
      j = k;
    else
      i = k;
    const float clip0 = clip[FC(j, i, filters)];
    const float clip1 = clip[FC(dim ? (j + 1) : j, dim ? i : (i + 1), filters)];
    if(i == 0 || i == roi_out->width - 1 || j == 0 || j == roi_out->height - 1)
    {
      if(pass == 3) out[0] = in[0];
    }
    else
    {
      if(in[0] < clip0 && in[0] > 1e-5f)
      { // both are not clipped
        if(in[offs] < clip1 && in[offs] > 1e-5f)
        { // update ratio, exponential decay. ratio = in[odd]/in[even]
          if(k & 1)
            ratio = (3.0f * ratio + in[0] / in[offs]) / 4.0f;
          else
            ratio = (3.0f * ratio + in[offs] / in[0]) / 4.0f;
        }
      }

      if(in[0] >= clip0 - 1e-5f)
      { // in[0] is clipped, restore it as in[1] adjusted according to ratio
        float add = 0.0f;
        if(in[offs] >= clip1 - 1e-5f)
          add = fmaxf(clip0, clip1);
        else if(k & 1)
          add = in[offs] * ratio;
        else
          add = in[offs] / ratio;

        if(pass == 0)
          out[0] = add;
        else if(pass == 3)
          out[0] = (out[0] + add) / 4.0f;
        else
          out[0] += add;
      }
      else
      {
        if(pass == 3) out[0] = in[0];
      }
    }
    out += offs;
    in += offs;
  }
}

static void process_lch_bayer(
    const void *const ivoid,
    void *const ovoid,
    const int width,
    const int height,
    const float clip)
{
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) default(none)
#endif
  for(int j = 0; j < height; j++)
  {
    float *out = (float *)ovoid + (size_t)width * j;
    float *in = (float *)ivoid + (size_t)width * j;
    for(int i = 0; i < width; i++)
    {
      if(i == 0 || i == width - 1 || j == 0 || j == height - 1)
      {
        // fast path for border
        out[0] = in[0];
      }
      else
      {
        // analyse one bayer block to get same number of rggb pixels each time
        const float near_clip = 0.96f * clip;
        const float post_clip = 1.10f * clip;
        float blend = 0.0f;
        float mean = 0.0f;
        for(int jj = 0; jj <= 1; jj++)
        {
          for(int ii = 0; ii <= 1; ii++)
          {
            const float val = in[(size_t)jj * width + ii];
            mean += val * 0.25f;
            blend += (fminf(post_clip, val) - near_clip) / (post_clip - near_clip);
          }
        }
        blend = CLAMP(blend, 0.0f, 1.0f);
        if(blend > 0)
        {
          // recover:
          out[0] = blend * mean + (1.f - blend) * in[0];
        }
        else
          out[0] = in[0];
      }
      out++;
      in++;
    }
  }
}

static void process_lch_xtrans(
    const void *const ivoid,
    void *const ovoid,
    const int width,
    const int height,
    const float clip,
    const dt_iop_roi_t *const roi_in,
    const uint8_t (*const xtrans)[6])
{
#ifdef _OPENMP
#pragma omp parallel for schedule(static) default(none)
#endif
  for(int j = 0; j < height; j++)
  {
    float *out = (float *)ovoid + (size_t)width * j;
    float *in = (float *)ivoid + (size_t)width * j;
    for(int i = 0; i < width; i++)
    {
      if(i < 3 || i > width - 3 || j < 3 || j > height - 3)
      {
        // fast path for border
        out[0] = MIN(clip, in[0]);
      }
      else
      {
        const float near_clip = 0.96f * clip;
        const float post_clip = 1.10f * clip;
        float blend[3] = {0.0f};
        float mean[3] = {0.0f};
        int cnt[3] = {0};
        for(int jj = -1; jj <= 1; jj++)
        {
          for(int ii = -1; ii <= 1; ii++)
          {
            const float val = in[(size_t)jj * width + ii];
            const int c = FCxtrans(j+jj, i+ii, roi_in, xtrans);
            mean[c] += val;
            cnt[c]++;
            blend[c] = fmaxf(blend[c], (fminf(post_clip, val) - near_clip) / (post_clip - near_clip));
          }
        }
        if(blend[0] + blend[1] + blend[2] > 0)
        { // recover:
          // options: use max colour and mean blend weight.
          // const float m = fmaxf(mean[0]/cnt[0], fmaxf(mean[1]/cnt[1], mean[2]/cnt[2]));
          // const float b = (blend[0] + blend[1] + blend[2])/3.0f;
          const float m = (mean[0]/cnt[0] + mean[1]/cnt[1] + mean[2]/cnt[2])/3.0f;
          const float b = fmaxf(fmaxf(blend[0], blend[1]), blend[2]);
          out[0] = b * m + (1.0f-b) * in[0];
        }
        else
          out[0] = in[0];
      }
      out++;
      in++;
    }
  }
}

static void process_clip_plain(dt_dev_pixelpipe_iop_t *piece, const void *const ivoid, void *const ovoid,
                               const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out,
                               const float clip)
{
  const float *const in = (const float *const)ivoid;
  float *const out = (float *const)ovoid;

  if(!dt_dev_pixelpipe_uses_downsampled_input(piece->pipe) && dt_image_filter(&piece->pipe->image))
  { // raw mosaic
#ifdef _OPENMP
#pragma omp parallel for SIMD() default(none) schedule(static)
#endif
    for(size_t k = 0; k < (size_t)roi_out->width * roi_out->height; k++)
    {
      out[k] = MIN(clip, in[k]);
    }
  }
  else
  {
    const int ch = piece->colors;

#ifdef _OPENMP
#pragma omp parallel for SIMD() default(none) schedule(static)
#endif
    for(size_t k = 0; k < (size_t)ch * roi_out->width * roi_out->height; k++)
    {
      out[k] = MIN(clip, in[k]);
    }
  }
}

#if defined(__SSE__)
static void process_clip_sse2(dt_dev_pixelpipe_iop_t *piece, const void *const ivoid, void *const ovoid,
                              const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out,
                              const float clip)
{
  if(!dt_dev_pixelpipe_uses_downsampled_input(piece->pipe) && dt_image_filter(&piece->pipe->image))
  { // raw mosaic
    const __m128 clipm = _mm_set1_ps(clip);
    const size_t n = (size_t)roi_out->height * roi_out->width;
    float *const out = (float *)ovoid;
    float *const in = (float *)ivoid;
#ifdef _OPENMP
#pragma omp parallel for schedule(static) default(none)
#endif
    for(size_t j = 0; j < (n & ~3u); j += 4) _mm_stream_ps(out + j, _mm_min_ps(clipm, _mm_load_ps(in + j)));
    _mm_sfence();
    // lets see if there's a non-multiple of four rest to process:
    if(n & 3)
      for(size_t j = n & ~3u; j < n; j++) out[j] = MIN(clip, in[j]);
  }
  else
  {
    const __m128 clipm = _mm_set1_ps(clip);
    const int ch = piece->colors;

#ifdef _OPENMP
#pragma omp parallel for default(none) schedule(static)
#endif
    for(int j = 0; j < roi_out->height; j++)
    {
      float *out = (float *)ovoid + (size_t)ch * roi_out->width * j;
      float *in = (float *)ivoid + (size_t)ch * roi_in->width * j;
      for(int i = 0; i < roi_out->width; i++, in += ch, out += ch)
      {
        _mm_stream_ps(out, _mm_min_ps(clipm, _mm_set_ps(in[3], in[2], in[1], in[0])));
      }
    }
    _mm_sfence();
  }
}
#endif

static void process_clip(dt_dev_pixelpipe_iop_t *piece, const void *const ivoid, void *const ovoid,
                         const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out,
                         const float clip)
{
  if(darktable.codepath.OPENMP_SIMD) process_clip_plain(piece, ivoid, ovoid, roi_in, roi_out, clip);
#if defined(__SSE__)
  else if(darktable.codepath.SSE2)
    process_clip_sse2(piece, ivoid, ovoid, roi_in, roi_out, clip);
#endif
  else
    dt_unreachable_codepath();
}

void process(struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, const void *const ivoid,
             void *const ovoid, const dt_iop_roi_t *const roi_in, const dt_iop_roi_t *const roi_out)
{
  const int filters = dt_image_filter(&piece->pipe->image);
  dt_iop_highlights_data_t *data = (dt_iop_highlights_data_t *)piece->data;

  const float clip
      = data->clip * fminf(piece->pipe->processed_maximum[0],
                           fminf(piece->pipe->processed_maximum[1], piece->pipe->processed_maximum[2]));
  // const int ch = piece->colors;
  if(dt_dev_pixelpipe_uses_downsampled_input(piece->pipe) || !filters)
  {
    process_clip(piece, ivoid, ovoid, roi_in, roi_out, clip);
    for(int k=0;k<3;k++)
      piece->pipe->processed_maximum[k] = fminf(piece->pipe->processed_maximum[0],
          fminf(piece->pipe->processed_maximum[1], piece->pipe->processed_maximum[2]));
    return;
  }

  switch(data->mode)
  {
    case DT_IOP_HIGHLIGHTS_INPAINT: // a1ex's (magiclantern) idea of color inpainting:
    {
      const float clips[4] = { 0.987 * data->clip * piece->pipe->processed_maximum[0],
                               0.987 * data->clip * piece->pipe->processed_maximum[1],
                               0.987 * data->clip * piece->pipe->processed_maximum[2], clip };

      if(filters == 9u)
      {
        const uint8_t(*const xtrans)[6] = (const uint8_t(*const)[6])self->dev->image_storage.xtrans;
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) default(none)
#endif
        for(int j = 0; j < roi_out->height; j++)
        {
          interpolate_color_xtrans(ivoid, ovoid, roi_in, roi_out, 0, 1, j, clips, xtrans, 0);
          interpolate_color_xtrans(ivoid, ovoid, roi_in, roi_out, 0, -1, j, clips, xtrans, 1);
        }
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) default(none)
#endif
        for(int i = 0; i < roi_out->width; i++)
        {
          interpolate_color_xtrans(ivoid, ovoid, roi_in, roi_out, 1, 1, i, clips, xtrans, 2);
          interpolate_color_xtrans(ivoid, ovoid, roi_in, roi_out, 1, -1, i, clips, xtrans, 3);
        }
      }
      else
      {
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) default(none) shared(data, piece)
#endif
        for(int j = 0; j < roi_out->height; j++)
        {
          interpolate_color(ivoid, ovoid, roi_out, 0, 1, j, clips, filters, 0);
          interpolate_color(ivoid, ovoid, roi_out, 0, -1, j, clips, filters, 1);
        }

// up/down directions
#ifdef _OPENMP
#pragma omp parallel for schedule(dynamic) default(none) shared(data, piece)
#endif
        for(int i = 0; i < roi_out->width; i++)
        {
          interpolate_color(ivoid, ovoid, roi_out, 1, 1, i, clips, filters, 2);
          interpolate_color(ivoid, ovoid, roi_out, 1, -1, i, clips, filters, 3);
        }
      }
      break;
    }
    case DT_IOP_HIGHLIGHTS_LCH:
      if(filters == 9u)
        process_lch_xtrans(ivoid, ovoid, roi_out->width, roi_out->height, clip, roi_in,
                           (const uint8_t(*const)[6])self->dev->image_storage.xtrans);
      else
        process_lch_bayer(ivoid, ovoid, roi_out->width, roi_out->height, clip);
      break;
    default:
    case DT_IOP_HIGHLIGHTS_CLIP:
      process_clip(piece, ivoid, ovoid, roi_in, roi_out, clip);
      break;
  }

  // update processed maximum
  const float m = fmaxf(fmaxf(
        piece->pipe->processed_maximum[0],
        piece->pipe->processed_maximum[1]),
      piece->pipe->processed_maximum[2]);
  for(int k=0;k<3;k++) piece->pipe->processed_maximum[k] = m;

  if(piece->pipe->mask_display) dt_iop_alpha_copy(ivoid, ovoid, roi_out->width, roi_out->height);
}

static void clip_callback(GtkWidget *slider, dt_iop_module_t *self)
{
  if(self->dt->gui->reset) return;
  dt_iop_highlights_params_t *p = (dt_iop_highlights_params_t *)self->params;
  p->clip = dt_bauhaus_slider_get(slider);
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

static void mode_changed(GtkWidget *combo, dt_iop_module_t *self)
{
  dt_iop_highlights_params_t *p = (dt_iop_highlights_params_t *)self->params;
  p->mode = dt_bauhaus_combobox_get(combo);
  if(p->mode > DT_IOP_HIGHLIGHTS_INPAINT) p->mode = DT_IOP_HIGHLIGHTS_INPAINT;
  dt_dev_add_history_item(darktable.develop, self, TRUE);
}

void commit_params(struct dt_iop_module_t *self, dt_iop_params_t *p1, dt_dev_pixelpipe_t *pipe,
                   dt_dev_pixelpipe_iop_t *piece)
{
  dt_iop_highlights_params_t *p = (dt_iop_highlights_params_t *)p1;
  dt_iop_highlights_data_t *d = (dt_iop_highlights_data_t *)piece->data;
  memcpy(d, p, sizeof(*p));

  piece->process_cl_ready = 1;

  // x-trans images not implemented in OpenCL yet
  if(pipe->image.filters == 9u) piece->process_cl_ready = 0;

  // no OpenCL for DT_IOP_HIGHLIGHTS_INPAINT yet.
  if(d->mode == DT_IOP_HIGHLIGHTS_INPAINT) piece->process_cl_ready = 0;
}

void init_global(dt_iop_module_so_t *module)
{
  const int program = 2; // basic.cl, from programs.conf
  dt_iop_highlights_global_data_t *gd
      = (dt_iop_highlights_global_data_t *)malloc(sizeof(dt_iop_highlights_global_data_t));
  module->data = gd;
  gd->kernel_highlights_1f = dt_opencl_create_kernel(program, "highlights_1f");
  gd->kernel_highlights_4f = dt_opencl_create_kernel(program, "highlights_4f");
}

void cleanup_global(dt_iop_module_so_t *module)
{
  dt_iop_highlights_global_data_t *gd = (dt_iop_highlights_global_data_t *)module->data;
  dt_opencl_free_kernel(gd->kernel_highlights_4f);
  dt_opencl_free_kernel(gd->kernel_highlights_1f);
  free(module->data);
  module->data = NULL;
}

void init_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = malloc(sizeof(dt_iop_highlights_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
}

void cleanup_pipe(struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  free(piece->data);
  piece->data = NULL;
}

void gui_update(struct dt_iop_module_t *self)
{
  dt_iop_module_t *module = (dt_iop_module_t *)self;
  dt_iop_highlights_gui_data_t *g = (dt_iop_highlights_gui_data_t *)self->gui_data;
  dt_iop_highlights_params_t *p = (dt_iop_highlights_params_t *)module->params;
  dt_bauhaus_slider_set(g->clip, p->clip);
  dt_bauhaus_combobox_set(g->mode, p->mode);
}

void reload_defaults(dt_iop_module_t *module)
{
  dt_iop_highlights_params_t tmp = (dt_iop_highlights_params_t){
    .mode = DT_IOP_HIGHLIGHTS_CLIP, .blendL = 1.0, .blendC = 0.0, .blendh = 0.0, .clip = 1.0
  };

  // we might be called from presets update infrastructure => there is no image
  if(!module->dev) goto end;

  // only on for raw images:
  if(dt_image_is_raw(&module->dev->image_storage))
    module->default_enabled = 1;
  else
    module->default_enabled = 0;

end:
  memcpy(module->params, &tmp, sizeof(dt_iop_highlights_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_highlights_params_t));
}

void init(dt_iop_module_t *module)
{
  // module->data = malloc(sizeof(dt_iop_highlights_data_t));
  module->params = calloc(1, sizeof(dt_iop_highlights_params_t));
  module->default_params = calloc(1, sizeof(dt_iop_highlights_params_t));
  module->priority = 61; // module order created by iop_dependencies.py, do not edit!
  module->default_enabled = 1;
  module->params_size = sizeof(dt_iop_highlights_params_t);
  module->gui_data = NULL;
}

void cleanup(dt_iop_module_t *module)
{
  free(module->params);
  module->params = NULL;
}

void gui_init(struct dt_iop_module_t *self)
{
  self->gui_data = malloc(sizeof(dt_iop_highlights_gui_data_t));
  dt_iop_highlights_gui_data_t *g = (dt_iop_highlights_gui_data_t *)self->gui_data;
  dt_iop_highlights_params_t *p = (dt_iop_highlights_params_t *)self->params;

  self->widget = gtk_box_new(GTK_ORIENTATION_VERTICAL, DT_BAUHAUS_SPACE);

  g->mode = dt_bauhaus_combobox_new(self);
  gtk_box_pack_start(GTK_BOX(self->widget), g->mode, TRUE, TRUE, 0);
  dt_bauhaus_widget_set_label(g->mode, NULL, _("method"));
  dt_bauhaus_combobox_add(g->mode, _("clip highlights"));
  dt_bauhaus_combobox_add(g->mode, _("reconstruct in LCh"));
  dt_bauhaus_combobox_add(g->mode, _("reconstruct color"));
  gtk_widget_set_tooltip_text(g->mode, _("highlight reconstruction method"));

  g->clip = dt_bauhaus_slider_new_with_range(self, 0.0, 2.0, 0.01, p->clip, 3);
  gtk_widget_set_tooltip_text(g->clip, _("manually adjust the clipping threshold against "
                                         "magenta highlights (you shouldn't ever need to touch this)"));
  dt_bauhaus_widget_set_label(g->clip, NULL, _("clipping threshold"));
  gtk_box_pack_start(GTK_BOX(self->widget), g->clip, TRUE, TRUE, 0);


  g_signal_connect(G_OBJECT(g->clip), "value-changed", G_CALLBACK(clip_callback), self);
  g_signal_connect(G_OBJECT(g->mode), "value-changed", G_CALLBACK(mode_changed), self);
}

void gui_cleanup(struct dt_iop_module_t *self)
{
  free(self->gui_data);
  self->gui_data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-spaces modified;
