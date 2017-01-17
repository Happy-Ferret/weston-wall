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
#include "unstable/dock-manager/dock-manager-unstable-v2-server-protocol.h"

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

struct weston_dock_manager {
    struct weston_compositor *compositor;
    struct wl_list resource_list;
    struct weston_layer layer;
    struct wl_list outputs;
    struct wl_listener output_destroyed_listener;
};

struct weston_dock_manager_output {
    struct wl_list link;
    struct weston_output *output;
    struct wl_list docks;
};

struct weston_dock {
    struct wl_list link;
    struct weston_dock_manager *dock_manager;
    struct weston_dock_manager_output *output;
    enum zww_dock_manager_v2_position position;
    struct wl_resource *resource;
    struct weston_surface *surface;
    struct weston_view *view;
    struct wl_listener view_destroy_listener;
};

static void
_weston_dock_manager_request_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static struct weston_dock_manager_output *
_weston_dock_manager_find_output(struct weston_dock_manager *dock_manager, struct weston_output *woutput)
{
    struct weston_dock_manager_output *self;

    wl_list_for_each(self, &dock_manager->outputs, link)
    {
        if ( self->output == woutput )
            return self;
    }

    self = zalloc(sizeof(struct weston_dock_manager_output));
    if ( self == NULL )
        return NULL;
    self->output = woutput;
    wl_list_insert(&dock_manager->outputs, &self->link);
    wl_list_init(&self->docks);

    return self;
}

static pixman_rectangle32_t
_weston_dock_manager_output_get_workarea(struct weston_dock_manager_output *output)
{
    struct weston_dock *dock;
    pixman_rectangle32_t area = {
        .x = output->output->x,
        .y = output->output->y,
        .width = output->output->width,
        .height = output->output->height,
    };

    wl_list_for_each(dock, &output->docks, link)
    {
        switch ( dock->position )
        {
        case ZWW_DOCK_MANAGER_V2_POSITION_TOP:
            area.y += dock->surface->height;
        case ZWW_DOCK_MANAGER_V2_POSITION_BOTTOM:
            area.height -= dock->surface->height;
        break;
        case ZWW_DOCK_MANAGER_V2_POSITION_LEFT:
            area.x += dock->surface->width;
        case ZWW_DOCK_MANAGER_V2_POSITION_RIGHT:
            area.width -= dock->surface->width;
        break;
        case ZWW_DOCK_MANAGER_V2_POSITION_DEFAULT:
            assert(0 && "not reached");
        }
    }

    return area;
}

static void
_weston_dock_manager_output_free(struct weston_dock_manager_output *self)
{
    wl_list_remove(&self->link);

    free(self);
}

static void
_weston_dock_surface_committed(struct weston_surface *surface, int32_t sx, int32_t sy)
{
    struct weston_dock *self = surface->committed_private, *dock;
    int32_t x, y;

    if ( weston_view_is_mapped(self->view) )
        return;

    x = self->output->output->x;
    y = self->output->output->y;

    switch ( self->position )
    {
    case ZWW_DOCK_MANAGER_V2_POSITION_BOTTOM:
        y += self->output->output->height - self->surface->height;
    case ZWW_DOCK_MANAGER_V2_POSITION_TOP:
    break;
    case ZWW_DOCK_MANAGER_V2_POSITION_RIGHT:
        x += self->output->output->width - self->surface->width;
    case ZWW_DOCK_MANAGER_V2_POSITION_LEFT:
    break;
    case ZWW_DOCK_MANAGER_V2_POSITION_DEFAULT:
        assert(0 && "not reached");
    }

    wl_list_for_each(dock, &self->output->docks, link)
    {
        if ( dock == self )
            continue;
        if ( dock->position != self->position )
            continue;

        switch ( dock->position )
        {
        case ZWW_DOCK_MANAGER_V2_POSITION_TOP:
            y += dock->surface->height;
        break;
        case ZWW_DOCK_MANAGER_V2_POSITION_BOTTOM:
            y -= dock->surface->height;
        break;
        case ZWW_DOCK_MANAGER_V2_POSITION_LEFT:
            x += dock->surface->width;
        break;
        case ZWW_DOCK_MANAGER_V2_POSITION_RIGHT:
            x -= dock->surface->width;
        break;
        case ZWW_DOCK_MANAGER_V2_POSITION_DEFAULT:
            assert(0 && "not reached");
        }
    }

    weston_layer_entry_insert(&self->dock_manager->layer.view_list, &self->view->layer_link);
    self->view->is_mapped = true;
    weston_view_set_position(self->view, x, y);
    weston_view_update_transform(self->view);
    weston_surface_damage(self->surface);
    weston_compositor_schedule_repaint(self->surface->compositor);
}

static void
_weston_dock_view_destroyed(struct wl_listener *listener, void *data)
{
    struct weston_dock *self = wl_container_of(listener, self, view_destroy_listener);

    weston_view_damage_below(self->view);
    self->view = NULL;
}

static void
_ww_dock_destroy(struct wl_resource *resource)
{
    struct weston_dock *self = wl_resource_get_user_data(resource);

    if ( self->view != NULL )
        weston_view_destroy(self->view);

    wl_list_remove(&self->link);

    free(self);
}

