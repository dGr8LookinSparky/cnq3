/*
===========================================================================
Copyright (C) 1999-2005 Id Software, Inc.

This file is part of Quake III Arena source code.

Quake III Arena source code is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the License,
or (at your option) any later version.

Quake III Arena source code is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with Quake III Arena source code; if not, write to the Free Software
Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
===========================================================================
*/
/*
** GLW_IMP.C
**
** This file contains ALL Linux specific stuff having to do with the
** OpenGL refresh.  When a port is being made the following functions
** must be implemented by the port:
**
** GLimp_EndFrame
** GLimp_Init
** GLimp_Shutdown
** GLimp_SwitchFullscreen
** GLimp_SetGamma
**
*/

#if !USE_SDL_VIDEO

#include <termios.h>
#include <sys/ioctl.h>
#ifdef __linux__
  #include <sys/stat.h>
  #include <sys/vt.h>
#endif
#include <dlfcn.h>
#include <signal.h>
#include <pthread.h>
#include <semaphore.h>

#include <GL/glx.h>

#include <X11/keysym.h>
#include <X11/XKBlib.h>
#include <X11/extensions/XInput2.h>

#include "../renderer/tr_local.h"
#include "../client/client.h"
#include "linux_local.h"
#include "unix_glw.h"

//#if !defined(__sun)
//#include <X11/extensions/xf86dga.h>
//#include <X11/extensions/xf86vmode.h>
//#endif


static Display *dpy = NULL;
static Window win = 0;


///////////////////////////////////////////////////////////////


struct Mouse {
	virtual qbool Init() { return qtrue; }
	virtual qbool Activate( qbool active );
	virtual void Shutdown() {}
	virtual void Read( int* mx, int* my ) = 0;
	virtual qbool ProcessEvent( XEvent& event ) { return qfalse; } // returns true if the event was handled

	Mouse() : active(qfalse) {}

	static const int buttons[];

private:
	qbool active;
};

static Mouse* mouse;
const int Mouse::buttons[] = { 0, K_MOUSE1, K_MOUSE3, K_MOUSE2, K_MWHEELUP, K_MWHEELDOWN, 0, 0, K_MOUSE4, K_MOUSE5 };


qbool Mouse::Activate( qbool _active )
{
	if (active == _active)
		return qfalse;

	active = _active;

	return qtrue;
}


///////////////////////////////////////////////////////////////


struct xmouse_t : public Mouse {
	virtual qbool Activate( qbool active );
	virtual void Read( int* mx, int* my );
	virtual qbool ProcessEvent( XEvent& event );

	int x, y, prev_x, prev_y;
	int window_center_x, window_center_y;
};

static xmouse_t xmouse;


qbool xmouse_t::Activate( qbool active )
{
	if (!active)
		return qtrue;

	window_center_x = glConfig.vidWidth / 2;
	window_center_y = glConfig.vidHeight / 2;

	XWarpPointer( dpy, None, win, 0,0,0,0, window_center_x, window_center_y );
	return qtrue;
}


void xmouse_t::Read( int* mx, int* my )
{
	// we could just use XQueryPointer here
	// but since we're processing events anyway we might as well use the data from those
	*mx = x;
	*my = y;
	x = y = 0;

	if (*mx || *my) {
		XWarpPointer( dpy, None, win, 0,0,0,0, window_center_x, window_center_y );
	}
}


qbool xmouse_t::ProcessEvent( XEvent& event )
{
	switch (event.type) {

		case MotionNotify: {
			// if this is exactly recentering the mouse, it's probably from our warp
			// but there's no way to actually handle this problem well in X
			if ((event.xmotion.x == window_center_x) && (event.xmotion.y == window_center_y)) {
				//Com_Printf( "WARP: mx %d  my %d \n", event.xmotion.x, event.xmotion.y );
				prev_x = event.xmotion.x;
				prev_y = event.xmotion.y;
				break;
			}

			//Com_Printf( "mx %d  my %d \n", event.xmotion.x, event.xmotion.y );
			// note that accumulating motion events like this is actually wrong
			// because button events are processed immediately
			// but the windows code has the same bug, and nobody seems upset by it there
			x += (event.xmotion.x - prev_x);
			y += (event.xmotion.y - prev_y);
			prev_x = event.xmotion.x;
			prev_y = event.xmotion.y;
			return qtrue;
		}
		break;

		case ButtonPress:
		case ButtonRelease:
			//Com_Printf( "Button %u \n", event.xbutton.button );
			if ((event.xbutton.button > 0) && (event.xbutton.button < (sizeof(buttons) / sizeof(buttons[0])))) {
				int button = buttons[event.xbutton.button];
				if (button) {
					Sys_QueEvent( 0, SE_KEY, button, (event.type == ButtonPress), 0, NULL );
				}
			}
		break;

	}

	return qfalse;
}


///////////////////////////////////////////////////////////////


// this is a total suckfest. debian didn't multiarch xinput for some reason,
// so using it means having to dlsym everything or you can't build both i386 and x64


typedef XIDeviceInfo* (*xiQueryDevice)( Display *display, int deviceid, int *ndevices_return );
typedef void (*xiFreeDeviceInfo)( XIDeviceInfo* );
typedef Status (*xiQueryVersion)( Display *display, int *major_version_inout, int *minor_version_inout );
typedef Status (*xiSelectEvents)( Display *display, Window win, XIEventMask *masks, int num_masks );

static void* libxi = 0;
static xiQueryDevice pxiQueryDevice;
static xiFreeDeviceInfo pxiFreeDeviceInfo;
static xiQueryVersion pxiQueryVersion;
static xiSelectEvents pxiSelectEvents;
static int xiOPCode;


static void* XI_GetProcAddress( const char* symbol )
{
	if (!libxi)
		return 0;
	return dlsym( libxi, symbol );
}
#define XI(F) { pxi##F = (xi##F)XI_GetProcAddress( "XI"#F ); }


struct rawmouse_t : public Mouse {
	virtual qbool Init();
	//virtual qbool Activate( qbool active );
	virtual void Shutdown();
	virtual void Read( int* mx, int* my );
	virtual qbool ProcessEvent( XEvent& event );

private:
	int x, y, prev_x, prev_y;
	int mode;

	static int FindMouse();
	static qbool ValidateMouse( const XIDeviceInfo* info );
};

static rawmouse_t rawmouse;


