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
#ifndef _WIN32
#  error You should not be including this file on this platform
#endif

#ifndef __GLW_WIN_H__
#define __GLW_WIN_H__

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct
{
	HDC     hDC;			// handle to device context
	HGLRC   hGLRC;			// handle to GL rendering context
	HINSTANCE hinstOpenGL;	// HINSTANCE for the OpenGL library

	int desktopWidth, desktopHeight, desktopBPP;

	qbool cdsFullscreen;
	qbool pixelFormatSet;
	int nPendingPF;

	qbool gammaRampSet; // qtrue if our custom ramp is active
} glwstate_t;

extern glwstate_t glw_state;

#if defined(__cplusplus)
};
#endif

#endif
