/*
    This file is part of darktable,
    copyright (c) 2011 johannes hanika.

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
#include "develop/imageop.h"
#include "develop/masks.h"
#include "develop/blend.h"
#include "control/control.h"
#include "control/conf.h"
#include "gui/gtk.h"
#include <gtk/gtk.h>
#include <stdlib.h>


// this is the version of the modules parameters,
// and includes version information about compile-time dt
DT_MODULE(3)

typedef struct dt_iop_spots_params_t
{
  int clone_algo[64];
}
dt_iop_spots_params_t;

typedef struct dt_iop_spots_gui_data_t
{
  GtkLabel *label;
  GtkWidget *bt_curve, *bt_circle;
}
dt_iop_spots_gui_data_t;

typedef struct dt_iop_spots_params_t dt_iop_spots_data_t;

// this returns a translatable name
const char *name()
{
  return _("spot removal");
}

int
groups ()
{
  return IOP_GROUP_CORRECT;
}

int flags()
{
  return IOP_FLAGS_SUPPORTS_BLENDING | IOP_FLAGS_NO_MASKS;
}

int legacy_params (dt_iop_module_t *self, const void *const old_params, const int old_version, void *new_params, const int new_version)
{
  /*if(old_version == 1 && new_version == 2)
  {
    typedef struct dt_iop_spots_v1_t
    {
      float x,y;
      float xc,yc;
      float radius;
    }
    dt_iop_spots_v1_t;
    typedef struct dt_iop_spots_params_v1_t
    {
      int num_spots;
      dt_iop_spots_v1_t spot[32];
    }
    dt_iop_spots_params_v1_t;

    dt_iop_spots_params_v1_t *o = (dt_iop_spots_params_v1_t *)old_params;
    dt_iop_spots_params_t *n = (dt_iop_spots_params_t *)new_params;
    dt_iop_spots_params_t *d = (dt_iop_spots_params_t *)self->default_params;

    *n = *d;  // start with a fresh copy of default parameters
    n->num_spots = o->num_spots;
    for (int i=0; i<n->num_spots; i++)
    {
      n->spot[i].version = 1;
      n->spot[i].opacity = 1.0f;
      n->spot[i].spot.center[0] = o->spot[i].x;
      n->spot[i].spot.center[1] = o->spot[i].y;
      n->spot[i].source[0] = o->spot[i].xc;
      n->spot[i].source[1] = o->spot[i].yc;
      n->spot[i].spot.border = 0.0f;
      n->spot[i].spot.radius = o->spot[i].radius;
    }
    return 0;
  }*/
  return 1;
}

static void _add_curve(GtkWidget *widget, GdkEventButton *e, dt_iop_module_t *self)
{
  //we want to be sure that the iop has focus
  dt_iop_request_focus(self);
  //we create the new form
  dt_masks_form_t *form = dt_masks_create(DT_MASKS_CURVE|DT_MASKS_CLONE);
  dt_masks_change_form_gui(form);
  darktable.develop->form_gui->creation = TRUE;
  darktable.develop->form_gui->creation_module = self;
  dt_control_queue_redraw_center();  
}
static void _add_circle(GtkWidget *widget, GdkEventButton *e, dt_iop_module_t *self)
{
  //we want to be sure that the iop has focus
  dt_iop_request_focus(self);
  //we create the new form
  dt_masks_form_t *spot = dt_masks_create(DT_MASKS_CIRCLE|DT_MASKS_CLONE);
  dt_masks_change_form_gui(spot);
  darktable.develop->form_gui->creation = TRUE;
  darktable.develop->form_gui->creation_module = self;
  dt_control_queue_redraw_center();
}


void modify_roi_out(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, dt_iop_roi_t *roi_out, const dt_iop_roi_t *roi_in)
{
  *roi_out = *roi_in;
}