qbool rawmouse_t::ValidateMouse( const XIDeviceInfo* info )
{
	if (!info->enabled)
		return qfalse;

	const XIAnyClassInfo** classes = (const XIAnyClassInfo**)info->classes;
	for (int i = 0; i < info->num_classes; ++i) {
		if (classes[i]->type == XIValuatorClass) {
			const XIValuatorClassInfo* v = (const XIValuatorClassInfo*)classes[i];
			if ((v->mode == XIModeRelative) && !v->resolution)
				return qfalse;   // invalid combination (eg XTEST mouse)
			ri.Printf( PRINT_DEVELOPER, "ValidateMouse: accepted device %d\n", info->deviceid );
			rawmouse.mode = v->mode;
			return qtrue;
		}
	}

	return qfalse;
}


int rawmouse_t::FindMouse()
{
	int n, device = 0;
	XIDeviceInfo* info = pxiQueryDevice( dpy, XIAllDevices, &n );

	cvar_t* m_device = Cvar_Get( "m_device", "0", 0 );
	if (m_device->integer) {
		for (int i = 0; i < n; ++i) {
			if ((info[i].deviceid == m_device->integer) && ValidateMouse(&info[i])) {
				device = info[i].deviceid;
				pxiFreeDeviceInfo( info );
				return device;
			}
		}
	}

	for (int i = 0; i < n; ++i) {
		if ((info[i].use == XIMasterPointer) && ValidateMouse(&info[i])) {
			device = info[i].deviceid;
			break;
		}
	}

	pxiFreeDeviceInfo( info );
	return device;
}


qbool rawmouse_t::Init()
{
	int event, error;
	if (!XQueryExtension(dpy, "XInputExtension", &xiOPCode, &event, &error))
		return qfalse;

	libxi = dlopen( "libXi.so.6", RTLD_GLOBAL | RTLD_LAZY );
	XI(QueryDevice);
	XI(FreeDeviceInfo);
	XI(QueryVersion);
	XI(SelectEvents);

	int major = 2, minor = 1;
	if (pxiQueryVersion(dpy, &major, &minor) == BadRequest) {
		return qfalse;
	}

	XIEventMask eventmask;
	unsigned char mask[4] = { 0 };
	eventmask.deviceid = FindMouse();
	if (!eventmask.deviceid) {
		return qfalse;
	}

	eventmask.mask_len = sizeof(mask);
	eventmask.mask = mask;
	XISetMask( mask, XI_RawMotion );
	XISetMask( mask, XI_RawButtonPress );
	XISetMask( mask, XI_RawButtonRelease );
	if (pxiSelectEvents( dpy, DefaultRootWindow(dpy), &eventmask, 1 ) == BadRequest) {
		return qfalse;
	}

	XSelectInput( dpy, win, KeyPressMask | KeyReleaseMask );

	return qtrue;
}


void rawmouse_t::Shutdown()
{
	if (libxi) {
		dlclose( libxi );
		libxi = 0;
	}
}


void rawmouse_t::Read( int* mx, int* my )
{
	*mx = x;
	*my = y;
	x = y = 0;
}


qbool rawmouse_t::ProcessEvent( XEvent& event )
{
	if ((event.xcookie.type != GenericEvent) || (event.xcookie.extension != xiOPCode))
		return qfalse;

	if (!XGetEventData(dpy, &event.xcookie))
		return qfalse;

	const XIRawEvent* raw = (const XIRawEvent*)event.xcookie.data;
	switch (event.xcookie.evtype) {
		case XI_RawMotion: {
			int mx = 0, my = 0;
			const double* val = raw->raw_values;
			for (int i = 0; i < raw->valuators.mask_len * 8; ++i) {
				if (XIMaskIsSet(raw->valuators.mask, i)) {
					//Com_Printf( "RawMotion on axis %d: %lf \n", i, *val );
					if (i == 0)
						mx += *val;
					else if (i == 1)
						my += *val;
					++val;
				}
			}
			if (mode == XIModeRelative) {
				x += mx;
				y += my;
			}
			// workaround for virtualbox bugs
			if (mode == XIModeAbsolute) {
				//Com_Printf( "mx %d  my %d    dx %d  dy %d \n", mx, my, (mx - prev_x), (my - prev_y) );
				if (mx) { x += (mx - prev_x) * 1920 / 0x8000; prev_x = mx; }
				if (my) { y += (my - prev_y) * 1200 / 0x8000; prev_y = my; }
			}
		}
		break;

		case XI_RawButtonPress:
		case XI_RawButtonRelease:
			//Com_Printf( "XI_RawButton %u \n", raw->detail );
			if ((raw->detail > 0) && (raw->detail < (sizeof(buttons) / sizeof(buttons[0])))) {
				int button = buttons[raw->detail];
				if (button) {
					Sys_QueEvent( 0, SE_KEY, button, (event.xcookie.evtype == XI_RawButtonPress), 0, NULL );
				}
			}
		break;
	}

	XFreeEventData(dpy, &event.xcookie);
	return qtrue;
}


///////////////////////////////////////////////////////////////


#if 0

static KeySym keys[255];


static void IN_InitKeyboard()
{
	int min, max;
	XDisplayKeycodes( dpy, &min, &max );

/*
	for (int i = min; i < max; ++i) {
		KeySym keysym = XkbKeycodeToKeysym( dpy, i, 0, 0 );
		if ((keysym > 32) && (keysym < 127)) {
			Com_Printf( "key[%d] : sym %04X %c \n", i, keysym, keysym );
		} else {
			Com_Printf( "key[%d] : sym %04X %s \n", i, keysym, XKeysymToString(keysym) );
		}
	}
*/

	for (int i = min; i < max; ++i) {
		KeySym keysym = XkbKeycodeToKeysym( dpy, i, 0, 0 );
		keys[i] = keysym;
	}
}


static int TranslateKey( XKeyEvent* ev )
{
	static char buf[64];
	KeySym keysym;
	int XLookupRet = XLookupString( ev, buf, sizeof buf, &keysym, 0 );

	switch (ev->keycode) {
		//case XK_grave: return '~';
		case XK_Tab: return K_TAB;
		case XK_Return: return K_ENTER;

		case XK_F1:  return K_F1;
		case XK_F2:  return K_F2;
		case XK_F3:  return K_F3;
		case XK_F4:  return K_F4;
		case XK_F5:  return K_F5;
		case XK_F6:  return K_F6;
		case XK_F7:  return K_F7;
		case XK_F8:  return K_F8;
		case XK_F9:  return K_F9;
		case XK_F10: return K_F10;
		case XK_F11: return K_F11;
		case XK_F12: return K_F12;
	}

	if (ev->keycode > 255) {
		Com_Printf( "key %ul out of range \n", ev->keycode );
		return 0;	// whatever it is, it's a Magic Key that's not in our list
	}

	// the keyCODE is just the key number, so map to ascii first
	int key = (int)keys[ev->keycode];

	if ((key >= 'A') && (key <= 'Z'))
		return (key - 'A' + 'a');

	return key;
}

