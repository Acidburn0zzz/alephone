/*

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
	March 18, 2000 (Loren Petrich)
	
	Dialog box for setting up OpenGL options
	
	Several changes...
	
	May 27, 2000 (Loren Petrich)
	
	Added support for flat static effect
	
	Jun 11, 2000 (Loren Petrich):
	Use a class created for the checkboxes.
	Also added "see through liquids" option

Sep 9, 2000:

	Added checkbox for AppleGL texturing fix

Dec 17, 2000 (Loren Petrich):
	Eliminated fog parameters from the preferences;
	there is still a "fog present" switch, which is used to indicate
	whether fog will not be suppressed.

Jan 25, 2002 (Br'fin (Jeremy Parsons)):
	Added accessors for datafields now opaque in Carbon
	Proc pointers are now UPP

Feb 5, 2002 (Br'fin (Jeremy Parsons)):
	Shifted functions so we weren't trying to get a menu from a needlessly locked control handle
	Put OpenGL dialogs in sheets under Carbon

Feb 14, 2002 (Br'fin (Jeremy Parsons)):
	Made the Carbon sheets backgrounds transparent
*/

#include "cseries.h"
#include "shell.h"
#include "OGL_Setup.h"
#include "MacCheckbox.h"
#include <stdio.h>

enum
{
	OK_Item = 1,
	Cancel_Item = 2,
	
	OpenGL_Dialog = 2000,
	Walls_Item = 5,
	Landscape_Item = 6,
	Inhabitants_Item = 7,
	WeaponsInHand_Item = 8,
	ZBuffer_Item = 9,
	ColorVoid_Item = 10,
	ColorVoidSwatch_Item = 11,
	FlatColorLandscapes_Item = 12,
	
	// The first of 8 items, in order:
	// Day Ground, Day Sky, Night Ground, Night Sky, ...
	LandscapeSwatch_ItemBase = 17,
	
	TwoDimGraphics_Item = 25,
	FlatStaticEffect_Item = 26,
	Fader_Item = 27,
	LiquidSeeThru_Item = 28,
	Map_Item = 29,
	TextureFix_Item = 30,
	AllowFog_Item = 31,
	Model_Item = 37,
	
	ColorPicker_PromptStrings = 200,
	ColorVoid_String = 0,
	FlatColorLandscapes_StringBase = 1,
	FogColor_String = 9,
	
	OpenGL_Textures_Dialog = 2100,
	WhichOne_Item = 4,
	Near_Item = 5,
	Far_Item = 6,
	Resolution_Item = 7,
	ColorDepth_Item = 8,
	BasedOn_Item = 9,
	
	BasedOn_Menu = 12140
};


#ifdef USES_NIBS

const CFStringRef Window_Prefs_OpenGL = CFSTR("Prefs_OpenGL");
const CFStringRef Window_Prefs_OpenGL_Textures = CFSTR("Prefs_OpenGL_Textures");

// For doing checkboxes en masse
// Which checkbox, flag value
const int NumCheckboxes = 11;
const int CheckboxDispatch[NumCheckboxes][2] = {
	{ZBuffer_Item, OGL_Flag_ZBuffer},
	{ColorVoid_Item, OGL_Flag_VoidColor},
	{FlatColorLandscapes_Item, OGL_Flag_FlatLand},
	{AllowFog_Item, OGL_Flag_Fog},
	{Model_Item, OGL_Flag_3D_Models},
	
	{TwoDimGraphics_Item, OGL_Flag_2DGraphics},
	{FlatStaticEffect_Item, OGL_Flag_FlatStatic},
	{Fader_Item, OGL_Flag_Fader},
	{LiquidSeeThru_Item, OGL_Flag_LiqSeeThru},
	{Map_Item, OGL_Flag_Map},
	
	{TextureFix_Item, OGL_Flag_TextureFix}
};

struct TextureConfigDlgHandlerData
{
	OGL_Texture_Configure *TxtrConfigList;
	short WhichTexture;
	ControlRef NearCtrl, FarCtrl, ResCtrl, ColorCtrl;
};

