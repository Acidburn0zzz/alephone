/* csstrings.h

	Copyright (C) 1991-2001 and beyond by Bungie Studios, Inc.
	and the "Aleph One" developers.
 
	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU General Public License for more details.

	This license is contained in the file "COPYING",
	which is included with this source code; it is available online at
	http://www.gnu.org/licenses/gpl.html

// LP: not sure who originally wrote these cseries files: Bo Lindbergh?

// LP (Aug 28, 2001): Added "fdprintf" -- used like dprintf, but writes to file AlephOneDebugLog.txt

    Sept-Nov 2001 (Woody Zenfell): added new routines; gave pstrcpy a "const" parameter qualifier.
*/

#ifndef _CSERIES_STRINGS_
#define _CSERIES_STRINGS_

#if defined(__GNUC__)
#define PRINTF_STYLE_ARGS(n,m) __attribute__((format(printf,n,m)))
#else
#define PRINTF_STYLE_ARGS(n,m)
#endif

extern char temporary[256];
#define ptemporary (*(Str255 *)temporary)

extern short countstr(
	short resid);

extern unsigned char *getpstr(
	unsigned char *string,
	short resid,
	short item);

extern char *getcstr(
	char *string,
	short resid,
	short item);

// ZZZ: changed to 'const' on src parameter
extern unsigned char *pstrcpy(
	unsigned char *dst,
	const unsigned char *src);

// START ZZZ additions
extern unsigned char* pstrncpy(
	unsigned char* dest,
	const unsigned char* source,
	size_t total_byte_count);

extern unsigned char* pstrdup(
	const unsigned char* source);

extern unsigned char* a1_c2pstr(
	char* inoutStringBuffer);

extern char* a1_p2cstr(
	unsigned char* inoutStringBuffer);
// END ZZZ additions

extern char *csprintf(
	char *buffer,
	const char *format,
	...) PRINTF_STYLE_ARGS(2,3);

extern unsigned char *psprintf(
	unsigned char *buffer,
	const char *format,
	...) PRINTF_STYLE_ARGS(2,3);

extern void dprintf(
	const char *format,
	...) PRINTF_STYLE_ARGS(1,2);

extern void fdprintf(
	const char *format,
	...) PRINTF_STYLE_ARGS(1,2);

#endif