#endif


///////////////////////////////////////////////////////////////


/*
** NOTE TTimo the keyboard handling is done with KeySyms
**   that means relying on the keyboard mapping provided by X
**   in-game it would probably be better to use KeyCode (i.e. hardware key codes)
**   you would still need the KeySyms in some cases, such as for the console and all entry textboxes
**     (cause there's nothing worse than a qwerty mapping on a french keyboard)
*/

static const char* TranslateKey( XKeyEvent* ev, int* key )
{
	static char raw[2], translated[2];
	KeySym keysym;
	int len;

	*key = 0;

	// get the normal interpretation of the key without messing with shifts, for SE_CHAR
	XLookupString( ev, translated, sizeof(translated), &keysym, 0 );

	// then get the keysym that we actually want with no shifts at all, for SE_KEY
	ev->state = 0;
	len = XLookupString( ev, raw, sizeof(raw), &keysym, 0 );


  switch (keysym)
  {
  case XK_KP_Page_Up:
  case XK_KP_9:  *key = K_KP_PGUP; break;
  case XK_Page_Up:   *key = K_PGUP; break;

  case XK_KP_Page_Down:
  case XK_KP_3: *key = K_KP_PGDN; break;
  case XK_Page_Down:   *key = K_PGDN; break;

  case XK_KP_Home: *key = K_KP_HOME; break;
  case XK_KP_7: *key = K_KP_HOME; break;
  case XK_Home:  *key = K_HOME; break;

  case XK_KP_End:
  case XK_KP_1:   *key = K_KP_END; break;
  case XK_End:   *key = K_END; break;

  case XK_KP_Left: *key = K_KP_LEFTARROW; break;
  case XK_KP_4: *key = K_KP_LEFTARROW; break;
  case XK_Left:  *key = K_LEFTARROW; break;

  case XK_KP_Right: *key = K_KP_RIGHTARROW; break;
  case XK_KP_6: *key = K_KP_RIGHTARROW; break;
  case XK_Right:  *key = K_RIGHTARROW;    break;

  case XK_KP_Down:
  case XK_KP_2:    *key = K_KP_DOWNARROW; break;
  case XK_Down:  *key = K_DOWNARROW; break;

  case XK_KP_Up:
  case XK_KP_8:    *key = K_KP_UPARROW; break;
  case XK_Up:    *key = K_UPARROW;   break;

  case XK_Escape: *key = K_ESCAPE;    break;

  case XK_KP_Enter: *key = K_KP_ENTER;  break;
  case XK_Return: *key = K_ENTER;    break;

  case XK_Tab:    *key = K_TAB;      break;

	case XK_F1:   *key = K_F1;   break;
	case XK_F2:   *key = K_F2;   break;
	case XK_F3:   *key = K_F3;   break;
	case XK_F4:   *key = K_F4;   break;
	case XK_F5:   *key = K_F5;   break;
	case XK_F6:   *key = K_F6;   break;
	case XK_F7:   *key = K_F7;   break;
	case XK_F8:   *key = K_F8;   break;
	case XK_F9:   *key = K_F9;   break;
	case XK_F10:  *key = K_F10;  break;
	case XK_F11:  *key = K_F11;  break;
	case XK_F12:  *key = K_F12;  break;

	case XK_BackSpace: *key = K_BACKSPACE; break;

  case XK_KP_Delete:
  case XK_KP_Decimal: *key = K_KP_DEL; break;
  case XK_Delete: *key = K_DEL; break;

  case XK_Pause:  *key = K_PAUSE;    break;

  case XK_Shift_L:
  case XK_Shift_R:  *key = K_SHIFT;   break;

  case XK_Execute:
  case XK_Control_L:
  case XK_Control_R:  *key = K_CTRL;  break;

  case XK_Alt_L:
  case XK_Meta_L:
  case XK_Alt_R:
  case XK_Meta_R: *key = K_ALT;     break;

  case XK_KP_Begin: *key = K_KP_5;  break;

  case XK_Insert:   *key = K_INS; break;
  case XK_KP_Insert:
  case XK_KP_0: *key = K_KP_INS; break;

	case XK_KP_Add:      *key = K_KP_PLUS; break;
	case XK_KP_Divide:   *key = K_KP_SLASH; break;
	case XK_KP_Multiply: *key = K_KP_STAR; break;
	case XK_KP_Subtract: *key = K_KP_MINUS; break;
/*
    // bk001130 - from cvs1.17 (mkv)
  case XK_exclam: *key = '1'; break;
  case XK_at: *key = '2'; break;
  case XK_numbersign: *key = '3'; break;
  case XK_dollar: *key = '4'; break;
  case XK_percent: *key = '5'; break;
  case XK_asciicircum: *key = '6'; break;
  case XK_ampersand: *key = '7'; break;
  case XK_asterisk: *key = '8'; break;
  case XK_parenleft: *key = '9'; break;
  case XK_parenright: *key = '0'; break;
*/
  // weird french keyboards ..
  // NOTE: console toggle is hardcoded in cl_keys.c, can't be unbound
  //   cleaner would be .. using hardware key codes instead of the key syms
  //   could also add a new K_KP_CONSOLE
  case XK_twosuperior: *key = '~'; break;

  // https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=472
  case XK_space:
  case XK_KP_Space: *key = K_SPACE; break;

		default:
			if (len) {
				*key = raw[0];
			} else {
				ri.Printf( PRINT_DEVELOPER, "XLookupString failed on KeySym %d\n", keysym );
				return NULL;
			}
		break;
	}

	return translated;
}


///////////////////////////////////////////////////////////////


static qboolean mouse_avail;
static qboolean mouse_active = qfalse;

static int mouse_accel_numerator;
static int mouse_accel_denominator;
static int mouse_threshold;

#define KEY_MASK (KeyPressMask | KeyReleaseMask)
#define MOUSE_MASK (ButtonPressMask | ButtonReleaseMask | PointerMotionMask | ButtonMotionMask )
#define X_MASK (KEY_MASK | MOUSE_MASK | VisibilityChangeMask | StructureNotifyMask )
cvar_t* in_nograb;