static void TextureConfigDlgHandler(ParsedControl &Ctrl, void *Data)
{
	TextureConfigDlgHandlerData *HDPtr = (TextureConfigDlgHandlerData *)(Data);
	
	if (Ctrl.ID.id == BasedOn_Item)
	{
		// Where to copy from
		int WhichSource = GetControl32BitValue(Ctrl.Ctrl) - 1;
		
		// Don't copy from oneself!
		if (WhichSource != HDPtr->WhichTexture)
		{
			OGL_Texture_Configure &TxtrConfig = HDPtr->TxtrConfigList[WhichSource];
			
			SetControl32BitValue(HDPtr->NearCtrl, TxtrConfig.NearFilter+1);
		
			SetControl32BitValue(HDPtr->FarCtrl, TxtrConfig.FarFilter+1);
			
			SetControl32BitValue(HDPtr->ResCtrl, TxtrConfig.Resolution+1);
			
			SetControl32BitValue(HDPtr->ColorCtrl, TxtrConfig.ColorFormat+1);
		}
	}
}


// Texture-configuration dialog box
// True for OK, false for cancel
static bool TextureConfigureDialog(OGL_Texture_Configure *TxtrConfigList, short WhichTexture)
{
	OSStatus err;
	
	// Get the window
	AutoNibWindow Window(GUI_Nib,Window_Prefs_OpenGL_Textures);
	
	// Set up the dialog box
	OGL_Texture_Configure &TxtrConfig = TxtrConfigList[WhichTexture];

	ControlRef NearCtrl = GetCtrlFromWindow(Window(), 0, Near_Item);
	SetControl32BitValue(NearCtrl, TxtrConfig.NearFilter+1);

	ControlRef FarCtrl = GetCtrlFromWindow(Window(), 0, Far_Item);
	SetControl32BitValue(FarCtrl, TxtrConfig.FarFilter+1);

	ControlRef ResCtrl = GetCtrlFromWindow(Window(), 0, Resolution_Item);
	SetControl32BitValue(ResCtrl, TxtrConfig.Resolution+1);

	ControlRef ColorCtrl = GetCtrlFromWindow(Window(), 0, ColorDepth_Item);
	SetControl32BitValue(ColorCtrl, TxtrConfig.ColorFormat+1);

	ControlRef BasedOnCtrl = GetCtrlFromWindow(Window(), 0, BasedOn_Item);
	SetControl32BitValue(BasedOnCtrl, WhichTexture+1);

	ControlRef WhichOneCtrl = GetCtrlFromWindow(Window(), 0, WhichOne_Item);
	
	MenuRef BasedOnMenu = GetControlPopupMenuHandle(BasedOnCtrl);
	Str255 WhichTxtrLabel;
	GetMenuItemText(BasedOnMenu, WhichTexture+1, WhichTxtrLabel);
	
	SetStaticPascalText(WhichOneCtrl,WhichTxtrLabel);
	
	TextureConfigDlgHandlerData HandlerData;
	HandlerData.TxtrConfigList = TxtrConfigList;
	HandlerData.WhichTexture = WhichTexture;
	HandlerData.NearCtrl = NearCtrl;
	HandlerData.FarCtrl = FarCtrl;
	HandlerData.ResCtrl = ResCtrl;
	HandlerData.ColorCtrl = ColorCtrl;
	

	bool IsOK = RunModalDialog(Window(),true, TextureConfigDlgHandler, &HandlerData);
	
	if (IsOK)
	{
		TxtrConfig.NearFilter = GetControl32BitValue(NearCtrl) - 1;
		
		TxtrConfig.FarFilter = GetControl32BitValue(FarCtrl) - 1;
		
		TxtrConfig.Resolution = GetControl32BitValue(ResCtrl) - 1;
		
		TxtrConfig.ColorFormat = GetControl32BitValue(ColorCtrl) - 1;
	}
	
	return IsOK;
}



struct OGL_Dialog_Handler_Data
{
	RGBColor VoidColor, LscpColors[4][2];
	OGL_Texture_Configure TxtrConfigList[OGL_NUMBER_OF_TEXTURE_TYPES];
};


