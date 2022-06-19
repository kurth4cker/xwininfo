/*
 * Copyright (c) 2010, Oracle and/or its affiliates. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */
/*

Copyright 1993, 1998  The Open Group

Permission to use, copy, modify, distribute, and sell this software and its
documentation for any purpose is hereby granted without fee, provided that
the above copyright notice appear in all copies and that both that
copyright notice and this permission notice appear in supporting
documentation.

The above copyright notice and this permission notice shall be included
in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
IN NO EVENT SHALL THE OPEN GROUP BE LIABLE FOR ANY CLAIM, DAMAGES OR
OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.

Except as contained in this notice, the name of The Open Group shall
not be used in advertising or otherwise to promote the sale, use or
other dealings in this Software without prior written authorization
from The Open Group.

*/

#include "config.h"

#include <xcb/xcb.h>
#include <xcb/xproto.h>
#include <xcb/xcb_icccm.h>
#include <X11/cursorfont.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include "clientwin.h"
#include "dsimple.h"

/*
 * Just_display: A group of routines designed to make the writing of simple
 *               X11 applications which open a display but do not open
 *               any windows much faster and easier.  Unless a routine says
 *               otherwise, it may be assumed to require program_name
 *               to be already defined on entry.
 *
 * Written by Mark Lillibridge.   Last updated 7/1/87
 */


/* This stuff is defined in the calling program by dsimple.h */
const char    *program_name = "unknown_program";

/*
 * get_display_name (argc, argv) - return string representing display name
 * that would be used given the specified argument (i.e. if it's NULL, check
 * getenv("DISPLAY") - always returns a non-NULL pointer, though it may be
 * an unwritable constant, so is safe to printf() on platforms that crash
 * on NULL printf arguments.
 */
const char *get_display_name (const char *display_name)
{
    const char *name = display_name;

    if (!name) {
	name = getenv ("DISPLAY");
	if (!name)
	    name = "";
    }
    return (name);
}


/*
 * setup_display_and_screen: This routine opens up the correct display (i.e.,
 *                           it calls get_display_name) and then stores a
 *                           pointer to it in dpy.  The default screen
 *                           for this display is then stored in screen.
 */
void setup_display_and_screen (
    const char *display_name,
    xcb_connection_t **dpy,	/* MODIFIED */
    xcb_screen_t **screen)	/* MODIFIED */
{
    int screen_number, i, err;

    /* Open Display */
    *dpy = xcb_connect (display_name, &screen_number);
    if ((err = xcb_connection_has_error (*dpy)) != 0) {
        switch (err) {
        case XCB_CONN_CLOSED_MEM_INSUFFICIENT:
            fatal_error ("Failed to allocate memory in xcb_connect");
        case XCB_CONN_CLOSED_PARSE_ERR:
            fatal_error ("unable to parse display name \"%s\"",
                         get_display_name(display_name) );
#ifdef XCB_CONN_CLOSED_INVALID_SCREEN
        case XCB_CONN_CLOSED_INVALID_SCREEN:
            fatal_error ("invalid screen %d in display \"%s\"",
                         screen_number, get_display_name(display_name));
#endif
        default:
            fatal_error ("unable to open display \"%s\"",
                         get_display_name(display_name) );
        }
    }

    if (screen) {
	/* find our screen */
	const xcb_setup_t *setup = xcb_get_setup(*dpy);
	xcb_screen_iterator_t screen_iter = xcb_setup_roots_iterator(setup);
	int screen_count = xcb_setup_roots_length(setup);
	if (screen_count <= screen_number)
	{
	    fatal_error ("unable to access screen %d, max is %d",
			 screen_number, screen_count-1 );
	}

	for (i = 0; i < screen_number; i++)
	    xcb_screen_next(&screen_iter);
	*screen = screen_iter.data;
    }
}

/*
 * xcb equivalent of XCreateFontCursor
 */
static xcb_cursor_t
create_font_cursor (xcb_connection_t *dpy, uint16_t glyph)
{
    static xcb_font_t cursor_font;
    xcb_cursor_t cursor;

    if (!cursor_font) {
	cursor_font = xcb_generate_id (dpy);
	xcb_open_font (dpy, cursor_font, strlen ("cursor"), "cursor");
    }

    cursor = xcb_generate_id (dpy);
    xcb_create_glyph_cursor (dpy, cursor, cursor_font, cursor_font,
			     glyph, glyph + 1,
                             0, 0, 0, 0xffff, 0xffff, 0xffff);  /* rgb, rgb */

    return cursor;
}

/*
 * Routine to let user select a window using the mouse
 */