static Cursor CreateNullCursor()
{
	Pixmap cursormask = XCreatePixmap( dpy, win, 1, 1, 1 );

	XGCValues xgc;
	xgc.function = GXclear;
	GC gc =  XCreateGC( dpy, cursormask, GCFunction, &xgc );
	XFillRectangle( dpy, cursormask, gc, 0, 0, 1, 1 );

	XColor colour;
	colour.pixel = 0;
	colour.red = 0;
	colour.flags = DoRed;

	Cursor cursor = XCreatePixmapCursor( dpy, cursormask, cursormask, &colour, &colour, 0, 0 );

	XFreePixmap( dpy, cursormask );
	XFreeGC( dpy, gc );
	return cursor;
}


static void install_grabs()
{
	XSync( dpy, False );

	XDefineCursor( dpy, win, CreateNullCursor() );

	XGrabPointer( dpy, win, False, MOUSE_MASK, GrabModeAsync, GrabModeAsync, win, None, CurrentTime );

	XGetPointerControl( dpy, &mouse_accel_numerator, &mouse_accel_denominator, &mouse_threshold );
	XChangePointerControl( dpy, True, True, 1, 1, 0 );

	XWarpPointer( dpy, None, win, 0, 0, 0, 0, glConfig.vidWidth / 2, glConfig.vidHeight / 2 );

	XGrabKeyboard( dpy, win, False, GrabModeAsync, GrabModeAsync, CurrentTime );

	XSync( dpy, False );
}


static void uninstall_grabs()
{
	XChangePointerControl( dpy, True, True, mouse_accel_numerator, mouse_accel_denominator, mouse_threshold );

	XUngrabPointer( dpy, CurrentTime );
	XUngrabKeyboard( dpy, CurrentTime );

	XUndefineCursor( dpy, win );
}


// bk001206 - from Ryan's Fakk2
/**
 * XPending() actually performs a blocking read
 *  if no events available. From Fakk2, by way of
 *  Heretic2, by way of SDL, original idea GGI project.
 * The benefit of this approach over the quite
 *  badly behaved XAutoRepeatOn/Off is that you get
 *  focus handling for free, which is a major win
 *  with debug and windowed mode. It rests on the
 *  assumption that the X server will use the
 *  same timestamp on press/release event pairs
 *  for key repeats.
 */
static qboolean X11_PendingInput(void) {

  assert(dpy != NULL);

  // Flush the display connection
  //  and look to see if events are queued
  XFlush( dpy );
  if ( XEventsQueued( dpy, QueuedAlready) )
  {
    return qtrue;
  }

  // More drastic measures are required -- see if X is ready to talk
  {
    static struct timeval zero_time;
    int x11_fd;
    fd_set fdset;

    x11_fd = ConnectionNumber( dpy );
    FD_ZERO(&fdset);
    FD_SET(x11_fd, &fdset);
    if ( select(x11_fd+1, &fdset, NULL, NULL, &zero_time) == 1 )
    {
      return(XPending(dpy));
    }
  }

  // Oh well, nothing is ready ..
  return qfalse;
}


// X sends Release/Press pairs for auto-repeat even though the key hasn't actually been released

static qboolean KeyRepeat( const XEvent* event )
{
	if (!X11_PendingInput())
		return qfalse;

	XEvent peek;
	XPeekEvent( dpy, &peek );

	if ( (peek.type == KeyPress) && (peek.xkey.keycode == event->xkey.keycode) && (peek.xkey.time == event->xkey.time) ) {
		XNextEvent( dpy, &peek );   // discard the CURRENT event, which is the RELEASE
		return qtrue;
	}

	return qfalse;
}


static void HandleEvents()
{
	XEvent event;
	int key;
	const char* p;

	if (!dpy)
		return;

	while (XPending(dpy))
	{
		XNextEvent(dpy, &event);

		if (mouse && mouse->ProcessEvent(event))
			continue;

		switch (event.type)
		{
			case KeyPress:
				p = TranslateKey( &event.xkey, &key );
				if (key) {
					Sys_QueEvent( 0, SE_KEY, key, qtrue, 0, NULL );
				}
				if (p) {
					while (*p) {
						Sys_QueEvent( 0, SE_CHAR, *p++, 0, 0, NULL );
					}
				}
			break;

			case KeyRelease:
				if (!cls.keyCatchers && KeyRepeat(&event)) {
					continue;
				}
				TranslateKey( &event.xkey, &key );
				if (key) {
					Sys_QueEvent( 0, SE_KEY, key, qfalse, 0, NULL );
				}
			break;
		}
	}

}



static void IN_ActivateMouse()
{
	if (!mouse_avail || !dpy || !win)
		return;

	if (!mouse_active) {
		if (!in_nograb->value)
			install_grabs();
		mouse_active = qtrue;
	}

	mouse->Activate( qtrue );
}


static void IN_DeactivateMouse()
{
	if (!mouse_avail || !dpy || !win)
		return;

	if (mouse_active) {
		if (!in_nograb->value)
			uninstall_grabs();
		mouse_active = qfalse;
	}
}


///////////////////////////////////////////////////////////////








#ifdef _XF86DGA_H_
#define HAVE_XF86DGA
#endif

#define	WINDOW_CLASS_NAME "CNQ3"

// OpenGL driver
#define OPENGL_DRIVER_NAME	"libGL.so.1"

typedef enum
{
  RSERR_OK,

  RSERR_INVALID_FULLSCREEN,
  RSERR_INVALID_MODE,

  RSERR_UNKNOWN
} rserr_t;

glwstate_t glw_state;

static GLXContext ctx = NULL;



static cvar_t *in_mouse;

// bk001130 - from cvs1.17 (mkv), but not static
cvar_t   *in_joystick      = NULL;
cvar_t   *in_joystickDebug = NULL;
cvar_t   *joy_threshold    = NULL;

cvar_t	*r_fullscreen;

qboolean vidmode_ext = qfalse;
#ifdef HAVE_XF86DGA
static int vidmode_MajorVersion = 0, vidmode_MinorVersion = 0; // major and minor of XF86VidExtensions

// gamma value of the X display before we start playing with it
static XF86VidModeGamma vidmode_InitialGamma = { -1,-1,-1 };  // drakkar - initialized to nonvalid values
#endif /* HAVE_XF86DGA */

#ifdef HAVE_XF86DGA
static XF86VidModeModeInfo **vidmodes = NULL;  // drakkar - initialized to NULL
#endif /* HAVE_XF86DGA */
static int num_vidmodes;
static qboolean vidmode_active = qfalse;

static int scrnum;


