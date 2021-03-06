/**
 * @file display.c
 * @brief OpenGL video output module
 */
/*****************************************************************************
 * Copyright © 2010-2011 Rémi Denis-Courmont
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston MA 02110-1301, USA.
 *****************************************************************************/

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif

#include <stdlib.h>
#include <assert.h>

#include <vlc_common.h>
#include <vlc_plugin.h>
#include <vlc_vout_display.h>
#include <vlc_opengl.h>
#include "vout_helper.h"

/* Plugin callbacks */
static int Open (vlc_object_t *);
static void Close (vlc_object_t *);

#define GL_TEXT N_("myOpenGL extension")
#define GLES2_TEXT N_("myOpenGL ES 2 extension")
#define PROVIDER_LONGTEXT N_( \
    "Extension through which to use the Open Graphics Library for test (OpenGL).")
#define EQUIRECTANGLE_PROJECTION_TEXT N_("Enable equirectangular projection")
#define EQUIRECTANGLE_PROJECTION_LONGTEXT N_("Enable equirectangular projection")
#define ALPHABLEND_PROJECTION_TEXT N_("Enable alpha blending projection")
#define ALPHABLEND_PROJECTION_LONGTEXT N_("Enable alpha blending projection")
#define ALPHABLEND_PROJECTION_RATIO_FRONT_TEXT N_("Alpha blending's overlapping ratio(front)")
#define ALPHABLEND_PROJECTION_RATIO_REAR_TEXT N_("Alpha blending's overlapping ratio(rear)")
#define ALPHABLEND_PROJECTION_FITTODISPLAY_TEXT N_("Enable fitting to display after alpha blend")
#define ALPHABLEND_PROJECTION_ENABLE_TEXT N_("Enable alpha blend")
#define ALPHABLEND_PROJECTION_SHOW_DIVIDER_TEXT N_("Show camera divider")

vlc_module_begin ()
#if defined (USE_OPENGL_ES2)
# define API VLC_OPENGL_ES2
# define MODULE_VARNAME "gles2"
    set_shortname (N_("myOpenGL ES2(test)"))
    set_description (N_("my OpenGL for video output of embedded Systems for test"))
    add_bool("equirectangular-projection", true, EQUIRECTANGLE_PROJECTION_TEXT, EQUIRECTANGLE_PROJECTION_LONGTEXT, true)
    add_bool("alpha-blend-projection", false, ALPHABLEND_PROJECTION_TEXT, ALPHABLEND_PROJECTION_LONGTEXT, true)
    add_bool("alpha-blend-fit-to-display", true, ALPHABLEND_PROJECTION_FITTODISPLAY_TEXT, ALPHABLEND_PROJECTION_FITTODISPLAY_TEXT, true)
    add_bool("alpha-blend-show-divider", false, ALPHABLEND_PROJECTION_SHOW_DIVIDER_TEXT, ALPHABLEND_PROJECTION_SHOW_DIVIDER_TEXT, true)
    add_bool("alpha-blend-enable-blend", false, ALPHABLEND_PROJECTION_ENABLE_TEXT, ALPHABLEND_PROJECTION_ENABLE_TEXT, true)
    add_float_with_range( "alpha-blend-ratio-front", 0.09, 0.0, 0.5, ALPHABLEND_PROJECTION_RATIO_FRONT_TEXT, ALPHABLEND_PROJECTION_RATIO_FRONT_TEXT, false )
    change_safe();
    add_float_with_range( "alpha-blend-ratio-rear", 0.135, 0.0, 0.5, ALPHABLEND_PROJECTION_RATIO_REAR_TEXT, ALPHABLEND_PROJECTION_RATIO_REAR_TEXT, false )
    change_safe();
    set_capability ("vout display", 264)
    set_callbacks (Open, Close)
    add_shortcut ("myopengles2", "mygles2")
    add_module("mygles2", "myopengl es2", NULL, GLES2_TEXT, PROVIDER_LONGTEXT)

#else