static void OGL_Dialog_Handler(ParsedControl &Ctrl, void *Data)
{
	OGL_Dialog_Handler_Data *HDPtr = (OGL_Dialog_Handler_Data *)(Data);
	
	if (Ctrl.ID.id == ColorVoidSwatch_Item)
	{
		RGBColor *ClrPtr = &HDPtr->VoidColor;
		getpstr(ptemporary, ColorPicker_PromptStrings, ColorVoid_String);
		PickControlColor(Ctrl.Ctrl, ClrPtr, ptemporary);
	}
	else if (
		(Ctrl.ID.id >= LandscapeSwatch_ItemBase) &&
		(Ctrl.ID.id < (LandscapeSwatch_ItemBase+8))
		)
	{
		int ix = Ctrl.ID.id - LandscapeSwatch_ItemBase;
		int il = ix/2;
		int ie = ix - 2*il;
		
		RGBColor *ClrPtr = &HDPtr->LscpColors[il][ie];
		getpstr(ptemporary, ColorPicker_PromptStrings, FlatColorLandscapes_StringBase+ix);
		PickControlColor(Ctrl.Ctrl, ClrPtr, ptemporary);
	}
	else
	{
		switch(Ctrl.ID.id)
		{
		case Walls_Item:
			TextureConfigureDialog(HDPtr->TxtrConfigList, OGL_Txtr_Wall);
			break;
		
		case Landscape_Item:
			TextureConfigureDialog(HDPtr->TxtrConfigList, OGL_Txtr_Landscape);
			break;
		
		case Inhabitants_Item:
			TextureConfigureDialog(HDPtr->TxtrConfigList, OGL_Txtr_Inhabitant);
			break;
		
		case WeaponsInHand_Item:
			TextureConfigureDialog(HDPtr->TxtrConfigList, OGL_Txtr_WeaponsInHand);
			break;
		}
	}
}


// True for OK, false for cancel
bool OGL_ConfigureDialog(OGL_ConfigureData& Data)
{
	OSStatus err;
	
	// Get the window
	AutoNibWindow Window(GUI_Nib,Window_Prefs_OpenGL);

	ControlRef Checkboxes[NumCheckboxes];
	for (int k=0; k<NumCheckboxes; k++)
	{
		Checkboxes[k] = GetCtrlFromWindow(Window(), 0, CheckboxDispatch[k][0]);
		SetControl32BitValue(Checkboxes[k], TEST_FLAG(Data.Flags, CheckboxDispatch[k][1]));
	}

	// For making the swatches drawable and hittable
	AutoDrawability Drawability;
	AutoHittability Hittability;
	
	// Temporary area for the colors;
	OGL_Dialog_Handler_Data HandlerData;
	HandlerData.VoidColor = Data.VoidColor;
	for (int il=0; il<4; il++)
		for (int ie=0; ie<2; ie++)
			HandlerData.LscpColors[il][ie] = Data.LscpColors[il][ie];
	
	// The swatch controls:
	ControlRef VoidSwatch = GetCtrlFromWindow(Window(), 0, ColorVoidSwatch_Item);
	Drawability(VoidSwatch, SwatchDrawer, &HandlerData.VoidColor);
	Hittability(VoidSwatch);
	
	ControlRef LscpSwatches[4][2];
	int cwid = LandscapeSwatch_ItemBase;
	for (int il=0; il<4; il++)
		for (int ie=0; ie<2; ie++)
		{
			LscpSwatches[il][ie] = GetCtrlFromWindow(Window(), 0, cwid++);
			Drawability(LscpSwatches[il][ie], SwatchDrawer, &HandlerData.LscpColors[il][ie]);
			Hittability(LscpSwatches[il][ie]);
		}
	
	for (int k=0; k<OGL_NUMBER_OF_TEXTURE_TYPES; k++)
		HandlerData.TxtrConfigList[k] = Data.TxtrConfigList[k];
	
	bool IsOK = RunModalDialog(Window(), true, OGL_Dialog_Handler, &HandlerData);
	
	if (IsOK)
	{
		for (int k=0; k<NumCheckboxes; k++)
			SET_FLAG(Data.Flags, CheckboxDispatch[k][1], GetControl32BitValue(Checkboxes[k]));
		
		Data.VoidColor = HandlerData.VoidColor;
		for (int il=0; il<4; il++)
			for (int ie=0; ie<2; ie++)
				Data.LscpColors[il][ie] = HandlerData.LscpColors[il][ie];
		
		for (int k=0; k<OGL_NUMBER_OF_TEXTURE_TYPES; k++)
			Data.TxtrConfigList[k] = HandlerData.TxtrConfigList[k];
	}
	
	return IsOK;
}