void modify_roi_in(struct dt_iop_module_t *self, struct dt_dev_pixelpipe_iop_t *piece, const dt_iop_roi_t *roi_out, dt_iop_roi_t *roi_in)
{
  *roi_in = *roi_out;

  int roir = roi_in->width+roi_in->x;
  int roib = roi_in->height+roi_in->y;
  int roix = roi_in->x;
  int roiy = roi_in->y;

  //dt_iop_spots_params_t *d = (dt_iop_spots_params_t *)piece->data;
  dt_develop_blend_params_t *bp = self->blend_params;

  //We calcul full image width and height at scale of ROI
  //const int imw = CLAMP(piece->pipe->iwidth*roi_in->scale, 1, piece->pipe->iwidth);
  //const int imh = CLAMP(piece->pipe->iheight*roi_in->scale, 1, piece->pipe->iheight);

  // We iterate throught all spots or polygons
  printf("spot roi\n");
  for(int i=0; i<bp->forms_count; i++)
  {
    printf("spot roi a %d\n",i);
    //we get the spot
    dt_masks_form_t *form = dt_masks_get_from_id(self->dev,bp->forms[i]);
    if (!form) continue;
    printf("spot roi b %d\n",i);
    //we get the area for the form
    int fl,ft,fw,fh;
    if (!dt_masks_get_area(self,piece,form,&fw,&fh,&fl,&ft)) continue;
    printf("spot roi c %d\n",i);
    //if the form is outside the roi, we just skip it
    fw *= roi_in->scale, fh *= roi_in->scale, fl *= roi_in->scale, ft *= roi_in->scale;
    if (ft>=roi_out->y+roi_out->height || ft+fh<=roi_out->y || fl>=roi_out->x+roi_out->width || fl+fw<=roi_out->x) continue;
    printf("spot roi d %d\n",i);
    //we get the area for the source
    if (!dt_masks_get_source_area(self,piece,form,&fw,&fh,&fl,&ft)) continue;
    fw *= roi_in->scale, fh *= roi_in->scale, fl *= roi_in->scale, ft *= roi_in->scale;
printf("spot roi e %d\n",i);
    //we elarge the roi if needed
    roiy = fminf(ft,roiy);
    roix = fminf(fl,roix);
    roir = fmaxf(fl+fw,roir);
    roib = fmaxf(ft+fh,roib);
    printf("spot roi f %d\n",i);
  }
printf("spot roi g\n");
  //now we set the values
  roi_in->x = CLAMP(roix, 0, piece->pipe->iwidth*roi_in->scale-1);
  roi_in->y = CLAMP(roiy, 0, piece->pipe->iheight*roi_in->scale-1);
  roi_in->width = CLAMP(roir-roi_in->x, 1, piece->pipe->iwidth*roi_in->scale-roi_in->x);
  roi_in->height = CLAMP(roib-roi_in->y, 1, piece->pipe->iheight*roi_in->scale-roi_in->y);
  printf("spot roi h\n");
}

