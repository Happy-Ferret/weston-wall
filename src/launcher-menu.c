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
#include "unstable/launcher-menu/launcher-menu-unstable-v1-server-protocol.h"

#define MIN(a,b) (((a) < (b)) ? (a) : (b))
#define MAX(a,b) (((a) > (b)) ? (a) : (b))

enum weston_launcher_menu_positon {
    WESTON_LAUNCHER_MENU_POSITION_DEFAULT,
    WESTON_LAUNCHER_MENU_POSITION_POINTER,
    WESTON_LAUNCHER_MENU_POSITION_SURFACE,
};

struct weston_launcher_menu {
    struct weston_compositor *compositor;
    struct wl_resource *binding;
    struct weston_layer layer;
    struct wl_list seats;
    struct wl_listener output_destroyed_listener;
    struct weston_output *output;
    struct weston_surface *surface;
    struct weston_view *view;
    struct wl_listener surface_destroy_listener;
    struct wl_listener view_destroy_listener;
    struct weston_launcher_menu_seat *grab_seat;
    enum weston_launcher_menu_positon position;
    struct {
        struct weston_view *view;
        struct weston_geometry geometry;
    } target;
};

struct weston_launcher_menu_seat {
    struct wl_list link;
    struct weston_launcher_menu *launcher_menu;
    struct weston_seat *seat;
    struct wl_listener seat_destroy_listener;
    struct {
        struct weston_keyboard_grab keyboard;
        struct weston_pointer_grab pointer;
        struct weston_touch_grab touch;
        bool initial_up;
    } grab;
};

static struct weston_output *
_weston_launcher_menu_get_default_output(struct weston_launcher_menu *self)
{
    struct weston_output *output = NULL;

    if ( ! wl_list_empty(&self->compositor->output_list) )
        output = wl_container_of(self->compositor->output_list.next, output, link);

    return output;
}

static struct weston_seat *
_weston_launcher_menu_get_default_seat(struct weston_launcher_menu *self)
{
    struct weston_seat *seat = NULL;

    if ( ! wl_list_empty(&self->compositor->seat_list) )
        seat = wl_container_of(self->compositor->seat_list.next, seat, link);

    return seat;
}

static void _weston_launcher_menu_seat_grab_end(struct weston_launcher_menu_seat *self, bool dismiss);

static void
_weston_launcher_menu_seat_grab_keyboard_key(struct weston_keyboard_grab *grab,
                        uint32_t time, uint32_t key,
                        enum wl_keyboard_key_state state)
{
    weston_keyboard_send_key(grab->keyboard, time, key, state);
}

static void
_weston_launcher_menu_seat_grab_keyboard_modifiers(struct weston_keyboard_grab *grab, uint32_t serial, uint32_t mods_depressed, uint32_t mods_latched, uint32_t mods_locked, uint32_t group)
{
    weston_keyboard_send_modifiers(grab->keyboard, serial, mods_depressed, mods_latched, mods_locked, group);
}

static void
_weston_launcher_menu_seat_grab_keyboard_cancel(struct weston_keyboard_grab *grab)
{
    struct weston_launcher_menu_seat *self = wl_container_of(grab, self, grab.keyboard);

    _weston_launcher_menu_seat_grab_end(self, true);
}

static const struct weston_keyboard_grab_interface _weston_desktop_seat_grab_keyboard_interface = {
   .key = _weston_launcher_menu_seat_grab_keyboard_key,
   .modifiers = _weston_launcher_menu_seat_grab_keyboard_modifiers,
   .cancel = _weston_launcher_menu_seat_grab_keyboard_cancel,
};

static void
_weston_launcher_menu_seat_grab_pointer_focus(struct weston_pointer_grab *grab)
{
    struct weston_launcher_menu_seat *self = wl_container_of(grab, self, grab.pointer);
    struct weston_pointer *pointer = grab->pointer;
    struct weston_view *view;
    wl_fixed_t sx, sy;

    view = weston_compositor_pick_view(pointer->seat->compositor, pointer->x, pointer->y, &sx, &sy);

    if ( ( view != NULL ) && ( view->surface->resource != NULL ) && ( view == self->launcher_menu->view ) )
        weston_pointer_set_focus(pointer, view, sx, sy);
    else
        weston_pointer_clear_focus(pointer);
}