# define API VLC_OPENGL
# define MODULE_VARNAME "gl"
    set_shortname (N_("myOpenGL(test)"))
    set_description (N_("myOpenGL video output for test"))
    set_category (CAT_VIDEO)
    set_subcategory (SUBCAT_VIDEO_VOUT)
    add_bool("equirectangular-projection", true, EQUIRECTANGLE_PROJECTION_TEXT, EQUIRECTANGLE_PROJECTION_LONGTEXT, true)
    add_bool("alpha-blend-projection", false, ALPHABLEND_PROJECTION_TEXT, ALPHABLEND_PROJECTION_LONGTEXT, true)
    add_bool("alpha-blend-fit-to-display", true, ALPHABLEND_PROJECTION_FITTODISPLAY_TEXT, ALPHABLEND_PROJECTION_FITTODISPLAY_TEXT, true)
    add_bool("alpha-blend-show-divider", false, ALPHABLEND_PROJECTION_SHOW_DIVIDER_TEXT, ALPHABLEND_PROJECTION_SHOW_DIVIDER_TEXT, true)
    add_bool("alpha-blend-enable-blend", false, ALPHABLEND_PROJECTION_ENABLE_TEXT, ALPHABLEND_PROJECTION_ENABLE_TEXT, true)
    add_float_with_range( "alpha-blend-ratio-front", 0.09, 0.0, 0.5, ALPHABLEND_PROJECTION_RATIO_FRONT_TEXT, ALPHABLEND_PROJECTION_RATIO_FRONT_TEXT, false )
    change_safe();
    add_float_with_range( "alpha-blend-ratio-rear", 0.135, 0.0, 0.5, ALPHABLEND_PROJECTION_RATIO_REAR_TEXT, ALPHABLEND_PROJECTION_RATIO_REAR_TEXT, false )
    change_safe();
    set_capability ("vout display", 269)
    set_callbacks (Open, Close)
    add_shortcut ("myopengl", "mygl")
    add_module("mygl", "myopengl", NULL, GL_TEXT, PROVIDER_LONGTEXT)
#endif
    add_glopts ()
vlc_module_end ()

struct vout_display_sys_t
{
    vout_display_opengl_t *vgl;
    vlc_gl_t *gl;
    picture_pool_t *pool;
};

/* Display callbacks */
static picture_pool_t *Pool (vout_display_t *, unsigned);
static void PictureRender (vout_display_t *, picture_t *, subpicture_t *, vlc_tick_t);
static void PictureDisplay (vout_display_t *, picture_t *, subpicture_t *);
static int Control (vout_display_t *, int, va_list);

/**
 * Allocates a surface and an OpenGL context for video output.
 */
static int Open (vlc_object_t *obj)
{
    vout_display_t *vd = (vout_display_t *)obj;
    vout_display_sys_t *sys = malloc (sizeof (*sys));
    if (unlikely(sys == NULL))
        return VLC_ENOMEM;

    sys->gl = NULL;
    sys->pool = NULL;

    vout_window_t *surface = vd->cfg->window;
    char *gl_name = var_InheritString(surface, MODULE_VARNAME);

    /* VDPAU GL interop works only with GLX. Override the "gl" option to force
     * it. */
#ifndef USE_OPENGL_ES2
    if (surface->type == VOUT_WINDOW_TYPE_XID)
    {
        switch (vd->fmt.i_chroma)
        {
            case VLC_CODEC_VDPAU_VIDEO_444:
            case VLC_CODEC_VDPAU_VIDEO_422:
            case VLC_CODEC_VDPAU_VIDEO_420:
            {
                /* Force the option only if it was not previously set */
                if (gl_name == NULL || gl_name[0] == 0
                 || strcmp(gl_name, "any") == 0)
                {
                    free(gl_name);
                    gl_name = strdup("glx");
                }
                break;
            }
            default:
                break;
        }
    }
#endif

    sys->gl = vlc_gl_Create (surface, API, gl_name);
    free(gl_name);
    if (sys->gl == NULL)
        goto error;

    vlc_gl_Resize (sys->gl, vd->cfg->display.width, vd->cfg->display.height);

    /* Initialize video display */
    const vlc_fourcc_t *spu_chromas;

    if (vlc_gl_MakeCurrent (sys->gl))
        goto error;

    sys->vgl = vout_display_opengl_New (&vd->fmt, &spu_chromas, sys->gl,
                                        &vd->cfg->viewpoint);
    vlc_gl_ReleaseCurrent (sys->gl);

    if (sys->vgl == NULL)
        goto error;

    vd->sys = sys;
    vd->info.has_pictures_invalid = false;
    vd->info.subpicture_chromas = spu_chromas;
    vd->pool = Pool;
    vd->prepare = PictureRender;
    vd->display = PictureDisplay;
    vd->control = Control;
    return VLC_SUCCESS;

error:
    if (sys->gl != NULL)
        vlc_gl_Release (sys->gl);
    free (sys);
    return VLC_EGENERIC;
}

/**
 * Destroys the OpenGL context.
 */