/*
** GLimp_SetGamma
**
** This routine should only be called if glConfig.deviceSupportsGamma is TRUE
*/
void GLimp_SetGamma( unsigned char red[256], unsigned char green[256], unsigned char blue[256] )
{
  // NOTE TTimo we get the gamma value from cvar, because we can't work with the s_gammatable
  //   the API wasn't changed to avoid breaking other OSes
#ifdef HAVE_XF86DGA
  float g = Cvar_Get("r_gamma", "1.0", 0)->value;
  XF86VidModeGamma gamma;
  // drakkar - do not set gamma if window is not focused
  Window focus;
  int state;
  XGetInputFocus( dpy, &focus, &state );
  if( win != focus ) return;
  // !drakkar
  assert(glConfig.deviceSupportsGamma);
  gamma.red = g;
  gamma.green = g;
  gamma.blue = g;
  XF86VidModeSetGamma(dpy, scrnum, &gamma);
#endif /* HAVE_XF86DGA */
}

/*
** GLimp_Shutdown
**
** This routine does all OS specific shutdown procedures for the OpenGL
** subsystem.  Under OpenGL this means NULLing out the current DC and
** HGLRC, deleting the rendering context, and releasing the DC acquired
** for the window.  The state structure is also nulled out.
**
*/
void GLimp_Shutdown( void )
{
	if (!ctx || !dpy)
		return;

	ri.Printf( PRINT_DEVELOPER, "Shutting down OpenGL subsystem\n" );

//  IN_DeactivateMouse();

	qglXDestroyContext(dpy, ctx);
	ctx = NULL;

	if (win) {
		XDestroyWindow(dpy, win);
		win = 0;
	}

#ifdef HAVE_XF86DGA
    if (vidmode_active)
      XF86VidModeSwitchToMode(dpy, scrnum, vidmodes[0]);
    if (glConfig.deviceSupportsGamma)
    {
      XF86VidModeSetGamma(dpy, scrnum, &vidmode_InitialGamma);
    }
#endif /* HAVE_XF86DGA */

	// NOTE TTimo opening/closing the display should be necessary only once per run
	//   but it seems QGL_Shutdown gets called in a lot of occasion
	//   in some cases, this XCloseDisplay is known to raise some X errors
	//   ( https://zerowing.idsoftware.com/bugzilla/show_bug.cgi?id=33 )
	XCloseDisplay(dpy);
	dpy = NULL;

  vidmode_active = qfalse;
  // drakkar
#ifdef HAVE_XF86DGA
  if( vidmodes ) XFree( vidmodes );
  vidmodes = NULL;
#endif
  // !drakkar

	QGL_Shutdown();

	memset( &glConfig, 0, sizeof( glConfig ) );
	memset( &glState, 0, sizeof( glState ) );
}