static void
_weston_launcher_menu_seat_grab_pointer_motion(struct weston_pointer_grab *grab, uint32_t time, struct weston_pointer_motion_event *event)
{
    weston_pointer_send_motion(grab->pointer, time, event);
}

static void
_weston_launcher_menu_seat_grab_pointer_button(struct weston_pointer_grab *grab, uint32_t time, uint32_t button, enum wl_pointer_button_state state)
{
    struct weston_launcher_menu_seat *self = wl_container_of(grab, self, grab.pointer);
    struct weston_pointer *pointer = grab->pointer;
    bool initial_up = self->grab.initial_up;

    if ( state == WL_POINTER_BUTTON_STATE_RELEASED )
        self->grab.initial_up = true;

    if ( weston_pointer_has_focus_resource(pointer) )
        weston_pointer_send_button(pointer, time, button, state);
    else if ( ( state == WL_POINTER_BUTTON_STATE_RELEASED ) && ( initial_up || ( ( time - grab->pointer->grab_time ) > 500 ) ) )
        _weston_launcher_menu_seat_grab_end(self, true);
}

static void
_weston_launcher_menu_seat_grab_pointer_axis(struct weston_pointer_grab *grab, uint32_t time, struct weston_pointer_axis_event *event)
{
    weston_pointer_send_axis(grab->pointer, time, event);
}

static void
_weston_launcher_menu_seat_grab_pointer_axis_source(struct weston_pointer_grab *grab, uint32_t source)
{
    weston_pointer_send_axis_source(grab->pointer, source);
}

static void
_weston_launcher_menu_seat_grab_pointer_frame(struct weston_pointer_grab *grab)
{
    weston_pointer_send_frame(grab->pointer);
}

static void
_weston_launcher_menu_seat_grab_pointer_cancel(struct weston_pointer_grab *grab)
{
    struct weston_launcher_menu_seat *self = wl_container_of(grab, self, grab.pointer);

    _weston_launcher_menu_seat_grab_end(self, true);
}

static const struct weston_pointer_grab_interface _weston_desktop_seat_grab_pointer_interface = {
   .focus = _weston_launcher_menu_seat_grab_pointer_focus,
   .motion = _weston_launcher_menu_seat_grab_pointer_motion,
   .button = _weston_launcher_menu_seat_grab_pointer_button,
   .axis = _weston_launcher_menu_seat_grab_pointer_axis,
   .axis_source = _weston_launcher_menu_seat_grab_pointer_axis_source,
   .frame = _weston_launcher_menu_seat_grab_pointer_frame,
   .cancel = _weston_launcher_menu_seat_grab_pointer_cancel,
};

static void
_weston_launcher_menu_seat_grab_touch_down(struct weston_touch_grab *grab, uint32_t time, int touch_id, wl_fixed_t sx, wl_fixed_t sy)
{
    weston_touch_send_down(grab->touch, time, touch_id, sx, sy);
}

static void
_weston_launcher_menu_seat_grab_touch_up(struct weston_touch_grab *grab, uint32_t time, int touch_id)
{
    weston_touch_send_up(grab->touch, time, touch_id);
}

static void
_weston_launcher_menu_seat_grab_touch_motion(struct weston_touch_grab *grab, uint32_t time, int touch_id, wl_fixed_t sx, wl_fixed_t sy)
{
    weston_touch_send_motion(grab->touch, time, touch_id, sx, sy);
}

static void
_weston_launcher_menu_seat_grab_touch_frame(struct weston_touch_grab *grab)
{
    weston_touch_send_frame(grab->touch);
}

static void
_weston_launcher_menu_seat_grab_touch_cancel(struct weston_touch_grab *grab)
{
    struct weston_launcher_menu_seat *self = wl_container_of(grab, self, grab.touch);

    _weston_launcher_menu_seat_grab_end(self, true);
}

static const struct weston_touch_grab_interface _weston_desktop_seat_grab_touch_interface = {
   .down = _weston_launcher_menu_seat_grab_touch_down,
   .up = _weston_launcher_menu_seat_grab_touch_up,
   .motion = _weston_launcher_menu_seat_grab_touch_motion,
   .frame = _weston_launcher_menu_seat_grab_touch_frame,
   .cancel = _weston_launcher_menu_seat_grab_touch_cancel,
};

