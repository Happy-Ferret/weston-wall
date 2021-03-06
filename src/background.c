/*
 * Copyright © 2013-2016 Quentin “Sardem FF7” Glidic
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <stdlib.h>
#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <linux/input.h>
#include <assert.h>
#include <signal.h>
#include <math.h>
#include <sys/types.h>

#include <wayland-server.h>
#include <compositor.h>
#include "unstable/background/background-unstable-v2-server-protocol.h"

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

struct weston_background {
    struct weston_compositor *compositor;
    struct wl_resource *binding;
    struct weston_layer layer;
    struct wl_list outputs;
    struct wl_listener output_destroyed_listener;
};

struct weston_background_output {
    struct wl_list link;
    struct weston_output *output;
    struct weston_surface *surface;
    struct weston_view *view;
    struct weston_transform transform;
    struct wl_listener surface_destroy_listener;
    struct wl_listener view_destroy_listener;
};

static struct weston_background_output *
_weston_background_find_output(struct weston_background *back, struct weston_output *woutput)
{
    struct weston_background_output *output;

    wl_list_for_each(output, &back->outputs, link)
    {
        if ( output->output == woutput )
            return output;
    }
    return NULL;
}

static void
_weston_background_output_update_transform(struct weston_background_output *self)
{
    weston_view_set_position(self->view, self->output->x, self->output->y);
    weston_view_set_mask(self->view, 0, 0, self->output->width, self->output->height);

    weston_surface_damage(self->surface);
    weston_compositor_schedule_repaint(self->surface->compositor);
}

static void
_weston_background_output_free(struct weston_background_output *self)
{
    if ( self->view != NULL )
        weston_view_destroy(self->view);

    wl_list_remove(&self->surface_destroy_listener.link);

    wl_list_remove(&self->link);

    free(self);
}

static void
_weston_background_output_surface_destroyed(struct wl_listener *listener, void *data)
{
    struct weston_background_output *self = wl_container_of(listener, self, surface_destroy_listener);

    _weston_background_output_free(self);
}

static void
_weston_background_output_view_destroyed(struct wl_listener *listener, void *data)
{
    struct weston_background_output *self = wl_container_of(listener, self, view_destroy_listener);

    weston_view_damage_below(self->view);
    self->view = NULL;
}

static void
_weston_background_request_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void
_weston_background_set_background(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource, struct wl_resource *output_resource)
{
    struct weston_background *back = wl_resource_get_user_data(resource);
    struct weston_surface *surface = wl_resource_get_user_data(surface_resource);
    struct weston_output *woutput = wl_resource_get_user_data(output_resource);

    if ( weston_surface_set_role(surface, "ww_background", resource, ZWW_BACKGROUND_V2_ERROR_ROLE) < 0 )
        return;

    struct weston_background_output *self = _weston_background_find_output(back, woutput);

    if ( self == NULL )
    {
        self = zalloc(sizeof(struct weston_background_output));
        if ( self == NULL )
        {
            wl_resource_post_no_memory(surface_resource);
            return;
        }
        self->output = woutput;
        wl_list_insert(&back->outputs, &self->link);
        self->surface_destroy_listener.notify = _weston_background_output_surface_destroyed;
        self->view_destroy_listener.notify = _weston_background_output_view_destroyed;
    }
    else
    {
        weston_view_destroy(self->view);
        wl_list_remove(&self->surface_destroy_listener.link);
    }

    self->surface = surface;
    self->view = weston_view_create(self->surface);

    wl_signal_add(&self->surface->destroy_signal, &self->surface_destroy_listener);
    wl_signal_add(&self->view->destroy_signal, &self->view_destroy_listener);

    weston_layer_entry_insert(&back->layer.view_list, &self->view->layer_link);

    _weston_background_output_update_transform(self);
}

static const struct zww_background_v2_interface weston_background_implementation = {
    .destroy = _weston_background_request_destroy,
    .set_background = _weston_background_set_background,
};

static void
_weston_background_unbind(struct wl_resource *resource)
{
    struct weston_background *back = wl_resource_get_user_data(resource);

    back->binding = NULL;
}

static void
_weston_background_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct weston_background *back = data;
    struct wl_resource *resource;

    resource = wl_resource_create(client, &zww_background_v2_interface, version, id);
    wl_resource_set_implementation(resource, &weston_background_implementation, back, _weston_background_unbind);

    if ( back->binding != NULL )
    {
        wl_resource_post_error(resource, ZWW_BACKGROUND_V2_ERROR_BOUND, "interface object already bound");
        wl_resource_destroy(resource);
        return;
    }

    back->binding = resource;
}

static void
_weston_background_output_destroyed(struct wl_listener *listener, void *data)
{
    struct weston_background *back = wl_container_of(listener, back, output_destroyed_listener);
    struct weston_output *woutput = data;
    struct weston_background_output *self = _weston_background_find_output(back, woutput);

    if ( self == NULL )
        return;

    _weston_background_output_free(self);
}

WW_EXPORT int
wet_module_init(struct weston_compositor *compositor, int *argc, char *argv[])
{
    struct weston_background *back;

    back = zalloc(sizeof(struct weston_background));
    if ( back == NULL )
        return -1;

    back->compositor = compositor;

    wl_list_init(&back->outputs);

    if ( wl_global_create(back->compositor->wl_display, &zww_background_v2_interface, 1, back, _weston_background_bind) == NULL)
        return -1;

    back->output_destroyed_listener.notify = _weston_background_output_destroyed;
    wl_signal_add(&back->compositor->output_destroyed_signal, &back->output_destroyed_listener);

    weston_layer_init(&back->layer, back->compositor);
    weston_layer_set_position(&back->layer, WESTON_LAYER_POSITION_BACKGROUND);

    return 0;
}