static rserr_t GLW_SetMode( qboolean fullscreen )
{
  int attrib[] = {
    GLX_RGBA,         // 0
    GLX_RED_SIZE, 4,      // 1, 2
    GLX_GREEN_SIZE, 4,      // 3, 4
    GLX_BLUE_SIZE, 4,     // 5, 6
    GLX_DOUBLEBUFFER,     // 7
    GLX_DEPTH_SIZE, 1,      // 8, 9
    GLX_STENCIL_SIZE, 1,    // 10, 11
    None
  };
  // these match in the array
#define ATTR_RED_IDX 2
#define ATTR_GREEN_IDX 4
#define ATTR_BLUE_IDX 6
#define ATTR_DEPTH_IDX 9
#define ATTR_STENCIL_IDX 11
  XVisualInfo *visinfo;
  XSetWindowAttributes attr;
  XSizeHints sizehints;
  unsigned long mask;
  int colorbits, depthbits, stencilbits;
  int tcolorbits, tdepthbits, tstencilbits;
  int actualWidth, actualHeight;
  int i;

	ri.Printf( PRINT_ALL, "Initializing OpenGL\n" );

	if (!(dpy = XOpenDisplay(NULL))) {
		ri.Error( ERR_FATAL, "GLW_SetMode - Couldn't open the X display" );
		return RSERR_INVALID_MODE;
	}

	scrnum = DefaultScreen(dpy);
	Window root = RootWindow(dpy, scrnum);

	if ( !R_GetModeInfo( &glConfig.vidWidth, &glConfig.vidHeight, &glConfig.windowAspect ) ) {
		glConfig.vidWidth = XDisplayWidth(dpy, scrnum);
		glConfig.vidHeight = XDisplayHeight(dpy, scrnum);
		glConfig.windowAspect = (float)glConfig.vidWidth / glConfig.vidHeight;
		vidmode_active = qtrue;
		fullscreen = qfalse;
	}
	ri.Printf( PRINT_DEVELOPER, "...setting mode %dx%d %s\n", glConfig.vidWidth, glConfig.vidHeight, fullscreen ? "FS" : "W" );



  actualWidth = glConfig.vidWidth;
  actualHeight = glConfig.vidHeight;

  // Get video mode list
#ifdef HAVE_XF86DGA
  if (!XF86VidModeQueryVersion(dpy, &vidmode_MajorVersion, &vidmode_MinorVersion))
  {
#endif /* HAVE_XF86DGA */
    vidmode_ext = qfalse;
#ifdef HAVE_XF86DGA
  } else
  {
    ri.Printf(PRINT_ALL, "Using XFree86-VidModeExtension Version %d.%d\n",
              vidmode_MajorVersion, vidmode_MinorVersion);
    vidmode_ext = qtrue;
  }
#endif /* HAVE_XF86DGA */


#ifdef HAVE_XF86DGA
  if (vidmode_ext)
  {
    int best_fit, best_dist, dist, x, y;

    XF86VidModeGetAllModeLines(dpy, scrnum, &num_vidmodes, &vidmodes);

    // Are we going fullscreen?  If so, let's change video mode
    if (fullscreen)
    {
      best_dist = 9999999;
      best_fit = -1;

      for (i = 0; i < num_vidmodes; i++)
      {
        if (glConfig.vidWidth > vidmodes[i]->hdisplay ||
            glConfig.vidHeight > vidmodes[i]->vdisplay)
          continue;

        x = glConfig.vidWidth - vidmodes[i]->hdisplay;
        y = glConfig.vidHeight - vidmodes[i]->vdisplay;
        dist = (x * x) + (y * y);
        if (dist < best_dist)
        {
          best_dist = dist;
          best_fit = i;
        }
      }

      if (best_fit != -1)
      {
        actualWidth = vidmodes[best_fit]->hdisplay;
        actualHeight = vidmodes[best_fit]->vdisplay;

        // change to the mode
        XF86VidModeSwitchToMode(dpy, scrnum, vidmodes[best_fit]);
        vidmode_active = qtrue;

        // Move the viewport to top left
        XF86VidModeSetViewPort(dpy, scrnum, 0, 0);

        ri.Printf(PRINT_ALL, "XFree86-VidModeExtension Activated at %dx%d\n",
                  actualWidth, actualHeight);

      } else
      {
        fullscreen = 0;
        ri.Printf(PRINT_ALL, "XFree86-VidModeExtension: No acceptable modes found\n");
      }
    } else
    {
      ri.Printf(PRINT_ALL, "XFree86-VidModeExtension:  Ignored on non-fullscreen\n");
    }
  }
#endif /* HAVE_XF86DGA */


  if (!r_colorbits->value)
    colorbits = 24;
  else
    colorbits = r_colorbits->value;

  if (!r_depthbits->value)
    depthbits = 24;
  else
    depthbits = r_depthbits->value;
  stencilbits = r_stencilbits->value;

  for (i = 0; i < 16; i++)
  {
    // 0 - default
    // 1 - minus colorbits
    // 2 - minus depthbits
    // 3 - minus stencil
    if ((i % 4) == 0 && i)
    {
      // one pass, reduce
      switch (i / 4)
      {
      case 2 :
        if (colorbits == 24)
          colorbits = 16;
        break;
      case 1 :
        if (depthbits == 24)
          depthbits = 16;
        else if (depthbits == 16)
          depthbits = 8;
      case 3 :
        if (stencilbits == 24)
          stencilbits = 16;
        else if (stencilbits == 16)
          stencilbits = 8;
      }
    }

    tcolorbits = colorbits;
    tdepthbits = depthbits;
    tstencilbits = stencilbits;

    if ((i % 4) == 3)
    { // reduce colorbits
      if (tcolorbits == 24)
        tcolorbits = 16;
    }

    if ((i % 4) == 2)
    { // reduce depthbits
      if (tdepthbits == 24)
        tdepthbits = 16;
      else if (tdepthbits == 16)
        tdepthbits = 8;
    }

    if ((i % 4) == 1)
    { // reduce stencilbits
      if (tstencilbits == 24)
        tstencilbits = 16;
      else if (tstencilbits == 16)
        tstencilbits = 8;
      else
        tstencilbits = 0;
    }

    if (tcolorbits == 24)
    {
      attrib[ATTR_RED_IDX] = 8;
      attrib[ATTR_GREEN_IDX] = 8;
      attrib[ATTR_BLUE_IDX] = 8;
    } else
    {
      // must be 16 bit
      attrib[ATTR_RED_IDX] = 4;
      attrib[ATTR_GREEN_IDX] = 4;
      attrib[ATTR_BLUE_IDX] = 4;
    }

    attrib[ATTR_DEPTH_IDX] = tdepthbits; // default to 24 depth
    attrib[ATTR_STENCIL_IDX] = tstencilbits;

    visinfo = qglXChooseVisual(dpy, scrnum, attrib);
    if (!visinfo)
    {
      continue;
    }

    ri.Printf( PRINT_ALL, "Using %d/%d/%d Color bits, %d depth, %d stencil display.\n",
               attrib[ATTR_RED_IDX], attrib[ATTR_GREEN_IDX], attrib[ATTR_BLUE_IDX],
               attrib[ATTR_DEPTH_IDX], attrib[ATTR_STENCIL_IDX]);

    glConfig.colorBits = tcolorbits;
    glConfig.depthBits = tdepthbits;
    glConfig.stencilBits = tstencilbits;
    break;
  }

  if (!visinfo)
  {
    ri.Printf( PRINT_ALL, "Couldn't get a visual\n" );
    return RSERR_INVALID_MODE;
  }

  /* window attributes */
  attr.background_pixel = BlackPixel(dpy, scrnum);
  attr.border_pixel = 0;
  attr.colormap = XCreateColormap(dpy, root, visinfo->visual, AllocNone);
  attr.event_mask = X_MASK;
  if (vidmode_active)
  {
    mask = CWBackPixel | CWColormap | CWSaveUnder | CWBackingStore |
           CWEventMask | CWOverrideRedirect;
    attr.override_redirect = True;
    attr.backing_store = NotUseful;
    attr.save_under = False;
  } else
    mask = CWBackPixel | CWBorderPixel | CWColormap | CWEventMask;

  win = XCreateWindow(dpy, root, 0, 0,
                      actualWidth, actualHeight,
                      0, visinfo->depth, InputOutput,
                      visinfo->visual, mask, &attr);
  XStoreName( dpy, win, WINDOW_CLASS_NAME );

  /* GH: Don't let the window be resized */
  sizehints.flags = PMinSize | PMaxSize;
  sizehints.min_width = sizehints.max_width = actualWidth;
  sizehints.min_height = sizehints.max_height = actualHeight;

  XSetWMNormalHints( dpy, win, &sizehints );

  XMapWindow( dpy, win );

  if (vidmode_active)
    XMoveWindow(dpy, win, 0, 0);

  XFlush(dpy);
  XSync(dpy,False); // bk001130 - from cvs1.17 (mkv)
  ctx = qglXCreateContext(dpy, visinfo, NULL, True);
  XSync(dpy,False); // bk001130 - from cvs1.17 (mkv)

  XFree( visinfo );

  qglXMakeCurrent(dpy, win, ctx);

  ri.Printf( PRINT_ALL, "GL_RENDERER: %s\n", (const char*)qglGetString( GL_RENDERER ) );

  return RSERR_OK;
}



static qboolean GLW_StartDriverAndSetMode( qboolean fullscreen )
{
  rserr_t err;

	if (fullscreen && in_nograb->value)
	{
		ri.Printf( PRINT_ALL, "Fullscreen not allowed with in_nograb 1\n" );
		ri.Cvar_Set( "r_fullscreen", "0" );
		r_fullscreen->modified = qfalse;
		fullscreen = qfalse;
	}

  err = GLW_SetMode( fullscreen );

  switch ( err )
  {
  case RSERR_INVALID_FULLSCREEN:
    ri.Printf( PRINT_ALL, "...WARNING: fullscreen unavailable in this mode\n" );
    return qfalse;
  case RSERR_INVALID_MODE:
    ri.Printf( PRINT_ALL, "...WARNING: could not set the given mode\n" );
    return qfalse;
  default:
    break;
  }
  return qtrue;
}