#else

inline void ToggleControl(ControlHandle Hdl)
{
	SetControlValue(Hdl, 1 - GetControlValue(Hdl));
}

// Local copies of this stuff, needed in case we cancel
static OGL_Texture_Configure TxtrConfigList[OGL_NUMBER_OF_TEXTURE_TYPES];
static RGBColor VoidColor, LscpColors[4][2];

// Texture-configuration dialog box
static bool TextureConfigureDialog(short WhichTexture)
// True for OK, false for cancel
{
	short ItemType;
	Rect Bounds;
	
	DialogPtr Dialog = myGetNewDialog(OpenGL_Textures_Dialog, NULL, (WindowPtr)(-1), 0);
	assert(Dialog);
	
	// Get a local copy of the data
	OGL_Texture_Configure TxtrConfig = TxtrConfigList[WhichTexture];
	
	ControlHandle Near_CHdl;
	GetDialogItem(Dialog, Near_Item, &ItemType, (Handle *)&Near_CHdl, &Bounds);
	SetControlValue(Near_CHdl, TxtrConfig.NearFilter+1);
	
	ControlHandle Far_CHdl;
	GetDialogItem(Dialog, Far_Item, &ItemType, (Handle *)&Far_CHdl, &Bounds);
	SetControlValue(Far_CHdl, TxtrConfig.FarFilter+1);

	ControlHandle Res_CHdl;
	GetDialogItem(Dialog, Resolution_Item, &ItemType, (Handle *)&Res_CHdl, &Bounds);
	SetControlValue(Res_CHdl, TxtrConfig.Resolution+1);
	
	ControlHandle Color_CHdl;
	GetDialogItem(Dialog, ColorDepth_Item, &ItemType, (Handle *)&Color_CHdl, &Bounds);
	SetControlValue(Color_CHdl, TxtrConfig.ColorFormat+1);
	
	ControlHandle BasedOn_CHdl;
	GetDialogItem(Dialog, BasedOn_Item, &ItemType, (Handle *)&BasedOn_CHdl, &Bounds);
	SetControlValue(BasedOn_CHdl, WhichTexture+1);
	
	// Edit the title
//#if defined(USE_CARBON_ACCESSORS)
	MenuHandle BasedOn_MHdl = GetControlPopupMenuHandle(BasedOn_CHdl);
/*
#else
	HLock(Handle(BasedOn_CHdl));
	ControlRecord *PopupPtr = *BasedOn_CHdl;
	
	Handle CtrlDataHdl = PopupPtr->contrlData;
	HLock(CtrlDataHdl);
	PopupPrivateData *PPD = (PopupPrivateData *)(*CtrlDataHdl);
	
	MenuHandle BasedOn_MHdl = PPD->mHandle;
#endif
*/
	Str255 WhichTxtrLabel;
	GetMenuItemText(BasedOn_MHdl,WhichTexture+1,WhichTxtrLabel);
	
	Handle WhichOne_Hdl;
	GetDialogItem(Dialog, WhichOne_Item, &ItemType, &WhichOne_Hdl, &Bounds);
	SetDialogItemText(WhichOne_Hdl,WhichTxtrLabel);
	
/*
#if !defined(USE_CARBON_ACCESSORS)
	HUnlock(CtrlDataHdl);
	HUnlock(Handle(BasedOn_CHdl));
#endif
*/	
	short WhichAltTxtr;	// Which alternative one selected
	
	// Reveal it
//#if defined(USE_CARBON_ACCESSORS)
#if USE_SHEETS
	SetThemeWindowBackground(GetDialogWindow(Dialog), kThemeBrushSheetBackgroundTransparent, false);
	WindowRef frontWindow = ActiveNonFloatingWindow();
	ShowSheetWindow(GetDialogWindow(Dialog), frontWindow);
#else
	SelectWindow(GetDialogWindow(Dialog));
	ShowWindow(GetDialogWindow(Dialog));
#endif
/*
#else
	SelectWindow(Dialog);
	ShowWindow(Dialog);
#endif
*/	
	bool WillQuit = false;
	bool IsOK = false;

	while(!WillQuit)
	{
		short ItemHit;
		ModalDialog(NULL, &ItemHit);
		
		switch(ItemHit)
		{
		case OK_Item:
				
			IsOK = true;
			WillQuit = true;
			break;
			
		case Cancel_Item:
			
			IsOK = false;
			WillQuit = true;
			break;
		
		case BasedOn_Item:
			// Copy in from another one (like Forge) if different
			WhichAltTxtr = GetControlValue(BasedOn_CHdl) - 1;
			if (WhichAltTxtr != WhichTexture)
			{
				TxtrConfig = TxtrConfigList[WhichAltTxtr];
				SetControlValue(Near_CHdl, TxtrConfig.NearFilter+1);
				SetControlValue(Far_CHdl, TxtrConfig.FarFilter+1);
				SetControlValue(Res_CHdl, TxtrConfig.Resolution+1);
				SetControlValue(Color_CHdl, TxtrConfig.ColorFormat+1);
			}
			break;
		}
	}
	
	// Change the data structure
	if (IsOK)
	{
		TxtrConfig.NearFilter = GetControlValue(Near_CHdl) - 1;
		TxtrConfig.FarFilter = GetControlValue(Far_CHdl) - 1;
		TxtrConfig.Resolution = GetControlValue(Res_CHdl) - 1;
		TxtrConfig.ColorFormat = GetControlValue(Color_CHdl) - 1;
		TxtrConfigList[WhichTexture] = TxtrConfig;
	}
	
	// Clean up
//#if defined(USE_CARBON_ACCESSORS)
# if USE_SHEETS
	HideSheetWindow(GetDialogWindow(Dialog));
# else
	HideWindow(GetDialogWindow(Dialog));
# endif
/*
#else
	HideWindow(Dialog);
#endif
*/
	DisposeDialog(Dialog);

#if /*defined(USE_CARBON_ACCESSORS) &&*/ USE_SHEETS
	SelectWindow(frontWindow);
#endif
	return IsOK;
}