void process (struct dt_iop_module_t *self, dt_dev_pixelpipe_iop_t *piece, void *i, void *o, const dt_iop_roi_t *roi_in, const dt_iop_roi_t *roi_out)
{
  dt_iop_spots_params_t *d = (dt_iop_spots_params_t *)piece->data;
  // const float scale = piece->iscale/roi_in->scale;
  //const float scale = 1.0f/roi_in->scale;
  dt_develop_blend_params_t *bp = self->blend_params;
  
  const int ch = piece->colors;
  const float *in = (float *)i;
  float *out = (float *)o;

  //We calcul full image width and height at scale of ROI
  //const int imw = CLAMP(piece->pipe->iwidth*roi_in->scale, 1, piece->pipe->iwidth);
  //const int imh = CLAMP(piece->pipe->iheight*roi_in->scale, 1, piece->pipe->iheight);

  printf("spot process piece %d %d  %f\n",piece->pipe->iwidth,piece->pipe->iheight,roi_in->scale);
  printf("spot process roi_o %d %d %d %d\n",roi_out->x,roi_out->y,roi_out->width,roi_out->height);
  printf("spot process roi_i %d %d %d %d\n",roi_in->x,roi_in->y,roi_in->width,roi_in->height);
  printf("spot process %d\n",bp->forms_count);
  // we don't modify most of the image:
#ifdef _OPENMP
    #pragma omp parallel for schedule(static) default(none) shared(out,in,roi_in,roi_out)
#endif
  for (int k=0; k<roi_out->height; k++)
  {
    float *outb = out + ch*k*roi_out->width;
    const float *inb =  in + ch*roi_in->width*(k+roi_out->y-roi_in->y) + ch*(roi_out->x-roi_in->x);
    memcpy(outb, inb, sizeof(float)*roi_out->width*ch);
  }

  // iterate throught all forms
  for(int i=0; i<bp->forms_count; i++)
  {
    printf("a %d\n",d->clone_algo[i]);
    //we get the spot
    dt_masks_form_t *form = dt_masks_get_from_id(self->dev,bp->forms[i]);
    if (!form) continue;
    printf("b\n");
    //we get the area for the form
    int fl,ft,fw,fh;
    if (!dt_masks_get_area(self,piece,form,&fw,&fh,&fl,&ft)) continue;
    printf("c\n");
    //if the form is outside the roi, we just skip it
    fw *= roi_in->scale, fh *= roi_in->scale, fl *= roi_in->scale, ft *= roi_in->scale;
    if (ft>=roi_out->y+roi_out->height || ft+fh<=roi_out->y || fl>=roi_out->x+roi_out->width || fl+fw<=roi_out->x) continue;
    printf("d\n");
    if (d->clone_algo[i] == 1 && (form->type & DT_MASKS_CIRCLE))
    {
      dt_masks_point_circle_t *circle = (dt_masks_point_circle_t *)g_list_nth_data(form->points,0);
      // convert from world space:
      const int posx  = (circle->center[0] * piece->buf_in.width)*roi_in->scale;
      const int posy  = (circle->center[1] * piece->buf_in.height)*roi_in->scale;
      const int posx_source = (form->source[0]*piece->buf_in.width)*roi_in->scale;
      const int posy_source = (form->source[1]*piece->buf_in.height)*roi_in->scale;      
      const int rad = circle->radius* MIN(piece->buf_in.width, piece->buf_in.height)*roi_in->scale;
      const int dx = posx-posx_source;
      const int dy = posy-posy_source;
      fw = fh = 2*rad;
  
      // convert from world space:
      float filter[2*rad + 1];
      // for(int k=-rad; k<=rad; k++) filter[rad + k] = expf(-k*k*2.f/(rad*rad));
      if(rad > 0)
      {
        for(int k=-rad; k<=rad; k++)
        {
          const float kk = 1.0f - fabsf(k/(float)rad);
          filter[rad + k] = kk*kk*(3.0f - 2.0f*kk);
        }
      }
      else
      {
        filter[0] = 1.0f;
      }
      for (int yy=posy ; yy<posy+fh; yy++)
      {
        //we test if we are inside roi_out
        if (yy<roi_out->y || yy>=roi_out->y+roi_out->height) continue;
        //we test if the source point is inside roi_in
        if (yy-dy<roi_in->y || yy-dy>=roi_in->y+roi_in->height) continue;
        for (int xx=posx ; xx<posx+fw; xx++)
        {
          //we test if we are inside roi_out
          if (xx<roi_out->x || xx>=roi_out->x+roi_out->width) continue;
          //we test if the source point is inside roi_in
          if (xx-dx<roi_in->x || xx-dx>=roi_in->x+roi_in->width) continue;
          
          const float f = filter[xx-posx+rad+1]*filter[yy-posy+rad+1];//*d->spot[i].opacity;          
          for(int c=0; c<ch; c++)
            out[4*(roi_out->width*(yy-roi_out->y) + xx-roi_out->x) + c] =
              out[4*(roi_out->width*(yy-roi_out->y) + xx-roi_out->x) + c] * (1.0f-f) +
              in[4*(roi_in->width*(yy-posy+posy_source-roi_in->y) + xx-posx+posx_source-roi_in->x) + c] * f;
        }
      }
    }
    else
    {
      printf("e\n");
      //we get the mask
      float *mask;
      int posx,posy,width,height;    
      dt_masks_get_mask(self,piece,form,&mask,&width,&height,&posx,&posy);
      //now we search the delta with the source
      int dx,dy;
      dx=dy=0;
      if (form->type & DT_MASKS_CURVE)
      {
        printf("f\n");
        dt_masks_point_curve_t *pt = (dt_masks_point_curve_t *)g_list_nth_data(form->points,0);
        dx = pt->corner[0]*roi_in->scale*piece->buf_in.width - form->source[0]*roi_in->scale*piece->buf_in.width;
        dy = pt->corner[1]*roi_in->scale*piece->buf_in.height - form->source[1]*roi_in->scale*piece->buf_in.height;
      }
      else if (form->type & DT_MASKS_CIRCLE)
      {
        dt_masks_point_circle_t *pt = (dt_masks_point_circle_t *)g_list_nth_data(form->points,0);
        printf("g %f %f %f %f\n",pt->center[0],form->source[0],pt->center[1],form->source[1]);
        dx = pt->center[0]*roi_in->scale*piece->buf_in.width - form->source[0]*roi_in->scale*piece->buf_in.width;
        dy = pt->center[1]*roi_in->scale*piece->buf_in.height - form->source[1]*roi_in->scale*piece->buf_in.height;
      }
      if (dx==0 && dy==0) continue;
printf("h form %d %d %d %d\n",posx,posy,width,height);
printf("h roio %d %d %d %d\n",roi_out->x,roi_out->y,roi_out->width,roi_out->height);
printf("h roii %d %d %d %d\n",roi_in->x,roi_in->y,roi_in->width,roi_in->height);
printf("h pice %f %d %d\n",roi_in->scale,piece->buf_in.width,piece->buf_in.height);
printf("h recu %d %d %d %d  %d %d\n",fl,ft,fw,fh,dx,dy);
      //now we do the pixel clone
      for (int yy=ft ; yy<ft+fh; yy++)
      {
        //we test if we are inside roi_out
        if (yy<roi_out->y || yy>=roi_out->y+roi_out->height) continue;
        //we test if the source point is inside roi_in
        if (yy-dy<roi_in->y || yy-dy>=roi_in->y+roi_in->height) continue;
        for (int xx=fl ; xx<fl+fw; xx++)
        {
          //we test if we are inside roi_out
          if (xx<roi_out->x || xx>=roi_out->x+roi_out->width) continue;
          //we test if the source point is inside roi_in
          if (xx-dx<roi_in->x || xx-dx>=roi_in->x+roi_in->width) continue;
          
          float f = mask[((int)((yy-ft)/roi_in->scale))*width + (int)((xx-fl)/roi_in->scale)];  //we can add the opacity here
          
          for(int c=0; c<ch; c++)
            out[4*(roi_out->width*(yy-roi_out->y) + xx-roi_out->x) + c] =
              out[4*(roi_out->width*(yy-roi_out->y) + xx-roi_out->x) + c] * (1.0f-f) +
              in[4*(roi_in->width*(yy-dy-roi_in->y) + xx-dx-roi_in->x) + c] * f;
        }
      }
      free(mask);
    }    
  }
}