static void GLW_InitExtensions()
{
	ri.Printf( PRINT_ALL, "Initializing OpenGL extensions\n" );

	int maxAnisotropy = 0;
	if ( strstr( glConfig.extensions_string, "GL_EXT_texture_filter_anisotropic" ) )
	{
		if ( r_ext_max_anisotropy->integer > 1 )
		{
			qglGetIntegerv( GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy );
			if ( maxAnisotropy <= 0 ) {
				ri.Printf( PRINT_DEVELOPER, "...GL_EXT_texture_filter_anisotropic not properly supported!\n" );
				maxAnisotropy = 0;
			}
			else
			{
				ri.Printf( PRINT_DEVELOPER, "...using GL_EXT_texture_filter_anisotropic (max: %i)\n", maxAnisotropy );
			}
		}
		else
		{
			ri.Printf( PRINT_DEVELOPER, "...ignoring GL_EXT_texture_filter_anisotropic\n" );
		}
	}
	else
	{
		ri.Printf( PRINT_DEVELOPER, "...GL_EXT_texture_filter_anisotropic not found\n" );
	}
	Cvar_Set( "r_ext_max_anisotropy", va("%i", maxAnisotropy) );
}


static void GLW_InitGamma()
{
  /* Minimum extension version required */
  #define GAMMA_MINMAJOR 2
  #define GAMMA_MINMINOR 0

  glConfig.deviceSupportsGamma = qfalse;

#ifdef HAVE_XF86DGA
  if (vidmode_ext)
  {
    if (vidmode_MajorVersion < GAMMA_MINMAJOR ||
        (vidmode_MajorVersion == GAMMA_MINMAJOR && vidmode_MinorVersion < GAMMA_MINMINOR)) {
      ri.Printf( PRINT_ALL, "XF86 Gamma extension not supported in this version\n");
      return;
    }
	if( vidmode_InitialGamma.red < 0 ) // drakkar - get initial gamma only one time
		XF86VidModeGetGamma(dpy, scrnum, &vidmode_InitialGamma);
    ri.Printf( PRINT_ALL, "XF86 Gamma extension initialized\n");
    glConfig.deviceSupportsGamma = qtrue;
  }
#endif /* HAVE_XF86DGA */
}

/*
** GLW_LoadOpenGL
**
** GLimp_win.c internal function that that attempts to load and use
** a specific OpenGL DLL.
*/
static qboolean GLW_LoadOpenGL( void )
{
  qboolean fullscreen;

  ri.Printf( PRINT_ALL, "...loading %s: ", OPENGL_DRIVER_NAME );

  // load the QGL layer
  if ( QGL_Init( OPENGL_DRIVER_NAME ) )
  {
    fullscreen = r_fullscreen->integer;

    // create the window and set up the context
    if ( !GLW_StartDriverAndSetMode( fullscreen ) )
    {
      goto fail;
    }

    return qtrue;
  } else
  {
    ri.Printf( PRINT_ALL, "failed\n" );
  }
  fail:

  QGL_Shutdown();

  return qfalse;
}

/*
** XErrorHandler
**   the default X error handler exits the application
**   I found out that on some hosts some operations would raise X errors (GLXUnsupportedPrivateRequest)
**   but those don't seem to be fatal .. so the default would be to just ignore them
**   our implementation mimics the default handler behaviour (not completely cause I'm lazy)
*/
int qXErrorHandler(Display *dpy, XErrorEvent *ev)
{
  static char buf[1024];
  XGetErrorText(dpy, ev->error_code, buf, 1024);
  ri.Printf( PRINT_ALL, "X Error of failed request: %s\n", buf);
  ri.Printf( PRINT_ALL, "  Major opcode of failed request: %d\n", ev->request_code, buf);
  ri.Printf( PRINT_ALL, "  Minor opcode of failed request: %d\n", ev->minor_code);
  ri.Printf( PRINT_ALL, "  Serial number of failed request: %d\n", ev->serial);
  return 0;
}

void QGL_SwapInterval( Display *dpy, Window win, int interval );

/*
** GLimp_Init
**
** This routine is responsible for initializing the OS specific portions
** of OpenGL.
*/
void GLimp_Init( void )
{
  qboolean attemptedlibGL = qfalse;
  qboolean success = qfalse;

  cvar_t *lastValidRenderer = ri.Cvar_Get( "r_lastValidRenderer", "(uninitialized)", CVAR_ARCHIVE );

  // guarded, as this is only relevant to SMP renderer thread
#ifdef SMP
  if (!XInitThreads())
  {
    Com_Printf("GLimp_Init() - XInitThreads() failed, disabling r_smp\n");
    ri.Cvar_Set( "r_smp", "0" );
  }
#endif

  InitSig();

  // set up our custom error handler for X failures
  XSetErrorHandler(&qXErrorHandler);

	in_nograb = Cvar_Get( "in_nograb", "0", 0 );

  // load appropriate DLL and initialize subsystem
  if (!GLW_LoadOpenGL() )
    ri.Error( ERR_FATAL, "GLimp_Init()->GLW_LoadOpenGL() - could not load OpenGL subsystem (using '%s')\n", OPENGL_DRIVER_NAME );

  // get our config strings
  Q_strncpyz( glConfig.vendor_string, (char *)qglGetString (GL_VENDOR), sizeof( glConfig.vendor_string ) );
  Q_strncpyz( glConfig.renderer_string, (char *)qglGetString (GL_RENDERER), sizeof( glConfig.renderer_string ) );
  if (*glConfig.renderer_string && glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] == '\n')
    glConfig.renderer_string[strlen(glConfig.renderer_string) - 1] = 0;
  Q_strncpyz( glConfig.version_string, (char *)qglGetString (GL_VERSION), sizeof( glConfig.version_string ) );
  Q_strncpyz( glConfig.extensions_string, (char *)qglGetString (GL_EXTENSIONS), sizeof( glConfig.extensions_string ) );

  ri.Cvar_Set( "r_lastValidRenderer", glConfig.renderer_string );

  // initialize extensions
  GLW_InitExtensions();
  GLW_InitARB(); // loads the ARB extensions
  GLW_InitGamma();
  QGL_InitARB(); // compiles the shaders etc
  QGL_SwapInterval( dpy, win, r_swapInterval->integer );

  InitSig(); // not clear why this is at begin & end of function

	IN_Init();
}


/*
** GLimp_EndFrame
**
** Responsible for doing a swapbuffers and possibly for other stuff
** as yet to be determined.  Probably better not to make this a GLimp
** function and instead do a call to GLimp_SwapBuffers.
*/
void GLimp_EndFrame (void)
{
  // update the swap interval
  if ( r_swapInterval->modified )
  {
    r_swapInterval->modified = qfalse;
    QGL_SwapInterval( dpy, win, r_swapInterval->integer );
  }

  // don't flip if drawing to front buffer
  if ( Q_stricmp( r_drawBuffer->string, "GL_FRONT" ) != 0 )
  {
    qglXSwapBuffers(dpy, win);
  }
}


#ifdef SMP
/*
===========================================================

SMP acceleration

===========================================================
*/

static pthread_mutex_t	smpMutex = PTHREAD_MUTEX_INITIALIZER;