static void
_weston_launcher_menu_seat_grab_start(struct weston_launcher_menu_seat *self)
{
    struct weston_keyboard *keyboard = weston_seat_get_keyboard(self->seat);
    struct weston_pointer *pointer = weston_seat_get_pointer(self->seat);
    struct weston_touch *touch = weston_seat_get_touch(self->seat);

    if ( ( keyboard != NULL ) && ( keyboard->grab->interface != &_weston_desktop_seat_grab_keyboard_interface ) )
    {
        weston_keyboard_set_focus(keyboard, self->launcher_menu->surface);
        weston_keyboard_start_grab(keyboard, &self->grab.keyboard);
    }

    if ( ( pointer != NULL ) && ( pointer->grab->interface != &_weston_desktop_seat_grab_pointer_interface ) )
    {
        weston_pointer_set_focus(pointer, self->launcher_menu->view, 0, 0);
        weston_pointer_start_grab(pointer, &self->grab.pointer);
    }

    if ( ( touch != NULL ) && ( touch->grab->interface != &_weston_desktop_seat_grab_touch_interface ) )
    {
        weston_touch_set_focus(touch, self->launcher_menu->view);
        weston_touch_start_grab(touch, &self->grab.touch);
    }

    self->grab.initial_up = ( ( pointer == NULL ) || ( pointer->button_count == 0 ) );
}

static void
_weston_launcher_menu_seat_grab_end(struct weston_launcher_menu_seat *self, bool dismiss)
{
    if ( self == NULL )
        return;

    struct weston_keyboard *keyboard = weston_seat_get_keyboard(self->seat);
    struct weston_pointer *pointer = weston_seat_get_pointer(self->seat);
    struct weston_touch *touch = weston_seat_get_touch(self->seat);

    if ( ( keyboard != NULL ) && ( keyboard->grab->interface == &_weston_desktop_seat_grab_keyboard_interface ) )
        weston_keyboard_end_grab(keyboard);

    if ( ( pointer != NULL ) && ( pointer->grab->interface == &_weston_desktop_seat_grab_pointer_interface ) )
        weston_pointer_end_grab(pointer);

    if ((touch != NULL) && (touch->grab->interface == &_weston_desktop_seat_grab_touch_interface))
        weston_touch_end_grab(touch);

    if ( dismiss && ( self->launcher_menu->binding != NULL ) )
        zww_launcher_menu_v1_send_dismiss(self->launcher_menu->binding);
    self->launcher_menu->grab_seat = NULL;
}

static void
_weston_launcher_menu_seat_destroy(struct wl_listener *listener, void *data)
{
    struct weston_launcher_menu_seat *self = wl_container_of(listener, self, seat_destroy_listener);

    free(self);
}

static struct weston_launcher_menu_seat *
_weston_launcher_menu_seat_from_seat(struct weston_seat *seat, struct weston_launcher_menu *launcher_menu)
{
    struct wl_listener *listener;
    struct weston_launcher_menu_seat *self;

    listener = wl_signal_get(&seat->destroy_signal, _weston_launcher_menu_seat_destroy);
    if (listener != NULL)
        return wl_container_of(listener, self, seat_destroy_listener);

    self = zalloc(sizeof(struct weston_launcher_menu_seat));
    if ( self == NULL )
        return NULL;

    self->launcher_menu = launcher_menu;
    self->seat = seat;

    self->seat_destroy_listener.notify = _weston_launcher_menu_seat_destroy;
    wl_signal_add(&seat->destroy_signal, &self->seat_destroy_listener);

    self->grab.keyboard.interface = &_weston_desktop_seat_grab_keyboard_interface;
    self->grab.pointer.interface = &_weston_desktop_seat_grab_pointer_interface;
    self->grab.touch.interface = &_weston_desktop_seat_grab_touch_interface;

    return self;
}


static void
_weston_launcher_menu_surface_update_position(struct weston_launcher_menu *self)
{
    int x = 0, y = 0;
    switch ( self->position )
    {
    case WESTON_LAUNCHER_MENU_POSITION_DEFAULT:
    {
        struct weston_output *output = self->output;
        x = output->x + ( output->width / 2 - self->surface->width / 2 );
        y = output->y + ( output->height / 2 - self->surface->height / 2 );
    }
    break;
    case WESTON_LAUNCHER_MENU_POSITION_POINTER:
    {
        struct weston_pointer *pointer = weston_seat_get_pointer(self->grab_seat->seat);

        x = wl_fixed_to_int(pointer->x);
        y = wl_fixed_to_int(pointer->y);
    }
    break;
    case WESTON_LAUNCHER_MENU_POSITION_SURFACE:
    {
        x = self->target.view->geometry.x + self->target.geometry.x;
        y = self->target.view->geometry.y + self->target.geometry.y + self->target.geometry.height;
    }
    break;
    }

    weston_view_set_position(self->view, x, y);

    weston_surface_damage(self->surface);
    weston_compositor_schedule_repaint(self->surface->compositor);
}