xcb_window_t select_window(xcb_connection_t *dpy,
			   const xcb_screen_t *screen,
			   int descend)
{
    xcb_cursor_t cursor;
    xcb_generic_event_t *event;
    xcb_window_t target_win = XCB_WINDOW_NONE;
    xcb_window_t root = screen->root;
    int buttons = 0;
    xcb_generic_error_t *err;
    xcb_grab_pointer_cookie_t grab_cookie;
    xcb_grab_pointer_reply_t *grab_reply;

    /* Make the target cursor */
    cursor = create_font_cursor (dpy, XC_crosshair);

    /* Grab the pointer using target cursor, letting it room all over */
    grab_cookie = xcb_grab_pointer
	(dpy, 1, root,
	 XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_BUTTON_RELEASE,
	 XCB_GRAB_MODE_SYNC, XCB_GRAB_MODE_ASYNC,
	 root, cursor, XCB_TIME_CURRENT_TIME);
    grab_reply = xcb_grab_pointer_reply (dpy, grab_cookie, &err);
    if (grab_reply->status != XCB_GRAB_STATUS_SUCCESS)
	fatal_error ("Can't grab the mouse.");

    /* Let the user select a window... */
    while ((target_win == XCB_WINDOW_NONE) || (buttons != 0)) {
	/* allow one more event */
	xcb_allow_events (dpy, XCB_ALLOW_SYNC_POINTER, XCB_TIME_CURRENT_TIME);
	xcb_flush (dpy);
	event = xcb_wait_for_event (dpy);
	if (event == NULL)
	    fatal_error ("Fatal IO error");
	switch (event->response_type & 0x7f) {
	case XCB_BUTTON_PRESS:
	{
	    xcb_button_press_event_t *bp = (xcb_button_press_event_t *)event;

	    if (target_win == XCB_WINDOW_NONE) {
		target_win = bp->child; /* window selected */
		if (target_win == XCB_WINDOW_NONE)
		    target_win = root;
	    }
	    buttons++;
	    break;
	}
	case XCB_BUTTON_RELEASE:
	    if (buttons > 0) /* there may have been some down before we started */
		buttons--;
	    break;
	default:
	    /* just discard all other events */
	    break;
	}
	free (event);
    }

    xcb_ungrab_pointer (dpy, XCB_TIME_CURRENT_TIME); /* Done with pointer */

    if (!descend || (target_win == root))
	return (target_win);

    target_win = find_client (dpy, root, target_win);

    return (target_win);
}


/*
 * window_with_name: routine to locate a window with a given name on a display.
 *                   If no window with the given name is found, 0 is returned.
 *                   If more than one window has the given name, the first
 *                   one found will be returned.  Only top and its subwindows
 *                   are looked at.  Normally, top should be the RootWindow.
 */

struct wininfo_cookies {
    xcb_get_property_cookie_t get_net_wm_name;
    xcb_get_property_cookie_t get_wm_name;
    xcb_query_tree_cookie_t query_tree;
};

static xcb_atom_t atom_net_wm_name, atom_utf8_string;

# define xcb_get_net_wm_name(Dpy, Win)			 \
    xcb_get_property (Dpy, 1, Win, atom_net_wm_name, \
		      atom_utf8_string, 0, BUFSIZ)


