#ifndef _FONT_HANDLER_
#define _FONT_HANDLER_

/*
	Font handler
	by Loren Petrich,
	December 17, 2000
	
	This is for specifying and working with text fonts;
	specifying them, changing them with XML, and creating a font brush
	for OpenGL rendering

Jan 5, 2001 (Loren Petrich):
	Added a "file" field, for containing font filenames
*/


#include "cseries.h"
#include "XML_ElementParser.h"


struct FontSpecifier
{
	enum {
		// Text styles (additive); these are MacOS style-flag values
		Normal = 0,
		Bold = 1,
		Italic = 2,
		Underline = 4,
		// How many characters
		NameSetLen = 64
	};
	
	// Parameters:
	
	// Name in HTML-fontname style "font1, font2, font3";
	// use the first of these fonts that are present,
	// otherwise use some sensible default
	char NameSet[NameSetLen];
	short Size;
	short Style;
	
	// For SDL font support: a font filename
	char File[NameSetLen];
	
	// Derived quantities: sync with parameters by calling Update()
	short Height;			// How tall is it?
	short LineSpacing;		// From same positions in each line
	
	// MacOS-specific:
	short ID;
	
	// Initialize: call this before calling anything else;
	// this is from not having a proper constructor for this object.
	void Init();
	
	// Do the updating: must be called before using the font; however, it is called by Init(),
	// and it will be called by the XML parser if it updates the parameters
	void Update();
	
	// Use this font (MacOS-specific); has this form because MacOS Quickdraw has a global font value
	// for each GrafPort (draw context)
	void Use();
	
	// Get text width for text that must be centered (map title)
	int TextWidth(char *Text);
	
#ifdef HAVE_OPENGL	
	// Reset the OpenGL fonts; its arg indicates whether this is for starting an OpenGL session
	// (this is to avoid texture and display-list memory leaks and other such things)
	void OGL_Reset(bool IsStarting);
	
	// Renders a C-style string in OpenGL.
	// assumes screen coordinates and that the left baseline point is at (0,0).
	// Alters the modelview matrix so that the next characters will be drawn at the proper place.
	// One can surround it with glPushMatrix() and glPopMatrix() to remember the original.
	void OGL_Render(char *Text);
#endif
	
	// Equality and assignment operators
	bool operator==(FontSpecifier& F);
	bool operator!=(FontSpecifier& F)
		{return (!((*this) == F));}
	FontSpecifier& operator=(FontSpecifier& F);
	
	// Search functions for names; call these in alternation on a pointer to the current name,
	// which is initialized to the name-string pointer above
	
	// Given a pointer to somewhere in a name set, returns the pointer
	// to the start of the next name, or NULL if it did not find any
	static char *FindNextName(char *NamePtr);
	
	// Given a pointer to the beginning of a name, finds the pointer to the character
	// just after the end of that name
	static char *FindNameEnd(char *NamePtr);
	
	// Not sure what kind of explicit constructor would be consistent with the way
	// that fonts' initial values are specified, as {nameset-string, size, style}.
	// Also, private members are inconsistent with that sort of initialization.

#ifdef HAVE_OPENGL
	// Stuff for OpenGL font rendering: the font texture and a display list for font rendering;
	// if OGL_Texture is NULL, then there is no OpenGL font texture to render.
	uint8 *OGL_Texture;
	short TxtrWidth, TxtrHeight;
	int GetTxtrSize() {return int(TxtrWidth)*int(TxtrHeight);}
	uint32 TxtrID;
	uint32 DispList;
#endif
};


// Returns a parser for the fonts;
// several elements may have colors, so this ought to be callable several times.
XML_ElementParser *Font_GetParser();

// This sets the list of fonts to be read into.
// Its args are the pointer to that list and the number of fonts in it.
// If that number is <= 0, then the color value is assumed to be non-indexed,
// and no "index" attribute will be searched for.
void Font_SetArray(FontSpecifier *FontList, int NumColors = 0);

#endif