static void
_weston_launcher_menu_surface_committed(struct weston_surface *surface, int32_t sx, int32_t sy)
{
    struct weston_launcher_menu *self = surface->committed_private;

    if ( ! weston_view_is_mapped(self->view) )
    {
        self->surface->is_mapped = true;
        self->view->is_mapped = true;
        weston_layer_entry_insert(&self->layer.view_list, &self->view->layer_link);
    }

    _weston_launcher_menu_surface_update_position(self);
}

static void
_weston_launcher_menu_surface_destroyed(struct wl_listener *listener, void *data)
{
    struct weston_launcher_menu *self = wl_container_of(listener, self, surface_destroy_listener);

    self->surface = NULL;

    _weston_launcher_menu_seat_grab_end(self->grab_seat, false);
}

static void
_weston_launcher_menu_view_destroyed(struct wl_listener *listener, void *data)
{
    struct weston_launcher_menu *self = wl_container_of(listener, self, view_destroy_listener);

    weston_view_damage_below(self->view);
    self->view = NULL;
}

static void
_weston_launcher_menu_request_destroy(struct wl_client *client, struct wl_resource *resource)
{
    wl_resource_destroy(resource);
}

static void
_weston_launcher_menu_show_common(struct weston_launcher_menu *self, struct wl_client *client, struct weston_surface *surface, struct weston_seat *grab_seat)
{
    struct weston_keyboard *keyboard = weston_seat_get_keyboard(grab_seat);

    if ( self->surface != NULL )
    {
        wl_resource_post_error(self->binding, ZWW_LAUNCHER_MENU_V1_ERROR_UNIQUE, "another surface has this role already");
        return;
    }

    if ( weston_surface_set_role(surface, "ww_launcher_menu", self->binding, ZWW_LAUNCHER_MENU_V1_ERROR_ROLE) < 0 )
        return;

    self->surface = surface;
    self->view = weston_view_create(self->surface);
    if ( self->view == NULL )
    {
        wl_client_post_no_memory(client);
        return;
    }

    self->surface->committed = _weston_launcher_menu_surface_committed;
    self->surface->committed_private = self;

    if ( ( keyboard != NULL ) && ( keyboard->focus != NULL ) && ( keyboard->focus->output != NULL ) )
        self->output = keyboard->focus->output;
    else
        self->output = _weston_launcher_menu_get_default_output(self);

    self->surface_destroy_listener.notify = _weston_launcher_menu_surface_destroyed;
    self->view_destroy_listener.notify = _weston_launcher_menu_view_destroyed;
    wl_signal_add(&self->surface->destroy_signal, &self->surface_destroy_listener);
    wl_signal_add(&self->view->destroy_signal, &self->view_destroy_listener);

    self->grab_seat = _weston_launcher_menu_seat_from_seat(grab_seat, self);
    _weston_launcher_menu_seat_grab_start(self->grab_seat);
}

static void
_weston_launcher_menu_show(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource)
{
    struct weston_launcher_menu *self = wl_resource_get_user_data(resource);
    struct weston_surface *surface = wl_resource_get_user_data(surface_resource);
    struct weston_seat *wseat = _weston_launcher_menu_get_default_seat(self);

    _weston_launcher_menu_show_common(self, client, surface, wseat);
    self->position = WESTON_LAUNCHER_MENU_POSITION_DEFAULT;
}

static void
_weston_launcher_menu_show_at_pointer(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource, struct wl_resource *seat_resource, uint32_t serial)
{
    struct weston_launcher_menu *self = wl_resource_get_user_data(resource);
    struct weston_surface *surface = wl_resource_get_user_data(surface_resource);
    struct weston_seat *wseat = wl_resource_get_user_data(seat_resource);

    struct weston_pointer *pointer = weston_seat_get_pointer(wseat);

    if ( ( pointer == NULL ) || ( pointer->grab_serial != serial ) )
    {
        wl_resource_post_error(resource, ZWW_LAUNCHER_MENU_V1_ERROR_SERIAL, "Invalid serial");
        return;
    }

    _weston_launcher_menu_show_common(self, client, surface, wseat);
    self->position = WESTON_LAUNCHER_MENU_POSITION_POINTER;
}