static const struct zww_dock_v2_interface _ww_dock_interface = {
    .destroy = _weston_dock_manager_request_destroy,
};

static void
_weston_dock_manager_create_dock(struct wl_client *client, struct wl_resource *resource, uint32_t id, struct wl_resource *surface_resource, struct wl_resource *output_resource, enum zww_dock_manager_v2_position position)
{
    struct weston_dock_manager *dock_manager = wl_resource_get_user_data(resource);
    struct weston_surface *surface = wl_resource_get_user_data(surface_resource);
    struct weston_output *woutput = NULL;
    struct weston_dock_manager_output *output;
    struct weston_dock *self;
    pixman_rectangle32_t area;

    if ( output_resource != NULL )
        woutput = wl_resource_get_user_data(output_resource);
    else
        woutput = wl_container_of(dock_manager->compositor->output_list.prev, woutput, link);

    if ( weston_surface_set_role(surface, "ww_dock", resource, ZWW_DOCK_MANAGER_V2_ERROR_ROLE) < 0 )
        return;

    output = _weston_dock_manager_find_output(dock_manager, woutput);
    if ( output == NULL )
    {
        wl_client_post_no_memory(client);
        return;
    }

    self = zalloc(sizeof(struct weston_dock));
    if ( self == NULL )
    {
        wl_client_post_no_memory(client);
        return;
    }

    if ( position == ZWW_DOCK_MANAGER_V2_POSITION_DEFAULT )
        position = ZWW_DOCK_MANAGER_V2_POSITION_BOTTOM;

    self->dock_manager = dock_manager;
    self->output = output;
    self->position = position;
    self->surface = surface;
    self->view = weston_view_create(self->surface);
    if ( self->view == NULL )
    {
        wl_client_post_no_memory(client);
        free(self);
        return;
    }

    self->resource = wl_resource_create(client, &zww_dock_v2_interface, wl_resource_get_version(resource), id);
    if ( self->resource == NULL )
    {
        wl_client_post_no_memory(client);
        weston_view_destroy(self->view);
        free(self);
        return;
    }

    wl_resource_set_implementation(self->resource, &_ww_dock_interface, self, _ww_dock_destroy);

    self->surface->committed = _weston_dock_surface_committed;
    self->surface->committed_private = self;

    self->view_destroy_listener.notify = _weston_dock_view_destroyed;
    wl_signal_add(&self->view->destroy_signal, &self->view_destroy_listener);

    area = _weston_dock_manager_output_get_workarea(output);
    zww_dock_v2_send_configure(self->resource, 1, 1, area.width, area.height, position);

    wl_list_insert(&self->output->docks, &self->link);
}

static const struct zww_dock_manager_v2_interface weston_dock_manager_implementation = {
    .destroy = _weston_dock_manager_request_destroy,
    .create_dock = _weston_dock_manager_create_dock,
};

static void
_weston_dock_manager_get_output_work_area(void *data, struct weston_output *woutput, pixman_rectangle32_t *area)
{
    struct weston_dock_manager *self = data;
    struct weston_dock_manager_output *output = _weston_dock_manager_find_output(self, woutput);

    *area = _weston_dock_manager_output_get_workarea(output);
}

static void
_weston_dock_manager_unbind(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

static void
_weston_dock_manager_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct weston_dock_manager *self = data;
    struct wl_resource *resource;

    resource = wl_resource_create(client, &zww_dock_manager_v2_interface, version, id);
    wl_resource_set_implementation(resource, &weston_dock_manager_implementation, self, _weston_dock_manager_unbind);

    wl_list_insert(&self->resource_list, wl_resource_get_link(resource));
}

static void
_weston_dock_manager_output_destroyed(struct wl_listener *listener, void *data)
{
    struct weston_dock_manager *dock_manager = wl_container_of(listener, dock_manager, output_destroyed_listener);
    struct weston_output *woutput = data;
    struct weston_dock_manager_output *self = _weston_dock_manager_find_output(dock_manager, woutput);

    if ( self == NULL )
        return;

    _weston_dock_manager_output_free(self);
}

WW_EXPORT int
wet_module_init(struct weston_compositor *compositor, int *argc, char *argv[])
{
    struct weston_dock_manager *self;

    self = zalloc(sizeof(struct weston_dock_manager));
    if ( self == NULL )
        return -1;

    self->compositor = compositor;

    wl_list_init(&self->resource_list);
    wl_list_init(&self->outputs);

    if ( wl_global_create(self->compositor->wl_display, &zww_dock_manager_v2_interface, 1, self, _weston_dock_manager_bind) == NULL)
        return -1;

    self->output_destroyed_listener.notify = _weston_dock_manager_output_destroyed;
    wl_signal_add(&self->compositor->output_destroyed_signal, &self->output_destroyed_listener);

    weston_layer_init(&self->layer, self->compositor);
    weston_layer_set_position(&self->layer, WESTON_LAYER_POSITION_UI);

    /* TODO: Add dock area API support */

    return 0;
}