// Procedure to draw a color swatch
static pascal void PaintSwatch(DialogPtr DPtr, short ItemNo) {
	short ItemType;
	ControlHandle Hdl;
	Rect Bounds;
	GetDialogItem(DPtr, ItemNo, &ItemType, (Handle *)&Hdl, &Bounds);

	RGBColor* ColorPtr = NULL;
	
	int ile;
	switch(ItemNo)
	{
	case ColorVoidSwatch_Item:
		ColorPtr = &VoidColor;
		break;
	
	default:
		ile = LandscapeSwatch_ItemBase;
		for (int il=0; il<4; il++)
		{
			for (int ie=0; ie<2; ie++)
			{
				if (ItemNo == ile)
				{
					ColorPtr = &LscpColors[il][ie];
					break;
				}
				if (ColorPtr != NULL) break;
				ile++;
			}
			if (ColorPtr != NULL) break;
		}
	}
	// Must find at least one color
	assert(ColorPtr);
		
	PenState OldPen;
	RGBColor OldBackColor, OldForeColor;

	// Save previous state		
	GetPenState(&OldPen);
	GetBackColor(&OldBackColor);
	GetForeColor(&OldForeColor);
	
	// Get ready to draw the swatch
	PenNormal();
	
	// Draw!
	RGBForeColor(ColorPtr);
	PaintRect(&Bounds);
	ForeColor(blackColor);
	FrameRect(&Bounds);
	
	// Restore previous state
	SetPenState(&OldPen);
	RGBBackColor(&OldBackColor);
	RGBForeColor(&OldForeColor);
}