static void
_weston_launcher_menu_show_at_surface(struct wl_client *client, struct wl_resource *resource, struct wl_resource *surface_resource, struct wl_resource *seat_resource, uint32_t serial, int32_t x, int32_t y, int32_t width, int32_t height)
{
    struct weston_launcher_menu *self = wl_resource_get_user_data(resource);
    struct weston_surface *surface = wl_resource_get_user_data(surface_resource);
    struct weston_seat *wseat = wl_resource_get_user_data(seat_resource);

    struct weston_keyboard *keyboard = weston_seat_get_keyboard(wseat);
    struct weston_pointer *pointer = weston_seat_get_pointer(wseat);
    struct weston_touch *touch = weston_seat_get_touch(wseat);

    struct weston_view *target = NULL;
    if ( ( keyboard != NULL ) && ( keyboard->grab_serial == serial ) )
        target = wl_container_of(keyboard->focus->views.next, target, link);
    else if ( ( pointer != NULL ) && ( pointer->grab_serial == serial ) )
        target = pointer->focus;
    else if ( ( touch != NULL ) && ( touch->grab_serial == serial ) )
        target = touch->focus;
    else
    {
        wl_resource_post_error(resource, ZWW_LAUNCHER_MENU_V1_ERROR_SERIAL, "Invalid serial");
        return;
    }

    _weston_launcher_menu_show_common(self, client, surface, wseat);
    self->position = WESTON_LAUNCHER_MENU_POSITION_SURFACE;
    self->target.view = target;
    self->target.geometry.x = x;
    self->target.geometry.y = y;
    self->target.geometry.width = width;
    self->target.geometry.height = height;
}

static const struct zww_launcher_menu_v1_interface weston_launcher_menu_implementation = {
    .destroy = _weston_launcher_menu_request_destroy,
    .show = _weston_launcher_menu_show,
    .show_at_pointer = _weston_launcher_menu_show_at_pointer,
    .show_at_surface = _weston_launcher_menu_show_at_surface,
};

static void
_weston_launcher_menu_unbind(struct wl_resource *resource)
{
    struct weston_launcher_menu *self = wl_resource_get_user_data(resource);

    self->binding = NULL;
    _weston_launcher_menu_seat_grab_end(self->grab_seat, false);
}

static void
_weston_launcher_menu_bind(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct weston_launcher_menu *self = data;
    struct wl_resource *resource;

    resource = wl_resource_create(client, &zww_launcher_menu_v1_interface, version, id);
    wl_resource_set_implementation(resource, &weston_launcher_menu_implementation, self, _weston_launcher_menu_unbind);

    if ( self->binding != NULL )
    {
        wl_resource_post_error(resource, ZWW_LAUNCHER_MENU_V1_ERROR_BOUND, "interface object already bound");
        wl_resource_destroy(resource);
        return;
    }

    self->binding = resource;
}

static void
_weston_launcher_menu_output_destroyed(struct wl_listener *listener, void *data)
{
    struct weston_launcher_menu *self = wl_container_of(listener, self, output_destroyed_listener);
    struct weston_output *woutput = data;
}

WW_EXPORT int
wet_module_init(struct weston_compositor *compositor, int *argc, char *argv[])
{
    struct weston_launcher_menu *self;

    self = zalloc(sizeof(struct weston_launcher_menu));
    if ( self == NULL )
        return -1;

    self->compositor = compositor;

    wl_list_init(&self->seats);

    if ( wl_global_create(self->compositor->wl_display, &zww_launcher_menu_v1_interface, 1, self, _weston_launcher_menu_bind) == NULL)
        return -1;

    self->output_destroyed_listener.notify = _weston_launcher_menu_output_destroyed;
    wl_signal_add(&self->compositor->output_destroyed_signal, &self->output_destroyed_listener);

    weston_layer_init(&self->layer, self->compositor);
    weston_layer_set_position(&self->layer, WESTON_LAYER_POSITION_UI);

    return 0;
}