static void Close (vlc_object_t *obj)
{
    vout_display_t *vd = (vout_display_t *)obj;
    vout_display_sys_t *sys = vd->sys;
    vlc_gl_t *gl = sys->gl;

    vlc_gl_MakeCurrent (gl);
    vout_display_opengl_Delete (sys->vgl);
    vlc_gl_ReleaseCurrent (gl);

    vlc_gl_Release (gl);
    free (sys);
}

/**
 * Returns picture buffers
 */
static picture_pool_t *Pool (vout_display_t *vd, unsigned count)
{
    vout_display_sys_t *sys = vd->sys;

    if (!sys->pool && vlc_gl_MakeCurrent (sys->gl) == VLC_SUCCESS)
    {
        sys->pool = vout_display_opengl_GetPool (sys->vgl, count);
        vlc_gl_ReleaseCurrent (sys->gl);
    }
    return sys->pool;
}

static void PictureRender (vout_display_t *vd, picture_t *pic, subpicture_t *subpicture,
                           vlc_tick_t date)
{
    VLC_UNUSED(date);
    vout_display_sys_t *sys = vd->sys;

    if (vlc_gl_MakeCurrent (sys->gl) == VLC_SUCCESS)
    {
        vout_display_opengl_Prepare (sys->vgl, pic, subpicture);
        vlc_gl_ReleaseCurrent (sys->gl);
    }
}

static void PictureDisplay (vout_display_t *vd, picture_t *pic, subpicture_t *subpicture)
{
    vout_display_sys_t *sys = vd->sys;

    if (vlc_gl_MakeCurrent (sys->gl) == VLC_SUCCESS)
    {
        vout_display_opengl_Display (sys->vgl, &vd->source);
        vlc_gl_ReleaseCurrent (sys->gl);
    }

    picture_Release (pic);
    if (subpicture != NULL)
        subpicture_Delete(subpicture);
}

static int Control (vout_display_t *vd, int query, va_list ap)
{
    vout_display_sys_t *sys = vd->sys;

    switch (query)
    {
#ifndef NDEBUG
      case VOUT_DISPLAY_RESET_PICTURES: // not needed
        vlc_assert_unreachable();
#endif

      case VOUT_DISPLAY_CHANGE_DISPLAY_SIZE:
      case VOUT_DISPLAY_CHANGE_DISPLAY_FILLED:
      case VOUT_DISPLAY_CHANGE_ZOOM:
      {
        vout_display_cfg_t c = *va_arg (ap, const vout_display_cfg_t *);
        const video_format_t *src = &vd->source;
        vout_display_place_t place;

        /* Reverse vertical alignment as the GL tex are Y inverted */
        if (c.align.vertical == VOUT_DISPLAY_ALIGN_TOP)
            c.align.vertical = VOUT_DISPLAY_ALIGN_BOTTOM;
        else if (c.align.vertical == VOUT_DISPLAY_ALIGN_BOTTOM)
            c.align.vertical = VOUT_DISPLAY_ALIGN_TOP;

        vout_display_PlacePicture (&place, src, &c, false);
        vlc_gl_Resize (sys->gl, c.display.width, c.display.height);
        if (vlc_gl_MakeCurrent (sys->gl) != VLC_SUCCESS)
            return VLC_EGENERIC;
        vout_display_opengl_SetWindowAspectRatio(sys->vgl, (float)place.width / place.height);
        vout_display_opengl_Viewport(sys->vgl, place.x, place.y, place.width, place.height);
        vlc_gl_ReleaseCurrent (sys->gl);
        return VLC_SUCCESS;
      }

      case VOUT_DISPLAY_CHANGE_SOURCE_ASPECT:
      case VOUT_DISPLAY_CHANGE_SOURCE_CROP:
      {
        const vout_display_cfg_t *cfg = vd->cfg;
        vout_display_place_t place;

        vout_display_PlacePicture (&place, &vd->source, cfg, false);
        if (vlc_gl_MakeCurrent (sys->gl) != VLC_SUCCESS)
            return VLC_EGENERIC;
        vout_display_opengl_SetWindowAspectRatio(sys->vgl, (float)place.width / place.height);
        vout_display_opengl_Viewport(sys->vgl, place.x, place.y, place.width, place.height);
        vlc_gl_ReleaseCurrent (sys->gl);
        return VLC_SUCCESS;
      }
      case VOUT_DISPLAY_CHANGE_VIEWPOINT:
        return vout_display_opengl_SetViewpoint (sys->vgl,
            &va_arg (ap, const vout_display_cfg_t* )->viewpoint);
      default:
        msg_Err (vd, "Unknown request %d", query);
    }
    return VLC_EGENERIC;
}