static pthread_cond_t		renderCommandsEvent = PTHREAD_COND_INITIALIZER;
static pthread_cond_t		renderCompletedEvent = PTHREAD_COND_INITIALIZER;

static void (*glimpRenderThread)( void );

static void *GLimp_RenderThreadWrapper( void *arg )
{
	Com_Printf( "Render thread starting\n" );

  glimpRenderThread();

	qglXMakeCurrent( dpy, None, NULL );

	Com_Printf( "Render thread terminating\n" );

	return arg;
}

qboolean GLimp_SpawnRenderThread( void (*function)( void ) )
{
	pthread_t renderThread;
	int ret;

	pthread_mutex_init( &smpMutex, NULL );

	pthread_cond_init( &renderCommandsEvent, NULL );
	pthread_cond_init( &renderCompletedEvent, NULL );

  glimpRenderThread = function;

	ret = pthread_create( &renderThread,
						  NULL,			// attributes
						  GLimp_RenderThreadWrapper,
						  NULL );		// argument
	if ( ret ) {
		ri.Printf( PRINT_ALL, "pthread_create returned %d: %s", ret, strerror( ret ) );
    return qfalse;
	} else {
		ret = pthread_detach( renderThread );
		if ( ret ) {
			ri.Printf( PRINT_ALL, "pthread_detach returned %d: %s", ret, strerror( ret ) );
		}
  }

  return qtrue;
}

static volatile void    *smpData = NULL;
static volatile qboolean smpDataReady;

void *GLimp_RendererSleep( void )
{
	void  *data;

	qglXMakeCurrent( dpy, None, NULL );

	pthread_mutex_lock( &smpMutex );
	{
		smpData = NULL;
		smpDataReady = qfalse;

		// after this, the front end can exit GLimp_FrontEndSleep
		pthread_cond_signal( &renderCompletedEvent );

		while ( !smpDataReady ) {
			pthread_cond_wait( &renderCommandsEvent, &smpMutex );
		}

		data = (void *)smpData;
	}
	pthread_mutex_unlock( &smpMutex );

	qglXMakeCurrent( dpy, win, ctx );

  return data;
}

void GLimp_FrontEndSleep( void )
{
	pthread_mutex_lock( &smpMutex );
	{
		while ( smpData ) {
			pthread_cond_wait( &renderCompletedEvent, &smpMutex );
		}
	}
	pthread_mutex_unlock( &smpMutex );

	qglXMakeCurrent( dpy, win, ctx );
}

void GLimp_WakeRenderer( void *data )
{
	qglXMakeCurrent( dpy, None, NULL );

	pthread_mutex_lock( &smpMutex );
	{
		assert( smpData == NULL );
		smpData = data;
		smpDataReady = qtrue;

		// after this, the renderer can continue through GLimp_RendererSleep
		pthread_cond_signal( &renderCommandsEvent );
	}
	pthread_mutex_unlock( &smpMutex );
}

#else

void GLimp_RenderThreadWrapper( void *stub ) {}
qboolean GLimp_SpawnRenderThread( void (*function)( void ) ) {
	ri.Printf( PRINT_WARNING, "ERROR: SMP support was disabled at compile time\n");
  return qfalse;
}
void *GLimp_RendererSleep( void ) {
  return NULL;
}
void GLimp_FrontEndSleep( void ) {}
void GLimp_WakeRenderer( void *data ) {}

#endif


static void IN_StartupMouse()
{
	assert( !mouse );
	mouse = 0;

	cvar_t* in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE|CVAR_LATCH );
	in_mouse->modified = qfalse;

	if (!in_mouse->integer) {
		Com_Printf( "Mouse not active.\n" );
		return;
	}

	if (in_mouse->integer == 1) {
		if (rawmouse.Init()) {
			mouse = &rawmouse;
			Com_Printf( "Using XInput2\n" );
			return;
		}
		Com_Printf( "XInput2 mouse initialization failed\n" );
	}

	mouse = &xmouse;
	mouse->Init();
	Com_Printf( "Using XWindows mouse input\n" );
}






void IN_Init()
{
	QSUBSYSTEM_INIT_START( "Input" );
	//IN_InitKeyboard();
	IN_StartupMouse();


  in_mouse = Cvar_Get( "in_mouse", "1", CVAR_ARCHIVE|CVAR_LATCH );

  // bk001130 - from cvs.17 (mkv), joystick variables
  in_joystick = Cvar_Get ("in_joystick", "0", CVAR_ARCHIVE|CVAR_LATCH);
  // bk001130 - changed this to match win32
  in_joystickDebug = Cvar_Get ("in_debugjoystick", "0", CVAR_TEMP);
  joy_threshold = Cvar_Get ("joy_threshold", "0.15", CVAR_ARCHIVE); // FIXME: in_joythreshold

// fix this: it's crap AND wrong: the mouse is what decides if the mouse is available or not
  if (in_mouse->value)
    mouse_avail = qtrue;
  else
    mouse_avail = qfalse;

	IN_StartupJoystick();

	QSUBSYSTEM_INIT_DONE( "Input" );
}


void IN_Shutdown(void)
{
  mouse_avail = qfalse;

	//IN_Activate( qfalse );

	if (mouse) {
		mouse->Shutdown();
		mouse = 0;
	}
}


void IN_Frame (void) {

  // bk001130 - from cvs 1.17 (mkv)
  IN_JoyMove(); // FIXME: disable if on desktop?

  if ( cls.keyCatchers & KEYCATCH_CONSOLE )
  {
    // temporarily deactivate if not in the game and
    // running on the desktop
    if ( Cvar_VariableValue ("r_fullscreen") == 0 )
    {
      IN_DeactivateMouse ();
      return;
    }
  }

  IN_ActivateMouse();

	if (!mouse)
		return;

	int mx, my;
	mouse->Read( &mx, &my );

	if ( !mx && !my )
		return;

	Sys_QueEvent( 0, SE_MOUSE, mx, my, 0, NULL );
}


void IN_Activate(void)
{
}

// bk001130 - cvs1.17 joystick code (mkv) was here, no linux_joystick.c

void Sys_SendKeyEvents (void) {
  // XEvent event; // bk001204 - unused

  if (!dpy)
    return;
  HandleEvents();
}


// bk010216 - added stubs for non-Linux UNIXes here
// FIXME - use NO_JOYSTICK or something else generic

#if (defined( __FreeBSD__ ) || defined( __sun)) // rb010123
void IN_StartupJoystick( void ) {}
void IN_JoyMove( void ) {}
#endif

#endif  // !USE_SDL_VIDEO