// True for OK, false for cancel
bool OGL_ConfigureDialog(OGL_ConfigureData& Data)
{
	short ItemType;
	Rect Bounds;
	RGBColor TempColor;
	Point Loc = {-1,-1};	// Color-picker dialog-box location: in the center of the screen
	
	DialogPtr Dialog = myGetNewDialog(OpenGL_Dialog, NULL, (WindowPtr)(-1), 0);
	assert(Dialog);
	
	// Get the dialog-item features and make lots of local copies
	for (int k=0; k<OGL_NUMBER_OF_TEXTURE_TYPES; k++)
	{
		TxtrConfigList[k] = Data.TxtrConfigList[k];
	}
	
	MacCheckbox ZBuffer_CB(Dialog, ZBuffer_Item, TEST_FLAG(Data.Flags,OGL_Flag_ZBuffer));
	MacCheckbox ColorVoid_CB(Dialog, ColorVoid_Item, TEST_FLAG(Data.Flags,OGL_Flag_VoidColor));
		
	ControlHandle ColorVoidSwatch_CHdl;
	GetDialogItem(Dialog, ColorVoidSwatch_Item, &ItemType, (Handle *)&ColorVoidSwatch_CHdl, &Bounds);
//#if defined(TARGET_API_MAC_CARBON)
	UserItemUPP PaintSwatchUPP = NewUserItemUPP(PaintSwatch);
/*
#else
	UserItemUPP PaintSwatchUPP = NewUserItemProc(PaintSwatch);
#endif
*/
	SetDialogItem(Dialog, ColorVoidSwatch_Item, ItemType, Handle(PaintSwatchUPP), &Bounds);
	
	MacCheckbox FlatColorLandscapes_CB(Dialog, FlatColorLandscapes_Item, TEST_FLAG(Data.Flags,OGL_Flag_FlatLand));
		
	for (int ile=0; ile<8; ile++)
	{
		short LandscapeSwatch_Item = LandscapeSwatch_ItemBase + ile;
		ControlHandle LandscapeSwatch_CHdl;
		GetDialogItem(Dialog, LandscapeSwatch_Item, &ItemType, (Handle *)&LandscapeSwatch_CHdl, &Bounds);
		SetDialogItem(Dialog, LandscapeSwatch_Item, ItemType, Handle(PaintSwatchUPP), &Bounds);
	}
	
	MacCheckbox Fog_CB(Dialog, AllowFog_Item, TEST_FLAG(Data.Flags,OGL_Flag_Fog));
		
	MacCheckbox TwoDimGraphics_CB(Dialog, TwoDimGraphics_Item, TEST_FLAG(Data.Flags,OGL_Flag_2DGraphics));
	MacCheckbox FlatStaticEffect_CB(Dialog, FlatStaticEffect_Item, TEST_FLAG(Data.Flags,OGL_Flag_FlatStatic));
	MacCheckbox FaderEffect_CB(Dialog, Fader_Item, TEST_FLAG(Data.Flags,OGL_Flag_Fader));
	MacCheckbox SeeThruLiquids_CB(Dialog, LiquidSeeThru_Item, TEST_FLAG(Data.Flags,OGL_Flag_LiqSeeThru));
	MacCheckbox Map_CB(Dialog, Map_Item, TEST_FLAG(Data.Flags,OGL_Flag_Map));
	MacCheckbox TextureFix_CB(Dialog, TextureFix_Item, TEST_FLAG(Data.Flags,OGL_Flag_TextureFix));
	MacCheckbox Model_CB(Dialog, Model_Item, TEST_FLAG(Data.Flags,OGL_Flag_3D_Models));
	
	// Load the colors into temporaries
	VoidColor = Data.VoidColor;
	for (int il=0; il<4; il++)
		for (int ie=0; ie<2; ie++)
			LscpColors[il][ie] = Data.LscpColors[il][ie];
	
	// Reveal it
//#if defined(USE_CARBON_ACCESSORS)
#if USE_SHEETS
	SetThemeWindowBackground(GetDialogWindow(Dialog), kThemeBrushSheetBackgroundTransparent, false);
	ShowSheetWindow(GetDialogWindow(Dialog), ActiveNonFloatingWindow());
#else
	SelectWindow(GetDialogWindow(Dialog));
	ShowWindow(GetDialogWindow(Dialog));
#endif
/*
#else
	SelectWindow(Dialog);
	ShowWindow(Dialog);
#endif
*/
	
	bool WillQuit = false;
	bool IsOK = false;
	
	int ile;
	bool Escape;
	while(!WillQuit)
	{
		short ItemHit;
		ModalDialog(NULL, &ItemHit);
		
		switch(ItemHit)
		{
		case OK_Item:
			IsOK = true;
			WillQuit = true;
			break;
			
		case Cancel_Item:
			IsOK = false;
			WillQuit = true;
			break;
		
		case Walls_Item:
			TextureConfigureDialog(OGL_Txtr_Wall);
			break;
		
		case Landscape_Item:
			TextureConfigureDialog(OGL_Txtr_Landscape);
			break;
		
		case Inhabitants_Item:
			TextureConfigureDialog(OGL_Txtr_Inhabitant);
			break;
		
		case WeaponsInHand_Item:
			TextureConfigureDialog(OGL_Txtr_WeaponsInHand);
			break;
				
		case ColorVoidSwatch_Item:
			getpstr(ptemporary,ColorPicker_PromptStrings,ColorVoid_String);
			if (GetColor(Loc,ptemporary,&VoidColor,&TempColor))
			{
				VoidColor = TempColor;
				DrawDialog(Dialog);
			}
			break;
		
		default:
		
			if (ZBuffer_CB.ToggleIfHit(ItemHit)) break;
			if (ColorVoid_CB.ToggleIfHit(ItemHit)) break;
			if (FlatColorLandscapes_CB.ToggleIfHit(ItemHit)) break;
			if (Fog_CB.ToggleIfHit(ItemHit)) break;
			if (TwoDimGraphics_CB.ToggleIfHit(ItemHit)) break;
			if (FlatStaticEffect_CB.ToggleIfHit(ItemHit)) break;
			if (FaderEffect_CB.ToggleIfHit(ItemHit)) break;
			if (SeeThruLiquids_CB.ToggleIfHit(ItemHit)) break;
			if (Map_CB.ToggleIfHit(ItemHit)) break;
			if (TextureFix_CB.ToggleIfHit(ItemHit)) break;
			if (Model_CB.ToggleIfHit(ItemHit)) break;
			
			ile = 0;
			Escape = false;
			for (int il=0; il<4; il++)
			{
				for (int ie=0; ie<2; ie++)
				{
					if (ItemHit == ile+LandscapeSwatch_ItemBase)
					{
					getpstr(ptemporary,ColorPicker_PromptStrings,FlatColorLandscapes_StringBase+ile);
					if (GetColor(Loc,ptemporary,&LscpColors[il][ie],&TempColor))
						{
							LscpColors[il][ie] = TempColor;
							DrawDialog(Dialog);
						}
						Escape = true;
						break;
					}
					ile++;
				}
				if (Escape) break;
			}
		}
	}
	
	// Change the data structure
	if (IsOK)
	{
		for (int k=0; k<OGL_NUMBER_OF_TEXTURE_TYPES; k++)
		{
			 Data.TxtrConfigList[k] = TxtrConfigList[k];
		}
		
		SET_FLAG(Data.Flags,OGL_Flag_ZBuffer,ZBuffer_CB.GetState());
		SET_FLAG(Data.Flags,OGL_Flag_VoidColor,ColorVoid_CB.GetState());
		SET_FLAG(Data.Flags,OGL_Flag_FlatLand,FlatColorLandscapes_CB.GetState());
		SET_FLAG(Data.Flags,OGL_Flag_Fog,Fog_CB.GetState());
		SET_FLAG(Data.Flags,OGL_Flag_2DGraphics,TwoDimGraphics_CB.GetState());
		SET_FLAG(Data.Flags,OGL_Flag_FlatStatic,FlatStaticEffect_CB.GetState());
		SET_FLAG(Data.Flags,OGL_Flag_Fader,FaderEffect_CB.GetState());
		SET_FLAG(Data.Flags,OGL_Flag_LiqSeeThru,SeeThruLiquids_CB.GetState());
		SET_FLAG(Data.Flags,OGL_Flag_Map,Map_CB.GetState());
		SET_FLAG(Data.Flags,OGL_Flag_TextureFix,TextureFix_CB.GetState());
		SET_FLAG(Data.Flags,OGL_Flag_3D_Models,Model_CB.GetState());
		Data.VoidColor = VoidColor;
		for (int il=0; il<4; il++)
			for (int ie=0; ie<2; ie++)
				Data.LscpColors[il][ie] = LscpColors[il][ie];
	}
	
	// Clean up
//#if defined(USE_CARBON_ACCESSORS)
#if USE_SHEETS
	HideSheetWindow(GetDialogWindow(Dialog));
#else
	HideWindow(GetDialogWindow(Dialog));
#endif
/*
#else
	HideWindow(Dialog);
#endif
*/
	DisposeUserItemUPP(PaintSwatchUPP);
	DisposeDialog(Dialog);
	
	return IsOK;
}

#endif