/** init, cleanup, commit to pipeline */
void init(dt_iop_module_t *module)
{
  // we don't need global data:
  module->data = NULL; //malloc(sizeof(dt_iop_spots_global_data_t));
  module->params = malloc(sizeof(dt_iop_spots_params_t));
  module->default_params = malloc(sizeof(dt_iop_spots_params_t));
  // our module is disabled by default
  // by default:
  module->default_enabled = 0;
  module->priority = 200; // module order created by iop_dependencies.py, do not edit!
  module->params_size = sizeof(dt_iop_spots_params_t);
  module->gui_data = NULL;
  // init defaults:
  dt_iop_spots_params_t tmp = (dt_iop_spots_params_t)
  {
    {2}
  };

  memcpy(module->params, &tmp, sizeof(dt_iop_spots_params_t));
  memcpy(module->default_params, &tmp, sizeof(dt_iop_spots_params_t));
}

void cleanup(dt_iop_module_t *module)
{
  free(module->gui_data);
  module->gui_data = NULL; // just to be sure
  free(module->params);
  module->params = NULL;
  free(module->data); // just to be sure
  module->data = NULL;
}

void gui_focus (struct dt_iop_module_t *self, gboolean in)
{
  //dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  if(self->enabled)
  {
    if(in)
    {
      // got focus, show all shapes
      dt_masks_set_edit_mode(self,TRUE);
    }
    else
    {
      // lost focus, hide all shapes
      dt_masks_set_edit_mode(self,FALSE);
    }
  }
}