static xcb_window_t
recursive_window_with_name  (
    xcb_connection_t *dpy,
    xcb_window_t window,
    struct wininfo_cookies *cookies,
    const char *name,
    size_t namelen)
{
    xcb_window_t *children;
    unsigned int nchildren;
    unsigned int i;
    xcb_window_t w = 0;
    xcb_generic_error_t *err;
    xcb_query_tree_reply_t *tree;
    struct wininfo_cookies *child_cookies;
    xcb_get_property_reply_t *prop;

    if (cookies->get_net_wm_name.sequence) {
	prop = xcb_get_property_reply (dpy, cookies->get_net_wm_name, &err);

	if (prop) {
	    if (prop->type == atom_utf8_string) {
		const char *prop_name = xcb_get_property_value (prop);
		int prop_name_len = xcb_get_property_value_length (prop);

		/* can't use strcmp, since prop.name is not null terminated */
		if ((namelen == (size_t) prop_name_len) &&
		    memcmp (prop_name, name, namelen) == 0) {
		    w = window;
		}
	    }
	    free (prop);
	} else if (err) {
	    if (err->response_type == 0)
		print_x_error (dpy, err);
	    return 0;
	}
    }

    if (w) {
	xcb_discard_reply (dpy, cookies->get_wm_name.sequence);
    } else {
	xcb_icccm_get_text_property_reply_t nameprop;

	if (xcb_icccm_get_wm_name_reply (dpy, cookies->get_wm_name,
					 &nameprop, &err)) {
	    /* can't use strcmp, since nameprop.name is not null terminated */
	    if ((namelen == (size_t) nameprop.name_len) &&
		memcmp (nameprop.name, name, namelen) == 0) {
		w = window;
	    }

	    xcb_icccm_get_text_property_reply_wipe (&nameprop);
	}
	else if (err) {
	    if (err->response_type == 0)
		print_x_error (dpy, err);
	    return 0;
	}
    }

    if (w)
    {
	xcb_discard_reply (dpy, cookies->query_tree.sequence);
	return w;
    }

    tree = xcb_query_tree_reply (dpy, cookies->query_tree, &err);
    if (!tree) {
	if (err->response_type == 0)
	    print_x_error (dpy, err);
	return 0;
    }

    nchildren = xcb_query_tree_children_length (tree);
    children = xcb_query_tree_children (tree);
    child_cookies = calloc(nchildren, sizeof(struct wininfo_cookies));

    if (child_cookies == NULL)
	fatal_error("Failed to allocate memory in recursive_window_with_name");

    for (i = 0; i < nchildren; i++) {
	if (atom_net_wm_name && atom_utf8_string)
	    child_cookies[i].get_net_wm_name =
		xcb_get_net_wm_name (dpy, children[i]);
	child_cookies[i].get_wm_name = xcb_icccm_get_wm_name (dpy, children[i]);
	child_cookies[i].query_tree = xcb_query_tree (dpy, children[i]);
    }
    xcb_flush (dpy);

    for (i = 0; i < nchildren; i++) {
	w = recursive_window_with_name (dpy, children[i],
					&child_cookies[i], name, namelen);
	if (w)
	    break;
    }

    if (w)
    {
	/* clean up remaining replies */
	for (/* keep previous i */; i < nchildren; i++) {
	    if (child_cookies[i].get_net_wm_name.sequence)
		xcb_discard_reply (dpy,
				   child_cookies[i].get_net_wm_name.sequence);
	    xcb_discard_reply (dpy, child_cookies[i].get_wm_name.sequence);
	    xcb_discard_reply (dpy, child_cookies[i].query_tree.sequence);
	}
    }

    free (child_cookies);
    free (tree); /* includes storage for children[] */
    return (w);
}

xcb_window_t
window_with_name (
    xcb_connection_t *dpy,
    xcb_window_t top,
    const char *name)
{
    struct wininfo_cookies cookies;

    atom_net_wm_name = get_atom (dpy, "_NET_WM_NAME");
    atom_utf8_string = get_atom (dpy, "UTF8_STRING");

    if (atom_net_wm_name && atom_utf8_string)
	cookies.get_net_wm_name = xcb_get_net_wm_name (dpy, top);
    else
	cookies.get_net_wm_name.sequence = 0;
    cookies.get_wm_name = xcb_icccm_get_wm_name (dpy, top);
    cookies.query_tree = xcb_query_tree (dpy, top);
    xcb_flush (dpy);
    return recursive_window_with_name(dpy, top, &cookies, name, strlen(name));
}


/*
 * Standard fatal error routine - call like printf
 */
void fatal_error (const char *msg, ...)
{
    va_list args;
    fflush (stdout);
    fflush (stderr);
    fprintf (stderr, "%s: error: ", program_name);
    va_start (args, msg);
    vfprintf (stderr, msg, args);
    va_end (args);
    fprintf (stderr, "\n");
    exit (EXIT_FAILURE);
}

/*
 * Print X error information like the default Xlib error handler
 */
