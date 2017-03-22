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
#include <libweston-desktop.h>
#include "unstable/window-switcher/window-switcher-unstable-v1-server-protocol.h"

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

struct weston_window_switcher {
    struct weston_compositor *compositor;
    struct wl_client *client;
    struct wl_resource *binding;
    struct wl_list windows;
};

struct weston_window_switcher_window {
    struct wl_list link;
    struct weston_window_switcher *switcher;
    struct wl_resource *resource;
    struct weston_desktop_surface *surface;
    struct weston_view *view;
    struct wl_listener surface_destroy_listener;
    struct wl_listener view_destroy_listener;
};

static void
_weston_window_switcher_request_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void
_weston_window_switcher_window_surface_destroyed(struct wl_listener *listener, void *data)
{
    struct weston_window_switcher_window *self = wl_container_of(listener, self, surface_destroy_listener);

    self->surface = NULL;
}

static void
_weston_window_switcher_window_view_destroyed(struct wl_listener *listener, void *data)
{
    struct weston_window_switcher_window  *self = wl_container_of(listener, self, view_destroy_listener);

    weston_view_damage_below(self->view);
    self->view = NULL;
}

static void
_weston_window_switcher_window_destroy(struct wl_resource *resource)
{
    struct weston_window_switcher *self = wl_resource_get_user_data(resource);

    self->binding = NULL;
}

static void
_weston_window_switcher_window_request_switch_to(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial)
{
    struct weston_window_switcher_window *self = wl_resource_get_user_data(resource);
    struct weston_surface *surface = weston_desktop_surface_get_surface(self->surface);
    struct weston_seat *seat = wl_resource_get_user_data(seat_resource);
    struct weston_keyboard *keyboard = weston_seat_get_keyboard(seat);
    struct weston_pointer *pointer = weston_seat_get_pointer(seat);
    struct weston_touch *touch = weston_seat_get_touch(seat);

    if ( keyboard == NULL )
        return;

    if ( ( keyboard != NULL ) && ( keyboard->grab_serial == serial ) )
        weston_keyboard_set_focus(keyboard, surface);
    else if ( ( pointer != NULL ) && ( pointer->grab_serial == serial ) )
        weston_keyboard_set_focus(keyboard, surface);
    else if ( ( touch != NULL ) && ( touch->grab_serial == serial ) )
        weston_keyboard_set_focus(keyboard, surface);
}

static void
_weston_window_switcher_window_request_close(struct wl_client *client, struct wl_resource *resource, struct wl_resource *seat_resource, uint32_t serial)
{
    struct weston_window_switcher_window *self = wl_resource_get_user_data(resource);
    struct weston_seat *seat = wl_resource_get_user_data(seat_resource);
    struct weston_keyboard *keyboard = weston_seat_get_keyboard(seat);
    struct weston_pointer *pointer = weston_seat_get_pointer(seat);
    struct weston_touch *touch = weston_seat_get_touch(seat);

    if ( keyboard == NULL )
        return;

    if ( ( keyboard != NULL ) && ( keyboard->grab_serial == serial ) )
        weston_desktop_surface_close(self->surface);
    else if ( ( pointer != NULL ) && ( pointer->grab_serial == serial ) )
        weston_desktop_surface_close(self->surface);
    else if ( ( touch != NULL ) && ( touch->grab_serial == serial ) )
        weston_desktop_surface_close(self->surface);
}

static void
_weston_window_switcher_window_request_show(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface, int32_t x, int32_t y, int32_t width, int32_t height)
{

}

static const struct zww_window_switcher_window_v1_interface weston_window_switcher_window_implementation = {
    .destroy = _weston_window_switcher_request_destroy,
    .switch_to = _weston_window_switcher_window_request_switch_to,
    .close = _weston_window_switcher_window_request_close,
    .show = _weston_window_switcher_window_request_show,
};

static void
_weston_window_switcher_window_create(struct weston_window_switcher *switcher, struct weston_surface *surface)
{
    struct weston_window_switcher_window  *self;
    struct weston_desktop_surface *dsurface = weston_surface_get_desktop_surface(surface);

    if ( dsurface == NULL )
        return;

    wl_list_for_each(self, &switcher->windows, link)
    {
        if ( self->surface == dsurface )
            return;
    }

    self = zalloc(sizeof(struct weston_window_switcher_window));
    if ( self == NULL )
    {
        wl_client_post_no_memory(switcher->client);
        return;
    }

    self->switcher = switcher;
    self->surface = dsurface;

    self->resource = wl_resource_create(switcher->client, &zww_window_switcher_window_v1_interface, wl_resource_get_version(switcher->binding), 0);
    if ( self->resource == NULL )
    {
        wl_client_post_no_memory(switcher->client);
        return;
    }

    self->surface_destroy_listener.notify = _weston_window_switcher_window_surface_destroyed;
    wl_signal_add(&surface->destroy_signal, &self->surface_destroy_listener);
    wl_resource_set_implementation(self->resource, &weston_window_switcher_window_implementation, self, _weston_window_switcher_window_destroy);

    zww_window_switcher_v1_send_window(switcher->binding, self->resource);

    const char *title = weston_desktop_surface_get_title(self->surface);
    if ( title != NULL )
        zww_window_switcher_window_v1_send_title(self->resource, title);
    const char *app_id = weston_desktop_surface_get_app_id(self->surface);
    if ( app_id != NULL )
        zww_window_switcher_window_v1_send_app_id(self->resource, app_id);
    zww_window_switcher_window_v1_send_done(self->resource);
}

static const struct zww_window_switcher_v1_interface weston_window_switcher_implementation = {
    .destroy = _weston_window_switcher_request_destroy,
};

static void
_weston_window_switcher_unbind(struct wl_resource *resource)
{
    struct weston_window_switcher *self = wl_resource_get_user_data(resource);

    self->binding = NULL;
}

static void
_weston_window_switcher_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct weston_window_switcher *self = data;
    struct wl_resource *resource;

    resource = wl_resource_create(client, &zww_window_switcher_v1_interface, version, id);
    wl_resource_set_implementation(resource, &weston_window_switcher_implementation, self, _weston_window_switcher_unbind);

    if ( self->binding != NULL )
    {
        wl_resource_post_error(resource, ZWW_WINDOW_SWITCHER_V1_ERROR_BOUND, "interface object already bound");
        wl_resource_destroy(resource);
        return;
    }

    self->client = client;
    self->binding = resource;

    struct weston_view *view;
    wl_list_for_each(view, &self->compositor->view_list, link)
        _weston_window_switcher_window_create(self, view->surface);
}

WW_EXPORT int
wet_module_init(struct weston_compositor *compositor, int *argc, char *argv[])
{
    struct weston_window_switcher *self;

    self = zalloc(sizeof(struct weston_window_switcher));
    if ( self == NULL )
        return -1;

    self->compositor = compositor;

    wl_list_init(&self->windows);

    if ( wl_global_create(self->compositor->wl_display, &zww_window_switcher_v1_interface, 1, self, _weston_window_switcher_bind) == NULL)
        return -1;

    return 0;
}