/** commit is the synch point between core and gui, so it copies params to pipe data. */
void commit_params (struct dt_iop_module_t *self, dt_iop_params_t *params, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  memcpy(piece->data, params, sizeof(dt_iop_spots_params_t));
}

void init_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  piece->data = malloc(sizeof(dt_iop_spots_data_t));
  self->commit_params(self, self->default_params, pipe, piece);
}

void cleanup_pipe (struct dt_iop_module_t *self, dt_dev_pixelpipe_t *pipe, dt_dev_pixelpipe_iop_t *piece)
{
  free(piece->data);
}

/** gui callbacks, these are needed. */
void gui_update (dt_iop_module_t *self)
{
  //dt_iop_spots_params_t *p = (dt_iop_spots_params_t *)self->params;
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  char str[3];
  snprintf(str,3,"%d",self->blend_params->forms_count);
  gtk_label_set_text(g->label, str);
}

void gui_init (dt_iop_module_t *self)
{
  const int bs = 14;
  
  self->gui_data = malloc(sizeof(dt_iop_spots_gui_data_t));
  dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;

  self->widget = gtk_vbox_new(FALSE, 5);
  //GtkWidget *label = gtk_label_new(_("click on a spot and drag on canvas to heal.\nuse the mouse wheel to adjust size.\nright click to remove a stroke."));
  //gtk_misc_set_alignment(GTK_MISC(label), 0.0f, 0.5f);
  //gtk_box_pack_start(GTK_BOX(self->widget), label, FALSE, TRUE, 0);
  GtkWidget * hbox = gtk_hbox_new(FALSE, 5);
  GtkWidget *label = gtk_label_new(_("number of strokes:"));
  gtk_box_pack_start(GTK_BOX(hbox), label, FALSE, TRUE, 0);
  g->label = GTK_LABEL(gtk_label_new("-1"));
  
  
  g->bt_curve = dtgtk_togglebutton_new(dtgtk_cairo_paint_masks_curve, CPF_STYLE_FLAT|CPF_DO_NOT_USE_BORDER);
  g_signal_connect(G_OBJECT(g->bt_curve), "button-press-event", G_CALLBACK(_add_curve), self);
  g_object_set(G_OBJECT(g->bt_curve), "tooltip-text", _("add curve shape"), (char *)NULL);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_curve), FALSE);
  gtk_widget_set_size_request(GTK_WIDGET(g->bt_curve),bs,bs);
  gtk_box_pack_end (GTK_BOX (hbox),g->bt_curve,FALSE,FALSE,0);
  
  g->bt_circle = dtgtk_togglebutton_new(dtgtk_cairo_paint_masks_circle, CPF_STYLE_FLAT|CPF_DO_NOT_USE_BORDER);
  g_signal_connect(G_OBJECT(g->bt_circle), "button-press-event", G_CALLBACK(_add_circle), self);
  g_object_set(G_OBJECT(g->bt_circle), "tooltip-text", _("add circular shape"), (char *)NULL);
  gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(g->bt_circle), FALSE);
  gtk_widget_set_size_request(GTK_WIDGET(g->bt_circle),bs,bs);
  gtk_box_pack_end (GTK_BOX (hbox),g->bt_circle,FALSE,FALSE,0);
  
  gtk_box_pack_start(GTK_BOX(hbox), GTK_WIDGET(g->label), FALSE, TRUE, 0);
  gtk_box_pack_start(GTK_BOX(self->widget), hbox, TRUE, TRUE, 0);
}

void gui_cleanup (dt_iop_module_t *self)
{
  //dt_iop_spots_gui_data_t *g = (dt_iop_spots_gui_data_t *)self->gui_data;
  // nothing else necessary, gtk will clean up the labels

  free(self->gui_data);
  self->gui_data = NULL;
}

// modelines: These editor modelines have been set for all relevant files by tools/update_modelines.sh
// vim: shiftwidth=2 expandtab tabstop=2 cindent
// kate: tab-indents: off; indent-width 2; replace-tabs on; indent-mode cstyle; remove-trailing-space on;