void
print_x_error (
    xcb_connection_t *dpy,
    xcb_generic_error_t *err
    )
{
    char buffer[256] = "";

    if ((err == NULL) || (err->response_type != 0)) /* not an error */
	return;

    /* Todo: find a more user friendly way to show request/extension info */
    if (err->error_code >= 128)
    {
	fprintf (stderr, "X Extension Error:  Error code %d\n",
		 err->error_code);
    }
    else
    {
	switch (err->error_code)
	{
	    case XCB_REQUEST:
		snprintf (buffer, sizeof(buffer), "Bad Request");
		break;

	    case XCB_VALUE:
		snprintf (buffer, sizeof(buffer),
			  "Bad Value: 0x%x", err->resource_id);
		break;

	    case XCB_WINDOW:
		snprintf (buffer, sizeof(buffer),
			  "Bad Window: 0x%x", err->resource_id);
		break;

	    case XCB_PIXMAP:
		snprintf (buffer, sizeof(buffer),
			  "Bad Pixmap: 0x%x", err->resource_id);
		break;

	    case XCB_ATOM:
		snprintf (buffer, sizeof(buffer),
			  "Bad Atom: 0x%x", err->resource_id);
		break;

	    case XCB_CURSOR:
		snprintf (buffer, sizeof(buffer),
			  "Bad Cursor: 0x%x", err->resource_id);
		break;

	    case XCB_FONT:
		snprintf (buffer, sizeof(buffer),
			  "Bad Font: 0x%x", err->resource_id);
		break;

	    case XCB_MATCH:
		snprintf (buffer, sizeof(buffer), "Bad Match");
		break;

	    case XCB_DRAWABLE:
		snprintf (buffer, sizeof(buffer),
			  "Bad Drawable: 0x%x", err->resource_id);
		break;

	    case XCB_ACCESS:
		snprintf (buffer, sizeof(buffer), "Access Denied");
		break;

	    case XCB_ALLOC:
		snprintf (buffer, sizeof(buffer),
			  "Server Memory Allocation Failure");
		break;

	    case XCB_COLORMAP:
		snprintf (buffer, sizeof(buffer),
			  "Bad Color: 0x%x", err->resource_id);
		break;

	    case XCB_G_CONTEXT:
		snprintf (buffer, sizeof(buffer),
			  "Bad GC: 0x%x", err->resource_id);
		break;

	    case XCB_ID_CHOICE:
		snprintf (buffer, sizeof(buffer),
			  "Bad XID: 0x%x", err->resource_id);
		break;

	    case XCB_NAME:
		snprintf (buffer, sizeof(buffer),
			  "Bad Name");
		break;

	    case XCB_LENGTH:
		snprintf (buffer, sizeof(buffer),
			  "Bad Request Length");
		break;

	    case XCB_IMPLEMENTATION:
		snprintf (buffer, sizeof(buffer),
			  "Server Implementation Failure");
		break;

	    default:
		snprintf (buffer, sizeof(buffer), "Unknown error");
		break;
	}
	fprintf (stderr, "X Error: %d: %s\n", err->error_code, buffer);
    }

    fprintf (stderr, "  Request Major code: %d\n", err->major_code);
    if (err->major_code >= 128)
    {
	fprintf (stderr, "  Request Minor code: %d\n", err->minor_code);
    }

    fprintf (stderr, "  Request serial number: %d\n", err->full_sequence);
}

/*
 * Cache for atom lookups in either direction
 */
struct atom_cache_entry {
    xcb_atom_t atom;
    const char *name;
    xcb_intern_atom_cookie_t intern_atom;
    struct atom_cache_entry *next;
};

static struct atom_cache_entry *atom_cache;

/*
 * Send a request to the server for an atom by name
 * Does not create the atom if it is not already present
 */
struct atom_cache_entry *Intern_Atom (xcb_connection_t * dpy, const char *name)
{
    struct atom_cache_entry *a;

    for (a = atom_cache ; a != NULL ; a = a->next) {
	if (strcmp (a->name, name) == 0)
	    return a; /* already requested or found */
    }

    a = calloc(1, sizeof(struct atom_cache_entry));
    if (a != NULL) {
	a->name = name;
	a->intern_atom = xcb_intern_atom (dpy, 1, strlen (name), (name));
	a->next = atom_cache;
	atom_cache = a;
    }
    return a;
}

/* Get an atom by name when it is needed. */
xcb_atom_t get_atom (xcb_connection_t * dpy, const char *name)
{
    struct atom_cache_entry *a = Intern_Atom (dpy, name);

    if (a == NULL)
	return XCB_ATOM_NONE;

    if (a->atom == XCB_ATOM_NONE) {
	xcb_intern_atom_reply_t *reply;

	reply = xcb_intern_atom_reply(dpy, a->intern_atom, NULL);
	if (reply) {
	    a->atom = reply->atom;
	    free (reply);
	} else {
	    a->atom = (xcb_atom_t) -1;
	}
    }
    if (a->atom == (xcb_atom_t) -1) /* internal error */
	return XCB_ATOM_NONE;

    return a->atom;
}

/* Get the name for an atom when it is needed. */
const char *get_atom_name (xcb_connection_t * dpy, xcb_atom_t atom)
{
    struct atom_cache_entry *a;

    for (a = atom_cache ; a != NULL ; a = a->next) {
	if (a->atom == atom)
	    return a->name; /* already requested or found */
    }

    a = calloc(1, sizeof(struct atom_cache_entry));
    if (a != NULL) {
	xcb_get_atom_name_cookie_t cookie = xcb_get_atom_name (dpy, atom);
	xcb_get_atom_name_reply_t *reply
	    = xcb_get_atom_name_reply (dpy, cookie, NULL);

	a->atom = atom;
	if (reply) {
	    int len = xcb_get_atom_name_name_length (reply);
	    char *name = malloc(len + 1);
	    if (name) {
		memcpy (name, xcb_get_atom_name_name (reply), len);
		name[len] = '\0';
		a->name = name;
	    }
	    free (reply);
	}

	a->next = atom_cache;
	atom_cache = a;

	return a->name;
    }
    return NULL;
}
