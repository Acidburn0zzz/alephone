/*
NETWORK_DIALOGS.C  (now network_dialogs_mac_sdl.cpp)

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

Feb 26, 2002 (Br'fin (Jeremy Parsons)):
	Forked off from network_dialogs_macintosh.cpp to tie to SDL networking apis
        
Feb 14, 2003 (Woody Zenfell):
	Support for resuming saved games as network games.
*/
/*
 *  network_dialogs_mac_sdl.cpp - Network dialogs for Carbon with SDL networking
 */
//#define NETWORK_TEST_POSTGAME_DIALOG
//#define ALSO_TEST_PROGRESS_BAR
//#define NETWORK_TEST_GATHER_DIALOG

#if TARGET_API_MAC_CARBON
	#define LIST_BOX_AS_CONTROL 1
#else
	#define LIST_BOX_AS_CONTROL 0
#endif

// add a taunt window for postgame.
// add a message window for gathering
// Don't allow gather on map with no entry points (marathon bites it)
// _overhead_map_is_omniscient is now _burn_items_on_death

#include "cseries.h"
#include "sdl_network.h"

#include "shell.h"  // for preferences
#include "map.h"    // so i can include player.h
#include "player.h" // for displaying damage statistics after network game.
#include "preferences.h"
#include "interface.h" // for _multiplayer_carnage_entry_point
#include "screen_drawing.h"

#include "network_games.h"
#include "network_stream.h"
#include "network_lookup_sdl.h"

// STL Libraries
#include <vector>
#include <algorithm>

// ZZZ: shared dialog item ID constants
#include "network_dialogs.h"

// LP change: outside handler for the default player name
#include "PlayerName.h"

#define TEST_NET_STATS_DIALOG  // for testing the dialog when i don't want to play a net game

#ifdef TEST_NET_STATS_DIALOG
//#define TEST_TEAM_DISPLAY_FOR_DIALOG
#endif

#ifdef env68k
#pragma segment network_dialogs
#endif

struct network_speeds
{
	short updates_per_packet;
	short update_latency;
};

/* ---------- constants */

// LP change: get player name from outside
#define PLAYER_TYPE GetPlayerName()

#define MONSTER_TEAM                8

#define fontTOP_LEVEL_FONT        130
#define menuZONES                1002

// ZZZ: Dialog and string constants moved to network_dialogs.h for sharing between SDL and Mac versions.

#define NAME_BOX_HEIGHT   28
#define NAME_BOX_WIDTH   114

#define BOX_SPACING  8

#define OPTION_KEYCODE             0x3a

/* ---------- globals */

// ZZZ: this is used to communicate between the join game dialog filter proc
// and the join game entry point.  kNetworkJoinFailed is used in this variable
// not to indicate failure, but to indicate that no game has started yet.
static int sStartJoinedGameResult;
static ListHandle network_list_box= (ListHandle) NULL;

// List of found player info when gathering a game
static vector<const SSLP_ServiceInstance*> found_players;

/* from screen_drawing.c */
extern TextSpec *_get_font_spec(short font_index);

/* ---------- private code */
static bool network_game_setup(player_info *player_information, game_info *game_information, bool inResumingGame);
/* static */ short fill_in_game_setup_dialog(DialogPtr dialog, player_info *player_information, bool allow_all_levels);
/* static */ void extract_setup_dialog_information(DialogPtr dialog, player_info *player_information, 
	game_info *game_information, short game_limit_type, bool allow_all_levels);
bool check_setup_information(DialogPtr dialog, short game_limit_type);

// ZZZ: moved to csdialogs
//static short get_dialog_control_value(DialogPtr dialog, short which_control);

static pascal Boolean gather_dialog_filter_proc(DialogPtr dialog, EventRecord *event, short *item_hit);
static pascal Boolean join_dialog_filter_proc(DialogPtr dialog, EventRecord *event, short *item_hit);
static pascal Boolean game_setup_filter_proc(DialogPtr dialog, EventRecord *event, short *item_hit);

static void setup_network_list_box(WindowPtr window, Rect *frame, unsigned char *zone);
static void dispose_network_list_box(void);
static pascal void update_player_list_item(DialogPtr dialog, short item_num);
static void found_player_callback(const SSLP_ServiceInstance* player);

// ZZZ: moved to network.cpp (network.h) so we can share
//static void reassign_player_colors(short player_index, short num_players);

#define NAME_BEVEL_SIZE    4
static void draw_beveled_text_box(bool inset, Rect *box, short bevel_size, RGBColor *brightest_color, char *text,short flags, bool name_box);

// ZZZ: moved a few static functions to network_dialogs.h so we can share

static MenuHandle get_popup_menu_handle(DialogPtr dialog, short item);
static void draw_player_box_with_team(Rect *rectangle, short player_index);

static short get_game_duration_radio(DialogPtr dialog);

static void lost_player_callback(const SSLP_ServiceInstance* player);

static bool key_is_down(short key_code);
#pragma mark -

#ifdef USES_NIBS
static bool CheckSetupInformation(
	NetgameSetupData& Data, 
	short game_limit_type);
#endif

/* ---------- code */

extern void NetUpdateTopology(void);

// JTP: Cribbed from network_dialogs_widgets_sdl, if I can't do it right do it compatibly.
// Actually, as it turns out, there should be a generic STL algorithm that does this, I think.
// Well, w_found_players ought to be using a set<> or similar anyway, much more natural.
// Shrug, this was what I came up with before I knew anything about STL, and I'm too lazy to change it.
template<class T>
static const int
find_item_index_in_vector(const T& inItem, const vector<T>& inVector)
{
	typedef typename std::vector<T>::const_iterator const_iterator;
	const_iterator i(inVector.begin());
	const_iterator end(inVector.end());
	int index	= 0;

	while(i != end) {
		if(*i == inItem)
			return index;
		
		index++;
		i++;
	}
	
	// Didn't find it
	return -1;
}


void modify_limit_type_choice_enabled(DialogPtr dialog, short inChangeEnable)
{
	if(inChangeEnable != NONE)
	{
		modify_control_enabled(dialog, iRADIO_NO_TIME_LIMIT, inChangeEnable);
		modify_control_enabled(dialog, iRADIO_TIME_LIMIT, inChangeEnable);
		modify_control_enabled(dialog, iRADIO_KILL_LIMIT, inChangeEnable);
	}
}


/*************************************************************************************************
 *
 * Function: network_gather
 * Purpose:  do the game setup and gather dialogs
 *
 *************************************************************************************************/

#ifdef USES_NIBS

// Ought to be a property of the player-list control...
NetgameGatherData *GatherDataPtr;

static pascal OSStatus SetPlayerListMember(
		ControlRef Browser,
		DataBrowserItemID ItemID,
		DataBrowserPropertyID PropertyID,
        DataBrowserItemDataRef ItemData,
        Boolean SetValue
		)
{
	NetgameGatherData& Data = *GatherDataPtr;
	
	// Find the player entry
	map<DataBrowserItemID, const SSLP_ServiceInstance*>::iterator Entry = 
		Data.FoundPlayers.find(ItemID);
	
	// Was it really found? if not, quit
	if (Entry == Data.FoundPlayers.end()) return noErr;
	
	const byte *NameBytes = (const byte *)Entry->second->sslps_name;
	
	CFStringRef NameStr = CFStringCreateWithBytes(
					NULL, NameBytes+1, NameBytes[0],
					NULL, false);
	
	SetDataBrowserItemDataText(ItemData, NameStr);
	
	CFRelease(NameStr);
	
	return noErr;
}

static UInt32 HowManySelected(ControlRef Browser)
{
	// Count how many selected;
	// from Apple's sample code in TechNote #2009
	// (a valuable source on the Data Browser control in general)
	UInt32 Count = 0;
	GetDataBrowserItemCount(Browser,
		kDataBrowserNoItem ,		/* start searching at the root item */
		true,						/* recursively search all subitems */
		kDataBrowserItemIsSelected,	/* only return selected items */
		&Count);
	
	return Count;
}


static pascal void PlayerListMemberHit(
		ControlRef Browser,
		DataBrowserItemID Item,
		DataBrowserItemNotification Message
		)
{
	NetgameGatherData& Data = *GatherDataPtr;

	switch(Message)
	{
	case kDataBrowserItemSelected:
	case kDataBrowserItemDoubleClicked:
		SetControlActivity(Data.AddCtrl, NetGetNumberOfPlayers() <= MAXIMUM_NUMBER_OF_PLAYERS);
		break;
		
	case kDataBrowserItemDeselected:
	case kDataBrowserItemRemoved:
		SetControlActivity(Data.AddCtrl, HowManySelected(Data.NetworkDisplayCtrl) > 0);
		break;
	}
}


void PlayerDisplayDrawer(ControlRef Ctrl, void *UserData)
{
	NetgameGatherData *DPtr = (NetgameGatherData *)(UserData);
	NetgameGatherData& Data = *DPtr;

	// No need for the window context -- it's assumed
	Rect Bounds = {0,0,0,0};
	
	GetControlBounds(Ctrl, &Bounds);
	
	// Draw background and boundary
	ForeColor(whiteColor);
	PaintRect(&Bounds);
	ForeColor(blackColor);
	FrameRect(&Bounds);

	// Cribbed from update_player_list_item()
	FontInfo finfo;
	GetFontInfo(&finfo);
	short height = finfo.ascent + finfo.descent + finfo.leading;
	MoveTo(Bounds.left + 3, Bounds.top+height);
	short num_players = NetNumberOfPlayerIsValid() ? NetGetNumberOfPlayers() : 0;
	
	Rect name_rect;
	SetRect(&name_rect, Bounds.left, Bounds.top, Bounds.left+NAME_BOX_WIDTH, Bounds.top+NAME_BOX_HEIGHT);
	for (short i = 0; i < num_players; i++)
	{
		draw_player_box_with_team(&name_rect, i);
		if (!(i % 2))
		{
			OffsetRect(&name_rect, NAME_BOX_WIDTH+BOX_SPACING, 0);
		}
		else
		{
			OffsetRect(&name_rect, -(NAME_BOX_WIDTH+BOX_SPACING), NAME_BOX_HEIGHT + BOX_SPACING);
		}
	}
	
}


void NetgameGather_Handler(ParsedControl& Ctrl, void *UserData)
{
	NetgameGatherData *DPtr = (NetgameGatherData *)(UserData);
	NetgameGatherData& Data = *DPtr;
	
	Handle ItemsHdl;
	DataBrowserItemID *ItemsPtr;
	
	switch(Ctrl.ID.id)
	{
	case iADD:
		// Find which players were selected
		
		ItemsHdl = NewHandle(0);
		GetDataBrowserItems(Data.NetworkDisplayCtrl,
			NULL, false, kDataBrowserItemIsSelected,
			ItemsHdl);
		
		int NumItems = GetHandleSize(ItemsHdl)/sizeof(DataBrowserItemID);
		
		HLock(ItemsHdl);
		ItemsPtr = (DataBrowserItemID *)(*ItemsHdl);
		
		for (int k=0; k<NumItems; k++)
		{
			// No need to try if we've gathered enough players
			if (NetGetNumberOfPlayers() >= MAXIMUM_NUMBER_OF_PLAYERS)
				continue;
			
			// Examine each entry in the list
			map<DataBrowserItemID, const SSLP_ServiceInstance*>::iterator Entry = 
				Data.FoundPlayers.find(ItemsPtr[k]);
			
			// Was it really found? if not, then try the next one
			if (Entry == Data.FoundPlayers.end()) continue;
	
			const SSLP_ServiceInstance *player = Entry->second;
			
			// Remove player from lists
			lost_player_callback(player);
			
			// Gather player
			int theGatherPlayerResult = NetGatherPlayer(player, reassign_player_colors);
			if (theGatherPlayerResult != kGatherPlayerFailed)
			{
				Draw1Control(Data.PlayerDisplayCtrl);
				if(theGatherPlayerResult == kGatheredUnacceptablePlayer)
					Data.AllPlayersOK = false;
			}
		}
		
		// Note: ADD button is handled in list-box callback
		SetControlActivity(Data.OK_Ctrl, (NetGetNumberOfPlayers() > 1) && Data.AllPlayersOK);
		
		break;	
	}
}


const double PollingInterval = 1.0/30.0;

static pascal void NetgameGather_Poller(EventLoopTimerRef Timer, void *UserData)
{
	NetgameGatherData *DPtr = (NetgameGatherData *)(UserData);
	NetgameGatherData& Data = *DPtr;
	
	SSLP_Pump();
	
	Draw1Control(Data.PlayerDisplayCtrl);
}

#ifndef NETWORK_TEST_POSTGAME_DIALOG

bool network_gather(bool ResumingGame)
{
	bool successful= false;
	game_info myGameInfo;
	player_info myPlayerInfo;

	show_cursor(); // JTP: Hidden one way or another
	
	// Do early returns if these inits failed
	
	if (!network_game_setup(&myPlayerInfo, &myGameInfo, ResumingGame))
		return successful;
	
	myPlayerInfo.desired_color= myPlayerInfo.color;
	
	if (!NetEnter())
		return successful;

	if (!NetGather(&myGameInfo, sizeof(game_info), (void*) &myPlayerInfo, 
		sizeof(myPlayerInfo), ResumingGame))
		return successful;
	
	// Now for the dialog box
	
	AutoNibWindow Window(GUI_Nib, Window_Network_Gather);
	
	NetgameGatherData Data;
	GatherDataPtr = &Data;

	// Actually a Data Browser control, a sort of super list box introduced in Carbon/OSX
	Data.NetworkDisplayCtrl = GetCtrlFromWindow(Window(), 0, iNETWORK_LIST_BOX);
	
	DataBrowserCallbacks Callbacks;
	obj_clear(Callbacks);	// Makes everything NULL
	Callbacks.version = kDataBrowserLatestCallbacks;
	Callbacks.u.v1.itemDataCallback = NewDataBrowserItemDataUPP(SetPlayerListMember);
	Callbacks.u.v1.itemNotificationCallback = NewDataBrowserItemNotificationUPP(PlayerListMemberHit);
	SetDataBrowserCallbacks(Data.NetworkDisplayCtrl, &Callbacks);
	
	Data.ItemID = 1; 	// Initial values
	Data.AllPlayersOK = true;
	
	Data.PlayerDisplayCtrl = GetCtrlFromWindow(Window(), 0, iPLAYER_DISPLAY_AREA);
	
	AutoDrawability Drawability;
	Drawability(Data.PlayerDisplayCtrl, PlayerDisplayDrawer, &Data);
	
	Data.AddCtrl = GetCtrlFromWindow(Window(), 0, iADD);
	Data.OK_Ctrl = GetCtrlFromWindow(Window(), 0, iOK);
	
	SetControlActivity(Data.AddCtrl,false);
	SetControlActivity(Data.OK_Ctrl,false);

	AutoTimer Poller(0, PollingInterval, NetgameGather_Poller, &Data);

#ifdef NETWORK_TEST_GATHER_DIALOG
	SSLP_ServiceInstance test[] = {
		{"type", "\pTesting one", {0, 0}}, {"type", "\pTesting two", {0, 0}},
		{"type", "\pTesting three", {0, 0}}, {"type", "\pTesting four", {0, 0}},
		{"type", "\pTesting five", {0, 0}}, {"type", "\pTesting six", {0, 0}},
		{"type", "\pTesting seven", {0, 0}}, {"type", "\pTesting eight", {0, 0}},
		{"type", "\pTesting nine", {0, 0}}, {"type", "\pTesting ten", {0, 0}},
		{"type", "\pTesting eleven", {0, 0}}, {"type", "\pTesting twelve", {0, 0}}};
	const int test_count= sizeof(test)/sizeof(SSLP_ServiceInstance);
	for(int i= 0; i < test_count; i++)
		found_player_callback(&test[i]);
#endif
	
	successful = RunModalDialog(Window(), false, NetgameGather_Handler, &Data);
	
	if (!successful)
	{
		NetCancelGather();
		NetExit();
	}
	
	// Clean up
	DisposeDataBrowserItemDataUPP(Callbacks.u.v1.itemDataCallback);
	DisposeDataBrowserItemNotificationUPP(Callbacks.u.v1.itemNotificationCallback);
	
	return successful;
}

#endif //ndef NETWORK_TEST_POSTGAME_DIALOG

#else

#ifndef NETWORK_TEST_POSTGAME_DIALOG

bool network_gather(bool inResumingGame)
{
	bool successful= false;
	game_info myGameInfo;
	player_info myPlayerInfo;

	show_cursor(); // JTP: Hidden one way or another
	if (network_game_setup(&myPlayerInfo, &myGameInfo, inResumingGame))
	{
		myPlayerInfo.desired_color= myPlayerInfo.color;
		memcpy(myPlayerInfo.long_serial_number, serial_preferences->long_serial_number, 10);
	
		if(NetEnter())
		{
			DialogPtr dialog;
			Rect item_rectangle;
			Handle item_handle;
			ModalFilterUPP gather_dialog_upp;
			UserItemUPP update_player_list_item_upp = NewUserItemUPP(update_player_list_item);
			short current_zone_index, item_type;
			Cell cell;
			short item_hit;
			ControlRef control;

#if LIST_BOX_AS_CONTROL
			dialog= myGetNewDialog(dlogGATHER, NULL, (WindowPtr) -1, 0);
#else
			dialog= myGetNewDialog(dlogGATHER, NULL, (WindowPtr) -1, refNETWORK_GATHER_DIALOG);
#endif
			assert(dialog);
			gather_dialog_upp= NewModalFilterUPP(gather_dialog_filter_proc);
			assert(gather_dialog_upp);
		
			GetDialogItem(dialog, iZONES_MENU, &item_type, &item_handle, &item_rectangle);
			SetControlValue((ControlHandle) item_handle, current_zone_index);
			GetDialogItem(dialog, iNETWORK_LIST_BOX, &item_type, &item_handle, &item_rectangle);
			setup_network_list_box(GetDialogWindow(dialog), &item_rectangle, (unsigned char *)"\p*");

			GetDialogItem(dialog, iPLAYER_DISPLAY_AREA, &item_type, &item_handle, &item_rectangle);
			SetDialogItem(dialog, iPLAYER_DISPLAY_AREA, kUserDialogItem|kItemDisableBit,
				(Handle)update_player_list_item_upp, &item_rectangle);
			
			// We're on the internet, just show item "Players in Network:"
			HideDialogItem(dialog, iZONES_MENU);
			
#if LIST_BOX_AS_CONTROL
			GetDialogItemAsControl( dialog, iNETWORK_LIST_BOX, &control );
			SetKeyboardFocus(GetDialogWindow(dialog), control, kControlListBoxPart);
#endif

			ShowWindow(GetDialogWindow(dialog));

#ifdef NETWORK_TEST_GATHER_DIALOG
			SSLP_ServiceInstance test[] = {
				{"type", "\pTesting one", {0, 0}}, {"type", "\pTesting two", {0, 0}},
				{"type", "\pTesting three", {0, 0}}, {"type", "\pTesting four", {0, 0}},
				{"type", "\pTesting five", {0, 0}}, {"type", "\pTesting six", {0, 0}},
				{"type", "\pTesting seven", {0, 0}}, {"type", "\pTesting eight", {0, 0}},
				{"type", "\pTesting nine", {0, 0}}, {"type", "\pTesting ten", {0, 0}},
				{"type", "\pTesting eleven", {0, 0}}, {"type", "\pTesting twelve", {0, 0}}};
			const int test_count= sizeof(test)/sizeof(SSLP_ServiceInstance);
			for(int i= 0; i < test_count; i++)
			{
				found_player_callback(&test[i]);
			}
#endif
			if(NetGather(&myGameInfo, sizeof(game_info), (void*) &myPlayerInfo, 
				sizeof(myPlayerInfo), inResumingGame))
			{
				do
				{
					short number_of_players= NetGetNumberOfPlayers();
					bool gathered_unacceptable_player= false;
					
					/* set button states */
					SetPt(&cell, 0, 0);
					short iADD_state= CONTROL_INACTIVE;
					if(number_of_players < MAXIMUM_NUMBER_OF_NETWORK_PLAYERS &&
						LGetSelect(true, &cell, network_list_box))
					{
						iADD_state= CONTROL_ACTIVE;
					}
					short iOK_state= CONTROL_INACTIVE;
					if(number_of_players>1 && !gathered_unacceptable_player)
					{
						iOK_state= CONTROL_ACTIVE;
					}
					modify_control(dialog, iADD, iADD_state, 0);
					modify_control(dialog, iOK, iOK_state, 0);
					
					ModalDialog(gather_dialog_upp, &item_hit);
			
					switch (item_hit)
					{
#if LIST_BOX_AS_CONTROL
						case iNETWORK_LIST_BOX:
							Boolean gotDoubleClick;
							Size actualSize;
							OSStatus err;
							
							GetDialogItemAsControl( dialog, iNETWORK_LIST_BOX, &control );
							err = GetControlData( control, 0, kControlListBoxDoubleClickTag,
								sizeof( Boolean ), (Ptr)&gotDoubleClick, &actualSize );
							if(!gotDoubleClick) break;
#endif

						case iADD:
							SetPt(&cell, 0, 0);
							if (LGetSelect(true, &cell, network_list_box)) /* if no selection, we goofed */
							{
								// Get player info
								const SSLP_ServiceInstance* player = found_players[cell.v];
								
								// Remove player from lists
								lost_player_callback(player);
								
								// Gather player
								int theGatherPlayerResult = NetGatherPlayer(player, reassign_player_colors);
								if (theGatherPlayerResult != kGatherPlayerFailed)
								{
									update_player_list_item(dialog, iPLAYER_DISPLAY_AREA);
									if(theGatherPlayerResult == kGatheredUnacceptablePlayer)
									{
										gathered_unacceptable_player= true;
									}
								}
							}
							break;
					}
				} while(item_hit!=iCANCEL && item_hit!=iOK);
			} else {
				/* Failed on NetGather */
				item_hit=iCANCEL;
			}

			dispose_network_list_box();
		
			DisposeUserItemUPP(update_player_list_item_upp);
			DisposeModalFilterUPP(gather_dialog_upp);
			DisposeDialog(dialog);
		
			if (item_hit==iOK)
			{
				successful= true;
			}
			else
			{
				NetCancelGather();
				NetExit();
			}
		} else {
			/* error correction handled in the network code now.. */
		}
	}

	hide_cursor();
	return successful;
}

#endif

#endif //ndef NETWORK_TEST_POSTGAME_DIALOG

/*************************************************************************************************
 *
 * Function: network_join
 * Purpose:  do the dialog to join a network game.
 *
 *************************************************************************************************/

#ifdef USES_NIBS

static bool RecentHostAddressMenuBuilder(
	int indx, Str255 ItemName, bool &ThisIsInitial, void *Data)
{
	// First one is one selected
	ThisIsInitial = (indx == 1);
	
	// Get the next address string;
	// Be sure to call "_StartIter()" before calling this
	char *Addr = RecentHostAddresses_NextIter();
	
	if (Addr)
	{
		CopyCStringToPascal(Addr,ItemName);
		return true;
	}
	else
		return false;
}


static pascal OSStatus Join_PlayerNameWatcher(
	EventHandlerCallRef HandlerCallRef,
	EventRef Event,
	void *UserData
	)
{
	NetgameJoinData *DPtr = (NetgameJoinData *)(UserData);
	NetgameJoinData& Data = *DPtr;
	
	// Hand off to the next event handler
	OSStatus err = CallNextEventHandler(HandlerCallRef, Event);
	
	// Need this order because we want to check the text field
	// after it's been changed, not before.
	// Adjust the OK button's activity as needed
	GetEditPascalText(Data.PlayerNameCtrl, ptemporary);
	SetControlActivity(Data.JoinCtrl, (ptemporary[0] != 0) && (Data.JoinState == NONE));
	
	return err;
}

void NetgameJoin_Handler(ParsedControl& Ctrl, void *UserData)
{
	NetgameJoinData *DPtr = (NetgameJoinData *)(UserData);
	NetgameJoinData& Data = *DPtr;
	
	int Value;
	bool did_join;
	char *Addr;

	switch(Ctrl.ID.id)
	{
	case iJOIN_BY_HOST_RECENT:
		// Set the current address string to the selected one
		Value = GetControl32BitValue(Ctrl.Ctrl);
		RecentHostAddresses_StartIter();
		for (int k=0; k<Value; k++)
			Addr = RecentHostAddresses_NextIter();
		
		if (Addr)
		{
			strcpy(network_preferences->join_address, Addr);
			SetEditCText(Data.ByHost_AddressCtrl,Addr);
			Draw1Control(Data.ByHost_AddressCtrl);
		}
		break;
	
	case iOK_SPECIAL:
		// Try to join!
		
		GetEditPascalText(Data.PlayerNameCtrl, ptemporary);
		if (ptemporary[0] > MAX_NET_PLAYER_NAME_LENGTH) ptemporary[0] = MAX_NET_PLAYER_NAME_LENGTH;
		pstrcpy(Data.myPlayerInfo.name, ptemporary);
		
		Data.myPlayerInfo.team = GetControl32BitValue(Data.PlayerTeamCtrl);
		Data.myPlayerInfo.color = GetControl32BitValue(Data.PlayerColorCtrl);
		Data.myPlayerInfo.desired_color = Data.myPlayerInfo.color;
		
		if (GetControl32BitValue(Data.ByHost_Ctrl))
		{
			network_preferences->join_by_address = true;
			
			GetEditPascalText(Data.ByHost_AddressCtrl, ptemporary);
			
			if (ptemporary[0] > kJoinHintingAddressLength)
				ptemporary[0] = kJoinHintingAddressLength;
			CopyPascalStringToC(ptemporary, network_preferences->join_address);
			RecentHostAddresses_Add(network_preferences->join_address);
		}
		else
			network_preferences->join_by_address = false;
		
		Data.DidJoin =
			NetGameJoin(Data.myPlayerInfo.name, PLAYER_TYPE,
				(void *) &Data.myPlayerInfo, sizeof(Data.myPlayerInfo), 
				get_network_version(),
				network_preferences->join_by_address ? network_preferences->join_address : NULL
				);
		
		if (Data.DidJoin)
		{
			SetControlActivity(Data.PlayerNameCtrl, false);
			SetControlActivity(Data.PlayerTeamCtrl, false);
			SetControlActivity(Data.PlayerColorCtrl, false);
			
			SetControlActivity(Data.ByHost_Ctrl, false);
			SetControlActivity(Data.ByHost_LabelCtrl, false);
			SetControlActivity(Data.ByHost_AddressCtrl, false);
			SetControlActivity(Data.ByHost_RecentLabelCtrl, false);
			SetControlActivity(Data.ByHost_RecentCtrl, false);
			
			SetControlActivity(Data.JoinCtrl, false);
						
			// update preferences for user (Eat Gaseous Worms!)
			pstrcpy(player_preferences->name, Data.myPlayerInfo.name);
			player_preferences->team = Data.myPlayerInfo.team;
			player_preferences->color = Data.myPlayerInfo.color;
			write_preferences();
			
			getpstr(ptemporary, strJOIN_DIALOG_MESSAGES, _join_dialog_waiting_string);
			SetStaticPascalText(Data.MessageCtrl, ptemporary);	
		}
		else
		{
			// Bug out!
			StopModalDialog(ActiveNonFloatingWindow(),false);
		}
		
		break;
		
	case iJOIN_BY_HOST:
		Value = GetControl32BitValue(Ctrl.Ctrl);
		SetControlActivity(Data.ByHost_LabelCtrl, Value);
		SetControlActivity(Data.ByHost_AddressCtrl, Value);
		SetControlActivity(Data.ByHost_RecentLabelCtrl, Value);
		SetControlActivity(Data.ByHost_RecentCtrl, Value);
		
		break;
	}
}

static pascal void NetgameJoin_Poller(EventLoopTimerRef Timer, void *UserData)
{
	NetgameJoinData *DPtr = (NetgameJoinData *)(UserData);
	NetgameJoinData& Data = *DPtr;
	
	// Cribbed from join_dialog_filter_proc
	
	// Let SSLP do its thing (respond to FIND messages, hint out HAVE messages, etc.)
	SSLP_Pump();
	
	/* check and see if we�ve gotten any connection requests */
	int JoinState = NetUpdateJoinState();
	switch (JoinState)
	{
		case NONE: // haven't Joined yet.
			break;

		case netJoining:
			break;

		case netCancelled: /* the server cancelled the game; force bail */
			StopModalDialog(ActiveNonFloatingWindow(),false);
			break;

		case netWaiting: /* if we just changed netJoining to netWaiting change the dialog text */
#ifdef OBSOLETE
			if (last_join_state==netJoining)
			{
				getpstr(ptemporary, strJOIN_DIALOG_MESSAGES, _join_dialog_accepted_string);
				SetStaticPascalText(Data.MessageCtrl, ptemporary);
			}
#endif
			SetControlActivity(Data.CancelCtrl, false);
			break;

		case netStartingUp: /* the game is starting up (we have the network topography) */
			Data.JoinResult = kNetworkJoinedNewGame;
			StopModalDialog(ActiveNonFloatingWindow(),false);
			break;

		case netStartingResumeGame: /* the game is starting up a resume game (we have the network topography) */
			Data.JoinResult = kNetworkJoinedResumeGame;
			StopModalDialog(ActiveNonFloatingWindow(),false);
			break;

		case netPlayerAdded:
			if(Data.JoinState==netWaiting)
			{
				char joinMessage[256];
				
				game_info *info= (game_info *)NetGetGameData();

				get_network_joined_message(joinMessage, info->net_game_type);
				SetStaticCText(Data.MessageCtrl, joinMessage);
			}
			// Update!
			Draw1Control(Data.PlayerDisplayCtrl);
			break;

		case netJoinErrorOccurred:
			StopModalDialog(ActiveNonFloatingWindow(),false);
			break;
                
		case netChatMessageReceived:
			{
				player_info*	sending_player;
				char*		chat_message;
				
				if(NetGetMostRecentChatMessage(&sending_player, &chat_message)) {
					// should actually do something with the message here
					// be sure to copy any info you need as the next call to
					// NetUpdateJoinState could overwrite the storage we're pointing at.
				}
			}
			break;

		default:
			// LP change:
			assert(false);
			// halt();
	}
	Data.JoinState = JoinState;
	
	// Update the "Join" button
	GetEditPascalText(Data.PlayerNameCtrl, ptemporary);
	SetControlActivity(Data.JoinCtrl, (ptemporary[0] != 0) && (Data.JoinState == NONE));
}

int network_join(
	void)
{
	show_cursor(); // Hidden one way or another
	
	/* If we can enter the network... */
	if(!NetEnter()) return kNetworkJoinFailed;
	
	// Now for the dialog box
	
	AutoNibWindow Window(GUI_Nib, Window_Network_Join);
	
	NetgameJoinData Data;

	Data.PlayerNameCtrl = GetCtrlFromWindow(Window(), 0, iJOIN_NAME);

	short name_length= player_preferences->name[0];
	if(name_length>MAX_NET_PLAYER_NAME_LENGTH) name_length= MAX_NET_PLAYER_NAME_LENGTH;
	memcpy(Data.myPlayerInfo.name, player_preferences->name, name_length+1);
	
	SetEditPascalText(Data.PlayerNameCtrl, Data.myPlayerInfo.name);
	
	Data.PlayerTeamCtrl = GetCtrlFromWindow(Window(), 0, iJOIN_TEAM);
	SetControl32BitValue(Data.PlayerTeamCtrl, player_preferences->team+1);

	AutoKeyboardWatcher Watcher(Join_PlayerNameWatcher);
	
	Watcher.Watch(Data.PlayerNameCtrl, &Data);
	
	Data.PlayerColorCtrl = GetCtrlFromWindow(Window(), 0, iJOIN_COLOR);
	SetControl32BitValue(Data.PlayerColorCtrl, player_preferences->color+1);

	Data.MessageCtrl = GetCtrlFromWindow(Window(), 0, iJOIN_MESSAGES);
	getpstr(ptemporary, strJOIN_DIALOG_MESSAGES, _join_dialog_welcome_string);
	SetStaticPascalText(Data.MessageCtrl, ptemporary);
	
	Data.PlayerDisplayCtrl = GetCtrlFromWindow(Window(), 0, iPLAYER_DISPLAY_AREA);
	
	AutoDrawability Drawability;
	Drawability(Data.PlayerDisplayCtrl, PlayerDisplayDrawer, &Data);	
	
	Data.ByHost_Ctrl = GetCtrlFromWindow(Window(), 0, iJOIN_BY_HOST);
	SetControl32BitValue(Data.ByHost_Ctrl, network_preferences->join_by_address);
	
	Data.ByHost_LabelCtrl = GetCtrlFromWindow(Window(), 0, iJOIN_BY_HOST_LABEL);
	SetControlActivity(Data.ByHost_LabelCtrl, network_preferences->join_by_address);
	
	Data.ByHost_AddressCtrl = GetCtrlFromWindow(Window(), 0, iJOIN_BY_HOST_ADDRESS);
	CopyCStringToPascal(network_preferences->join_address, ptemporary);
	SetEditPascalText(Data.ByHost_AddressCtrl, ptemporary);
	SetControlActivity(Data.ByHost_AddressCtrl, network_preferences->join_by_address);
	
	Data.ByHost_RecentLabelCtrl = GetCtrlFromWindow(Window(), 0, iJOIN_BY_HOST_RECENT_LABEL);
	SetControlActivity(Data.ByHost_RecentLabelCtrl, network_preferences->join_by_address);
	
	Data.ByHost_RecentCtrl = GetCtrlFromWindow(Window(), 0, iJOIN_BY_HOST_RECENT);
	SetControlActivity(Data.ByHost_RecentCtrl, network_preferences->join_by_address);
	
	// Add the most recently joined sites, earliest to latest
	RecentHostAddresses_Add(network_preferences->join_address);
	
	// "_Startiter()" must be before the "_NextIter()"'s in the menu builder
	RecentHostAddresses_StartIter();
	BuildMenu(Data.ByHost_RecentCtrl, RecentHostAddressMenuBuilder);
	
	Data.CancelCtrl = GetCtrlFromWindow(Window(), 0, iCANCEL);
	Data.JoinCtrl = GetCtrlFromWindow(Window(), 0, iOK_SPECIAL);
	if (Data.myPlayerInfo.name[0] == 0) SetControlActivity(Data.JoinCtrl, false);
	
	Data.DidJoin = false;
	Data.JoinState = NONE;	// Have not joined
	Data.JoinResult = kNetworkJoinFailed;	// Change if succeeded, of course
	
	AutoTimer Poller(0, PollingInterval, NetgameJoin_Poller, &Data);
	
	RunModalDialog(Window(), false, NetgameJoin_Handler, &Data);
	
	if (Data.JoinResult != kNetworkJoinFailed)
	{
		game_info *GameInfoPtr = (game_info *)NetGetGameData();
		NetSetInitialParameters(
			GameInfoPtr->initial_updates_per_packet,
			GameInfoPtr->initial_update_latency);
	}
	else
	{
		if (Data.DidJoin)
			NetCancelJoin();
		
		NetExit();
	}
	
	hide_cursor();
	return Data.JoinResult;
}


#else

int network_join(
	void)
{
	show_cursor(); // Hidden one way or another
	
	/* If we can enter the network... */
	if(NetEnter())
	{
		short item_hit, item_type;
		GrafPtr old_port;
		bool did_join = false;
		DialogPtr dialog;
		player_info myPlayerInfo;
		game_info *myGameInfo;
		ModalFilterUPP join_dialog_upp;
		UserItemUPP update_player_list_item_upp = NewUserItemUPP(update_player_list_item);
		Rect item_rect;
		Handle item_handle;
		short name_length;
		
		dialog= myGetNewDialog(dlogJOIN, NULL, (WindowPtr) -1, 0);
		assert(dialog);
		join_dialog_upp = NewModalFilterUPP(join_dialog_filter_proc);
		assert(join_dialog_upp);
	
		name_length= player_preferences->name[0];
		if(name_length>MAX_NET_PLAYER_NAME_LENGTH) name_length= MAX_NET_PLAYER_NAME_LENGTH;
		memcpy(myPlayerInfo.name, player_preferences->name, name_length+1);

		copy_pstring_to_text_field(dialog, iJOIN_NAME, myPlayerInfo.name);
		SelectDialogItemText(dialog, iJOIN_NAME, 0, INT16_MAX);
		modify_control(dialog, iJOIN_TEAM, CONTROL_ACTIVE, player_preferences->team+1);
		modify_control(dialog, iJOIN_COLOR, CONTROL_ACTIVE, player_preferences->color+1);
		if (myPlayerInfo.name[0] == 0) modify_control(dialog, iOK, CONTROL_INACTIVE, NONE);
	
		copy_pstring_to_static_text(dialog, iJOIN_MESSAGES,
			getpstr(ptemporary, strJOIN_DIALOG_MESSAGES, _join_dialog_welcome_string));

		GetDialogItem(dialog, iPLAYER_DISPLAY_AREA, &item_type, &item_handle, &item_rect);
		SetDialogItem(dialog, iPLAYER_DISPLAY_AREA, kUserDialogItem|kItemDisableBit,
			(Handle)update_player_list_item_upp, &item_rect);

#if TARGET_API_MAC_CARBON
		CopyCStringToPascal(network_preferences->join_address, ptemporary);
#else
		strncpy(temporary, network_preferences->join_address, 256);
		c2pstr(temporary);
#endif
		copy_pstring_to_text_field(dialog, iJOIN_BY_HOST_ADDRESS, ptemporary);

		modify_boolean_control(dialog, iJOIN_BY_HOST, NONE, network_preferences->join_by_address);

		// JTP: Requires control embedding
		// I would have put these in a pane by themselves if I could have figured out
		// how to without having a visible group box
		modify_control_enabled(dialog, iJOIN_BY_HOST_LABEL,   network_preferences->join_by_address?CONTROL_ACTIVE:CONTROL_INACTIVE);
		modify_control_enabled(dialog, iJOIN_BY_HOST_ADDRESS, network_preferences->join_by_address?CONTROL_ACTIVE:CONTROL_INACTIVE);
	
		sStartJoinedGameResult = kNetworkJoinFailed;

		GetPort(&old_port);
		SetPort(GetWindowPort(GetDialogWindow(dialog)));
		ShowWindow(GetDialogWindow(dialog));
		
		do
		{
			ModalDialog(join_dialog_upp, &item_hit);
			switch(item_hit)
			{
				case iJOIN:
					SetPort(GetWindowPort(GetDialogWindow(dialog)));
					GetDialogItem(dialog, iJOIN_NAME, &item_type, &item_handle, &item_rect);
					GetDialogItemText(item_handle, ptemporary);
					if (*temporary > MAX_NET_PLAYER_NAME_LENGTH) *temporary = MAX_NET_PLAYER_NAME_LENGTH;
					pstrcpy(myPlayerInfo.name, ptemporary);
					GetDialogItem(dialog, iJOIN_TEAM, &item_type, &item_handle, &item_rect);
					myPlayerInfo.team= GetControlValue((ControlHandle) item_handle) - 1;
					GetDialogItem(dialog, iJOIN_COLOR, &item_type, &item_handle, &item_rect);
					myPlayerInfo.color= GetControlValue((ControlHandle) item_handle) - 1;
					myPlayerInfo.desired_color= myPlayerInfo.color;
					memcpy(myPlayerInfo.long_serial_number, serial_preferences->long_serial_number, 10);
					if(get_boolean_control_value(dialog, iJOIN_BY_HOST))
					{
						network_preferences->join_by_address = true;
						GetDialogItem(dialog, iJOIN_BY_HOST_ADDRESS, &item_type, &item_handle, &item_rect);
						GetDialogItemText(item_handle, ptemporary);
						if (*temporary > kJoinHintingAddressLength)
							*temporary = kJoinHintingAddressLength;
						CopyPascalStringToC(ptemporary, network_preferences->join_address);
					}
					else
						network_preferences->join_by_address = false;
/*
#if !TARGET_API_MAC_CARBON
					// Aqua will handle if we're unresponseive
					SetCursor(*GetCursor(watchCursor));
#endif
*/
					did_join= NetGameJoin(myPlayerInfo.name, PLAYER_TYPE, (void *) &myPlayerInfo, sizeof(myPlayerInfo), 
						get_network_version(),
						network_preferences->join_by_address ? network_preferences->join_address : NULL
						);

/*					
#if !TARGET_API_MAC_CARBON
					SetCursor(&qd.arrow);
#endif
*/
					if(did_join)
					{
						modify_control_enabled(dialog, iJOIN_NAME, CONTROL_INACTIVE);
						modify_control_enabled(dialog, iJOIN_BY_HOST, CONTROL_INACTIVE);
						modify_control(dialog, iJOIN_TEAM, CONTROL_INACTIVE, NONE);
						modify_control(dialog, iJOIN_COLOR, CONTROL_INACTIVE, NONE);
						modify_control(dialog, iJOIN, CONTROL_INACTIVE, NONE);
						modify_control_enabled(dialog, iJOIN_BY_HOST_LABEL, CONTROL_INACTIVE);
						modify_control_enabled(dialog, iJOIN_BY_HOST_ADDRESS, CONTROL_INACTIVE);
	
						// update preferences for user (Eat Gaseous Worms!)
						pstrcpy(player_preferences->name, myPlayerInfo.name);
						player_preferences->team = myPlayerInfo.team;
						player_preferences->color = myPlayerInfo.color;
						write_preferences();
	
						copy_pstring_to_static_text(dialog, iJOIN_MESSAGES,
							getpstr(ptemporary, strJOIN_DIALOG_MESSAGES, _join_dialog_waiting_string));
					} else {
						/* If you fail in joining the game, print the error and return */
						/*  to the main menu (this is primarily for modem) */
						item_hit= iCANCEL;
					}
					break;
					
				case iCANCEL:
					break;
					
				case iJOIN_TEAM:
					break;
	
				case iJOIN_BY_HOST:
					modify_control(dialog, item_hit, NONE, !get_dialog_control_value(dialog, item_hit));
					if(get_dialog_control_value(dialog, item_hit))
					{
						modify_control_enabled(dialog, iJOIN_BY_HOST_LABEL, CONTROL_ACTIVE);
						modify_control_enabled(dialog, iJOIN_BY_HOST_ADDRESS, CONTROL_ACTIVE);
					}
					else
					{
						modify_control_enabled(dialog, iJOIN_BY_HOST_LABEL, CONTROL_INACTIVE);
						modify_control_enabled(dialog, iJOIN_BY_HOST_ADDRESS, CONTROL_INACTIVE);
					}
					break;
					
				default:
					break;
			}
		}
		while (sStartJoinedGameResult == kNetworkJoinFailed && item_hit != iCANCEL);
	
		SetPort(old_port);
	
		DisposeUserItemUPP(update_player_list_item_upp);
		DisposeModalFilterUPP(join_dialog_upp);
		DisposeDialog(dialog);
	
		if (sStartJoinedGameResult != kNetworkJoinFailed)
		{
			myGameInfo= (game_info *)NetGetGameData();
			NetSetInitialParameters(myGameInfo->initial_updates_per_packet, myGameInfo->initial_update_latency);
		}
		else
		{
			if (did_join)
			{
				NetCancelJoin();
			}
			
			NetExit();
		}
	}
	
	hide_cursor();
	return sStartJoinedGameResult;
}

#endif

/* ---------- private code */

/*************************************************************************************************
 *
 * Function: network_game_setup
 * Purpose:  handle the dialog to setup a network game.
 *
 *************************************************************************************************/

#ifdef USES_NIBS

static pascal OSStatus Setup_PlayerNameWatcher(
	EventHandlerCallRef HandlerCallRef,
	EventRef Event,
	void *UserData
	)
{
	NetgameSetupData *DPtr = (NetgameSetupData *)(UserData);
	NetgameSetupData& Data = *DPtr;
	
	// Hand off to the next event handler
	OSStatus err = CallNextEventHandler(HandlerCallRef, Event);
	
	// Need this order because we want to check the text field
	// after it's been changed, not before.
	// Adjust the OK button's activity as needed
	GetEditPascalText(Data.PlayerNameCtrl, ptemporary);
	SetControlActivity(Data.OK_Ctrl, ptemporary[0] != 0);
	
	return err;
}

void NetgameSetup_Handler(ParsedControl& Ctrl, void *UserData)
{
	NetgameSetupData *DPtr = (NetgameSetupData *)(UserData);
	NetgameSetupData& Data = *DPtr;
	
	switch(Ctrl.ID.id)
	{
	case iRADIO_GROUP_DURATION:
		switch(GetControl32BitValue(Ctrl.Ctrl))
		{
		case duration_no_time_limit:
			NetgameSetup_Untimed(Data);
			break;
			
		case duration_time_limit:
			NetgameSetup_Timed(Data);
			break;
			
		case duration_kill_limit:
			NetgameSetup_ScoreLimit(Data);
			break;
		}
		break;
		
	case iFORCE_UNIQUE_TEAMS:
		SetControlActivity(Data.PlayerTeamCtrl, GetControl32BitValue(Ctrl.Ctrl));

	case iGAME_TYPE:
		{
			short new_game_type= GetControl32BitValue(Ctrl.Ctrl)-1;
			
			if(new_game_type != Data.game_information->net_game_type)
			{
				long entry_flags, old_entry_flags;
				struct entry_point entry;
				
				if(Data.allow_all_levels)
				{
					entry_flags= old_entry_flags= NONE;
				} else {
					old_entry_flags= get_entry_point_flags_for_game_type(Data.game_information->net_game_type);
					entry_flags= get_entry_point_flags_for_game_type(new_game_type);
				}

				menu_index_to_level_entry(GetControl32BitValue(Data.EntryPointCtrl), old_entry_flags, &entry);
				
				/* Get the old one and reset.. */
				EntryPoints_FillIn(Data.EntryPointCtrl, entry_flags, entry.level_number);
				Data.game_information->net_game_type= new_game_type;
				
				NetgameSetup_GameType(Data, new_game_type);
			}
		}
		break;
	
	case iOK_SPECIAL:
		// Verify whether it's OK to exit
		{
			bool information_is_acceptable = false;
			// START Benad
			if (GetControl32BitValue(Data.GameTypeCtrl) - 1 == _game_of_defense)
			{
				information_is_acceptable =
					CheckSetupInformation(Data, duration_time_limit) &&
					CheckSetupInformation(Data, duration_kill_limit);
			}
			else
			{
				short game_limit_type= GetControl32BitValue(Data.DurationCtrl);
			
				information_is_acceptable= CheckSetupInformation(Data, game_limit_type);
			}
			// END Benad
			if (information_is_acceptable)
			{
				Data.IsOK = true;
				StopModalDialog(ActiveNonFloatingWindow(),false);
			}
		}
	}
}


bool network_game_setup(
	player_info *player_information,
	game_info *game_information,
        bool ResumingGame)
{
	bool allow_all_levels= key_is_down(OPTION_KEYCODE);
	
	AutoNibWindow Window(GUI_Nib, Window_Network_Setup);
	
	NetgameSetupData Data;
	
	Data.PlayerNameCtrl = GetCtrlFromWindow(Window(), 0, iGATHER_NAME);
	Data.PlayerColorCtrl = GetCtrlFromWindow(Window(), 0, iGATHER_COLOR);
	Data.PlayerTeamCtrl = GetCtrlFromWindow(Window(), 0, iGATHER_TEAM);
	
	Data.EntryPointCtrl = GetCtrlFromWindow(Window(), 0, iENTRY_MENU);
	Data.GameTypeCtrl = GetCtrlFromWindow(Window(), 0, iGAME_TYPE);
	Data.DifficultyCtrl = GetCtrlFromWindow(Window(), 0, iDIFFICULTY_MENU);
	
	Data.MonstersCtrl = GetCtrlFromWindow(Window(), 0, iUNLIMITED_MONSTERS);
	Data.NoMotionSensorCtrl = GetCtrlFromWindow(Window(), 0, iMOTION_SENSOR_DISABLED);
	Data.BadDyingCtrl = GetCtrlFromWindow(Window(), 0, iDYING_PUNISHED);
	Data.BadSuicideCtrl = GetCtrlFromWindow(Window(), 0, iSUICIDE_PUNISHED);
	Data.UniqTeamsCtrl = GetCtrlFromWindow(Window(), 0, iFORCE_UNIQUE_TEAMS);
	Data.BurnItemsCtrl = GetCtrlFromWindow(Window(), 0, iBURN_ITEMS_ON_DEATH);
	Data.StatsReportingCtrl = GetCtrlFromWindow(Window(), 0, iREALTIME_NET_STATS);
	
	Data.DurationCtrl = GetCtrlFromWindow(Window(), 0, iRADIO_GROUP_DURATION);
	Data.TimeLabelCtrl = GetCtrlFromWindow(Window(), 0, iTEXT_TIME_LIMIT);
	Data.TimeTextCtrl = GetCtrlFromWindow(Window(), 0, iTIME_LIMIT);
	Data.KillsLabelCtrl = GetCtrlFromWindow(Window(), 0, iTEXT_KILL_LIMIT);
	Data.KillsTextCtrl = GetCtrlFromWindow(Window(), 0, iKILL_LIMIT);
	
	Data.UseMicrophoneCtrl = GetCtrlFromWindow(Window(), 0, iREAL_TIME_SOUND);
	Data.MicrophoneTypeCtrl = GetCtrlFromWindow(Window(), 0, iMICROPHONE_TYPE);
	
	Data.OK_Ctrl = GetCtrlFromWindow(Window(), 0, iOK_SPECIAL);
	
	Data.game_information = game_information;
	Data.allow_all_levels = allow_all_levels;
	
	Data.IsOK = false;
	
	AutoKeyboardWatcher Watcher(Setup_PlayerNameWatcher);
	
	Watcher.Watch(Data.PlayerNameCtrl, &Data);
	
	game_information->net_game_type =
		NetgameSetup_FillIn(Data, player_information, allow_all_levels, ResumingGame);

	bool IsOK = RunModalDialog(Window(), false, NetgameSetup_Handler, &Data);
	IsOK = Data.IsOK;
	
	if (IsOK)
	{
		short game_limit_type= GetControl32BitValue(Data.DurationCtrl) - 1;
		
		NetgameSetup_Extract(Data, player_information, game_information,
			game_limit_type, allow_all_levels, ResumingGame);
	}
	
	return IsOK;
}

#else

bool network_game_setup(
	player_info *player_information,
	game_info *game_information,
        bool inResumingGame)
{
	short item_hit;
	GrafPtr old_port;
	bool information_is_acceptable;
	DialogPtr dialog;
	ModalFilterUPP game_setup_filter_upp;
	bool allow_all_levels= key_is_down(OPTION_KEYCODE);

	dialog= myGetNewDialog(dlogGAME_SETUP, NULL, (WindowPtr) -1, refNETWORK_SETUP_DIALOG);
	assert(dialog);
	game_setup_filter_upp= NewModalFilterUPP(game_setup_filter_proc);
	assert(game_setup_filter_upp);
	GetPort(&old_port);
	SetPort(GetWindowPort(GetDialogWindow(dialog)));

	game_information->net_game_type= fill_in_game_setup_dialog(dialog, player_information, allow_all_levels, inResumingGame);

	ShowWindow(GetDialogWindow(dialog));

	do
	{
		do
		{
			ModalDialog(game_setup_filter_upp, &item_hit);
			
			SetPort(GetWindowPort(GetDialogWindow(dialog)));
			
			switch (item_hit)
			{
				case iRADIO_NO_TIME_LIMIT:
					setup_for_untimed_game(dialog);
					break;
					
				case iRADIO_TIME_LIMIT:
					setup_for_timed_game(dialog);
					break;
					
				case iRADIO_KILL_LIMIT:
					setup_for_score_limited_game(dialog);
					break;

				case iFORCE_UNIQUE_TEAMS:
					modify_control(dialog, item_hit, NONE, !get_dialog_control_value(dialog, item_hit));
					if(!get_dialog_control_value(dialog, item_hit))
					{
						modify_control(dialog, iGATHER_TEAM, CONTROL_INACTIVE, NONE);
					} else {
						modify_control(dialog, iGATHER_TEAM, CONTROL_ACTIVE, NONE);
					}
					break;

				case iUNLIMITED_MONSTERS:
				case iMOTION_SENSOR_DISABLED:
				case iDYING_PUNISHED:
				case iSUICIDE_PUNISHED:
				case iBURN_ITEMS_ON_DEATH:
				case iREALTIME_NET_STATS:
					modify_control(dialog, item_hit, NONE, !get_dialog_control_value(dialog, item_hit));
					break;

				case iGAME_TYPE:
					{
						short new_game_type;
						
						new_game_type= get_dialog_control_value(dialog, iGAME_TYPE)-1;
						
						if(new_game_type != game_information->net_game_type)
						{
							long entry_flags, old_entry_flags;
							struct entry_point entry;

							if(allow_all_levels)
							{
								entry_flags= old_entry_flags= NONE;
							} else {
								old_entry_flags= get_entry_point_flags_for_game_type(game_information->net_game_type);
								entry_flags= get_entry_point_flags_for_game_type(new_game_type);
							}

							menu_index_to_level_entry(get_dialog_control_value(dialog, iENTRY_MENU), old_entry_flags, &entry);
							
							/* Get the old one and reset.. */
							fill_in_entry_points(dialog, iENTRY_MENU, entry_flags, entry.level_number);
							game_information->net_game_type= new_game_type;

							setup_dialog_for_game_type(dialog, new_game_type);
						}
					}
					break;

				case iREAL_TIME_SOUND:
					modify_control(dialog, iREAL_TIME_SOUND, NONE, !get_dialog_control_value(dialog, iREAL_TIME_SOUND));
					break;
			}
		} while (item_hit != iOK && item_hit != iCANCEL);
		
		if (item_hit==iCANCEL)
		{
			information_is_acceptable= true;
		}
		else
		{
			// START Benad
			if (get_dialog_control_value(dialog, iGAME_TYPE)-1 == _game_of_defense)
			{
				information_is_acceptable = (check_setup_information(dialog, iRADIO_TIME_LIMIT) &&
					check_setup_information(dialog, iRADIO_KILL_LIMIT));
			}
			else
			{
				short game_limit_type= get_game_duration_radio(dialog);
			
				information_is_acceptable= check_setup_information(dialog, game_limit_type);
			}
			// END Benad
		}
	} while (!information_is_acceptable);	

	if (item_hit == iOK)
	{
		short game_limit_type= get_game_duration_radio(dialog);
			
		extract_setup_dialog_information(dialog, player_information, game_information, 
			game_limit_type, allow_all_levels, inResumingGame);
	}
	
	SetPort(old_port);
	DisposeModalFilterUPP(game_setup_filter_upp);
	DisposeDialog(dialog);	

	return (item_hit==iOK);
}

#endif


/*************************************************************************************************
 *
 * Function: fill_in_game_setup_dialog
 * Purpose:  setup the majority of the game setup dialog.
 *
 *************************************************************************************************/
// ZZZ: moved this function to shared network_dialogs.cpp


// ZZZ: new function (has different implementation on SDL)
void set_limit_type(DialogPtr dialog, short limit_type) {
	modify_radio_button_family(dialog, iRADIO_NO_TIME_LIMIT, iRADIO_KILL_LIMIT, limit_type);
}


static short get_game_duration_radio(
	DialogPtr dialog)
{
	short items[]= {iRADIO_NO_TIME_LIMIT, iRADIO_TIME_LIMIT, iRADIO_KILL_LIMIT};
	unsigned short index, item_hit;
	
	for(index= 0; index<sizeof(items)/sizeof(items[0]); ++index)
	{
		short item_type;
		ControlHandle control;
		Rect bounds;
	
		GetDialogItem(dialog, items[index], &item_type, (Handle *) &control, &bounds);
		if(GetControlValue(control)) 
		{
			item_hit= items[index];
			break;
		}
	}
	
	assert(index!=sizeof(items)/sizeof(items[0]));
	
	return item_hit;
}


/*************************************************************************************************
 *
 * Function: extract_setup_dialog_information
 * Purpose:  extract all the information that we can squeeze out of the game setup dialog
 *
 *************************************************************************************************/
// ZZZ: moved this function to shared network_dialogs.cpp


/*************************************************************************************************
 *
 * Function: set_dialog_game_options
 * Purpose:  setup the game dialog's radio buttons given the game option flags.
 *
 *************************************************************************************************/
// ZZZ: moved this function to shared network_dialogs.cpp


#ifdef USES_NIBS

struct EntryPointMenuData
{
	long entry_flags;
	short level_index;
	short default_level;
};


// Cribbed from interface_macintosh.h : LevelNumberMenuBuilder()
static bool EntryPointMenuBuilder(
	int indx, Str255 ItemName, bool &ThisIsInitial, void *Data)
{
	EntryPointMenuData *MDPtr = (EntryPointMenuData *)(Data);
	entry_point entry;
		
	bool UseThis = get_indexed_entry_point(
		&entry, &MDPtr->level_index, MDPtr->entry_flags);
	
	ThisIsInitial = (MDPtr->level_index == MDPtr->default_level);
	
	if (UseThis)
		psprintf(ItemName, "%s",entry.level_name);
	
	return UseThis;
}


void EntryPoints_FillIn(
	ControlRef EntryPointCtrl,
	long entry_flags,
	short default_level
	)
{
	EntryPointMenuData MenuData;
	MenuData.entry_flags = entry_flags;
	MenuData.level_index = 0;
	MenuData.default_level = default_level;
	
	BuildMenu(EntryPointCtrl, EntryPointMenuBuilder, &MenuData);
}

#else

// ZZZ: exposed this function
void fill_in_entry_points(
	DialogPtr dialog,
	short item,
	long entry_flags,
	short default_level)
{
	short item_type;
	Rect bounds;
	ControlHandle control;
	MenuHandle menu;
	short map_index, menu_index;
	struct entry_point entry;
	short default_item= NONE;

	/* Add the maps.. */
	GetDialogItem(dialog, item, &item_type, (Handle *) &control, &bounds);
	menu= get_popup_menu_handle(dialog, item);

	/* Nix the maps */
	while(CountMenuItems(menu))
	{
		DeleteMenuItem(menu, 1);
	}
	
	map_index= menu_index= 0;
	while (get_indexed_entry_point(&entry, &map_index, entry_flags))
	{
		AppendMenu(menu, "\p ");
		CopyCStringToPascal(entry.level_name, ptemporary);
		menu_index++;
		if(entry.level_name[0])
		{
			SetMenuItemText(menu, menu_index, ptemporary);
		}

		if(entry.level_number==default_level) 
		{
			default_item= menu_index;
		}
	}
	SetControlMaximum(control, menu_index);
	
	if(default_item != NONE)
	{
		SetControlValue(control, default_item);	
	} 
	else if(GetControlValue(control)>=GetControlMaximum(control)) 
	{	
		SetControlValue(control, 1);
	}

	if (!CountMenuItems(menu)) modify_control(dialog, iOK, CONTROL_INACTIVE, 0);

	return;
}

#endif

// ZZZ: new function
void select_entry_point(DialogPtr inDialog, short inItem, int16 inLevelNumber)
{
	modify_selection_control(inDialog, inItem, CONTROL_ACTIVE, inLevelNumber+1);
}


/*************************************************************************************************
 *
 * Function: check_setup_information
 * Purpose:  check to make sure that the user entered usable information in the dialog.
 *
 *************************************************************************************************/

#ifdef USES_NIBS

bool CheckSetupInformation(
	NetgameSetupData& Data, 
	short game_limit_type)
{
	bool information_is_acceptable = true;
	long limit;
	
	if (information_is_acceptable)
	{
		GetEditCText(Data.TimeTextCtrl, temporary);
		information_is_acceptable = (sscanf(temporary, "%hd", &limit) >= 1);
	}
	
	if (information_is_acceptable)
	{
		if (game_limit_type == duration_time_limit && limit <= 0)
			information_is_acceptable = false;
	}
	
	if (information_is_acceptable)
	{
		GetEditCText(Data.KillsTextCtrl, temporary);
		information_is_acceptable = (sscanf(temporary, "%hd", &limit) >= 1);
	}
		
	if (information_is_acceptable)
	{
		if (game_limit_type == duration_kill_limit && limit <= 0)
			information_is_acceptable = false;
	}
	
	if (information_is_acceptable)
	{
		GetEditPascalText(Data.PlayerNameCtrl, ptemporary);
		if (ptemporary[0] == 0)
			information_is_acceptable = true;
	}
	
	if (!information_is_acceptable)
	{
		SysBeep(30);
	}

	return information_is_acceptable;
}


#else

bool check_setup_information(
	DialogPtr dialog, 
	short game_limit_type)
{
	Rect     item_rect;
	short    item_type;
	short    limit;
	short    bad_item;
	Handle   item_handle;
	bool  information_is_acceptable;
	
	limit = extract_number_from_text_item(dialog, iTIME_LIMIT);
	if (game_limit_type == iRADIO_TIME_LIMIT && limit <= 0)
	{
		bad_item = iTIME_LIMIT;
		information_is_acceptable = false;
	}
	else
	{
		limit = extract_number_from_text_item(dialog, iKILL_LIMIT);
		if (game_limit_type == iRADIO_KILL_LIMIT && limit <= 0)
		{
			bad_item = iKILL_LIMIT;
			information_is_acceptable = false;
		}
		else
		{
			GetDialogItem(dialog, iGATHER_NAME, &item_type, &item_handle, &item_rect);
			GetDialogItemText(item_handle, ptemporary);
			if (*temporary == 0)
			{
				bad_item = iGATHER_NAME;
				information_is_acceptable = false;
			}
			else
				information_is_acceptable = true;
		}
	}
	
	if (!information_is_acceptable)
	{
		SysBeep(3);
		SelectDialogItemText(dialog, bad_item, 0, INT16_MAX);
	}

	return information_is_acceptable;
}

#endif

/*************************************************************************************************
 *
 * Function: get_dialog_control_value
 * Purpose:  given a dialog and an item number, extract the value of the control
 *
 *************************************************************************************************/
// ZZZ: moved this function to csdialogs_macintosh.cpp, and made one like it for csdialogs_sdl.cpp

// JTP: Routines initially copied from network_dialogs_sdl.cpp

#warning SDL Autogather unhandled - Non critical
#if 0
static void
autogather_callback(w_select* inAutoGather) {
	if(inAutoGather->get_selection() > 0) {
		dialog* theDialog = inAutoGather->get_owning_dialog();
		assert(theDialog != NULL);
		
		w_found_players* theFoundPlayers = dynamic_cast<w_found_players*>(theDialog->get_widget_by_id(iNETWORK_LIST_BOX));
		assert(theFoundPlayers != NULL);
		
		theFoundPlayers->callback_on_all_items();
	}
}
#endif

#ifdef USES_NIBS

static void
found_player_callback(const SSLP_ServiceInstance* player)
{
	NetgameGatherData& Data = *GatherDataPtr;

	DataBrowserItemID ItemID = Data.ItemID++;	// Get current value, then move to next one
	Data.FoundPlayers[ItemID] = player;
	Data.ReverseFoundPlayers[player] = ItemID;
	
	AddDataBrowserItems(Data.NetworkDisplayCtrl,
		kDataBrowserNoItem,
		1, &ItemID,
		NULL
		);
}


static void
lost_player_callback(const SSLP_ServiceInstance* player)
{
	NetgameGatherData& Data = *GatherDataPtr;

	map<const SSLP_ServiceInstance*, DataBrowserItemID>::iterator Entry =
		Data.ReverseFoundPlayers.find(player);
	
	// Could the player entry be found?
	if (Entry == Data.ReverseFoundPlayers.end()) return;

	DataBrowserItemID ItemID = Entry->second;
	Data.FoundPlayers.erase(ItemID);
	Data.ReverseFoundPlayers.erase(Entry);
	
	RemoveDataBrowserItems(Data.NetworkDisplayCtrl,
		kDataBrowserNoItem,
		1, &ItemID,
		NULL
		);
}

static void
player_name_changed_callback(const SSLP_ServiceInstance* player)
{
	NetgameGatherData& Data = *GatherDataPtr;
	
	map<const SSLP_ServiceInstance*, DataBrowserItemID>::iterator Entry =
		Data.ReverseFoundPlayers.find(player);

	// Could the player entry be found?
	if (Entry == Data.ReverseFoundPlayers.end()) return;
	
	DataBrowserItemID ItemID = Entry->second;
		
	UpdateDataBrowserItems(Data.NetworkDisplayCtrl,
		kDataBrowserNoItem,
		1, &ItemID,
		NULL, NULL
		);
}

#else

static void
found_player_callback(const SSLP_ServiceInstance* player)
{
	Cell cell;
	ListBounds bounds;
	short theIndex;
	
	found_players.push_back(player);
	
#if TARGET_API_MAC_CARBON
	GetListDataBounds(network_list_box, &bounds);
#else
	bounds = (*network_list_box)->dataBounds;
#endif
	theIndex = bounds.bottom + 1;
	LAddRow(1, theIndex, network_list_box);
	SetPt(&cell, 0, theIndex - 1);

	LSetCell(&(player->sslps_name[1]), player->sslps_name[0], cell, network_list_box);

#if LIST_BOX_AS_CONTROL
	WindowRef window = ActiveNonFloatingWindow();
	Rect portBounds;
	GetPortBounds(GetWindowPort(window), &portBounds);
	InvalWindowRect(window, &portBounds);
#endif
}

static void
lost_player_callback(const SSLP_ServiceInstance* player) {
#warning Potential for off by one error in this code
	int theIndex = find_item_index_in_vector(player, found_players);
	
	if(theIndex == -1)
		return;	// didn't know about it anyway
	
	// Axe it
	found_players.erase(found_players.begin() + theIndex);
	
	// Remove from either hidden list or displayed list.
	LDelRow(1, theIndex, network_list_box);

#if LIST_BOX_AS_CONTROL
	WindowRef window = ActiveNonFloatingWindow();
	Rect bounds;
	GetPortBounds(GetWindowPort(window), &bounds);
	InvalWindowRect(window, &bounds);
#endif
}

static void
player_name_changed_callback(const SSLP_ServiceInstance* player) {
#warning Potential for off by one error in this code
	Cell cell;
	int theIndex = find_item_index_in_vector(player, found_players);
	
	if(theIndex == -1)
		return;	// didn't know about it anyway

	SetPt(&cell, 0, theIndex);

	LSetCell(&(player->sslps_name[1]), player->sslps_name[0], cell, network_list_box);
#if LIST_BOX_AS_CONTROL
	WindowRef window = ActiveNonFloatingWindow();
	Rect bounds;
	GetPortBounds(GetWindowPort(window), &bounds);
	InvalWindowRect(window, &bounds);
#endif
}

#endif

/*************************************************************************************************
 *
 * Function: gather_dialog_filter_proc
 * Purpose:  the dialog filter procedure passed to ModalDialog() for the gathering dialog
 *
 *************************************************************************************************/
static pascal Boolean gather_dialog_filter_proc(
	DialogPtr dialog,
	EventRecord *event,
	short *item_hit)
{
#if !LIST_BOX_AS_CONTROL			
	short item_type, charcode;
	Handle item_handle;
	Rect item_rectangle;
	Point where;
	bool cell_is_selected;
	short cell_count;
#endif
	GrafPtr old_port;
	bool handled;
	Cell selected_cell;

	/* preprocess events */	
	handled= false;
	GetPort(&old_port);
	SetPort(GetWindowPort(GetDialogWindow(dialog)));

	/* update the names list box; if we don�t have a selection afterwords, dim the ADD button */

	// NetLookupUpdate unused - as long as SSLP_Pump() is called, e.g. by the dialog, its unnecessary 
	// Let SSLP do its thing (respond to FIND messages, hint out HAVE messages, etc.)
	SSLP_Pump();

	SetPt(&selected_cell, 0, 0);
	if (!LGetSelect(true, &selected_cell, network_list_box)) modify_control(dialog, iADD, CONTROL_INACTIVE, 0);

	switch(event->what)
	{
#if !LIST_BOX_AS_CONTROL			
		case mouseDown:
			/* get the mouse in local coordinates */
			where= event->where;
			GlobalToLocal(&where);
			
			/* check for clicks in the list box */
			GetDialogItem(dialog, iNETWORK_LIST_BOX, &item_type, &item_handle, &item_rectangle);
			if (PtInRect(where, &item_rectangle))
			{
#if TARGET_API_MAC_CARBON 
				CGrafPtr port = GetWindowPort(GetDialogWindow(dialog));
				SInt16   pixelDepth = (*GetPortPixMap(port))->pixelSize;
			
				ThemeDrawingState savedState;
				GetThemeDrawingState(&savedState);
				SetThemeBackground(kThemeBrushListViewBackground, pixelDepth, pixelDepth > 1);
#endif
				if (LClick(where, event->modifiers, network_list_box))
				{
					GetDialogItem(dialog, iADD, &item_type, &item_handle, &item_rectangle);
					if (hit_dialog_button(dialog, iADD)) *item_hit= iADD;
				}
				
				handled= true;
				
#if TARGET_API_MAC_CARBON
				SetThemeDrawingState(savedState, true);
#endif
			}

			/* check for clicks in the zone popup menu */
			break;
#endif

#if !LIST_BOX_AS_CONTROL
		case updateEvt:
			if ((WindowPtr)event->message==GetDialogWindow(dialog))
			{
				/* update the zone popup menu */
				
				/* update the network list box and it�s frame */
#if TARGET_API_MAC_CARBON
				GetDialogItem(dialog, iNETWORK_LIST_BOX, &item_type, &item_handle, &item_rectangle);
				InsetRect(&item_rectangle, -1, -1);
				item_rectangle.right --;

				CGrafPtr port = GetWindowPort(GetDialogWindow(dialog));
				SInt16   pixelDepth = (*GetPortPixMap(port))->pixelSize;
			
				ThemeDrawingState savedState;
				ThemeDrawState curState =
					IsWindowActive(GetDialogWindow(dialog))?kThemeStateActive:kThemeStateInactive;
			
				GetThemeDrawingState(&savedState);
				SetThemeBackground(kThemeBrushListViewBackground, pixelDepth, pixelDepth > 1);
				EraseRect(&item_rectangle);
#endif

				RgnHandle visRgn = NewRgn();
				GetPortVisibleRegion(GetWindowPort(GetDialogWindow(dialog)), visRgn);
				LUpdate(visRgn, network_list_box);
				DisposeRgn(visRgn);
//#if TARGET_API_MAC_CARBON
				DrawThemePrimaryGroup(&item_rectangle, curState);
				SetThemeDrawingState(savedState, true);
/*
#else
				GetDialogItem(dialog, iNETWORK_LIST_BOX, &item_type, &item_handle, &item_rectangle);
				InsetRect(&item_rectangle, -1, -1);
				FrameRect(&item_rectangle);
#endif
*/
				/* update the player area */
				update_player_list_item(dialog, iPLAYER_DISPLAY_AREA);
			}
			break;
#endif
#if !LIST_BOX_AS_CONTROL
		case keyDown:
			charcode = event->message & charCodeMask;

			cell_count = (**network_list_box).dataBounds.bottom - (**network_list_box).dataBounds.top;		
			SetPt(&selected_cell, 0, 0);
			cell_is_selected = LGetSelect(true, &selected_cell, network_list_box);

			if (cell_count)
			{
				*item_hit = iNETWORK_LIST_BOX; // for ADD button to be updated.
				switch(charcode)
				{
					case kUP_ARROW:
						if (cell_is_selected)
						{
							if (selected_cell.v > 0)
							{
								LSetSelect(false, selected_cell, network_list_box);
								selected_cell.v--;
								LSetSelect(true, selected_cell, network_list_box);
								LAutoScroll(network_list_box);
							}
						}
						else
						{
							SetPt(&selected_cell, 0, cell_count-1);
							LSetSelect(true, selected_cell, network_list_box);
							LAutoScroll(network_list_box);
						}
						*item_hit = iNETWORK_LIST_BOX; // for ADD button to be updated.
						handled = true;
						break;

					case kDOWN_ARROW:
						if (cell_is_selected)
						{
							if (selected_cell.v < cell_count-1)
							{
								LSetSelect(false, selected_cell, network_list_box);
								selected_cell.v++;
								LSetSelect(true, selected_cell, network_list_box);
								LAutoScroll(network_list_box);
							}
						}
						else
						{
							SetPt(&selected_cell, 0, 0);
							LSetSelect(true, selected_cell, network_list_box);
							LAutoScroll(network_list_box);
						}
						*item_hit = iNETWORK_LIST_BOX; // for ADD button to be updated.
						handled = true;
						break;
				}
			}
			break;
#endif
	}

	/* give the player area time (for animation, etc.) */

	/* check and see if we�ve gotten any connection requests */	
	
	SetPort(old_port);

	return handled ? true : general_filter_proc(dialog, event, item_hit);
}

/*************************************************************************************************
 *
 * Function: join_dialog_filter_proc
 * Purpose:  the dialog filter procedure passed to ModalDialog() for the joining dialog
 *
 *************************************************************************************************/
static pascal Boolean join_dialog_filter_proc(
	DialogPtr dialog,
	EventRecord *event,
	short *item_hit)
{
	Rect     item_rect;
	short    join_state;
	short    item_type;
	Handle   item_handle;
	bool  handled= false;
	GrafPtr  old_port;
	static short last_join_state;

	// Let SSLP do its thing (respond to FIND messages, hint out HAVE messages, etc.)
	SSLP_Pump();

	/* preprocess events */	
	GetPort(&old_port);
	SetPort(GetWindowPort(GetDialogWindow(dialog)));

	/* give the player area time (for animation, etc.) */

	/* check and see if we�ve gotten any connection requests */
	join_state= NetUpdateJoinState();
	switch (join_state)
	{
		case NONE: // haven't Joined yet.
			break;

		case netJoining:
			break;

		case netCancelled: /* the server cancelled the game; force bail */
			*item_hit= iCANCEL;
			handled= true;
			break;

		case netWaiting: /* if we just changed netJoining to netWaiting change the dialog text */
#ifdef OBSOLETE
			if (last_join_state==netJoining)
			{
				GetDialogItem(dialog, iJOIN_MESSAGES, &item_type, &item_handle, &item_rect);
				SetDialogItemText(item_handle, getpstr(ptemporary, strJOIN_DIALOG_MESSAGES, _join_dialog_accepted_string));
			}
#endif
			modify_control(dialog, iCANCEL, CONTROL_INACTIVE, 0);
			break;

		case netStartingUp: /* the game is starting up (we have the network topography) */
			sStartJoinedGameResult= kNetworkJoinedNewGame;
			handled= true;
			break;

		case netStartingResumeGame: /* the game is starting up a resume game (we have the network topography) */
			sStartJoinedGameResult= kNetworkJoinedResumeGame;
			handled= true;
			break;

		case netPlayerAdded:
			if(last_join_state==netWaiting)
			{
				char joinMessage[256];
				
				game_info *info= (game_info *)NetGetGameData();

				get_network_joined_message(joinMessage, info->net_game_type);
#if TARGET_API_MAC_CARBON
				CopyCStringToPascal(joinMessage, ptemporary);
#else
				strcpy(temporary, joinMessage); 
				c2pstr(temporary);
#endif
				copy_pstring_to_static_text(dialog, iJOIN_MESSAGES, ptemporary);
			}
			update_player_list_item(dialog, iPLAYER_DISPLAY_AREA);
			break;

		case netJoinErrorOccurred:
			*item_hit= iCANCEL;
			handled= true;
			break;
                
		case netChatMessageReceived:
			{
				player_info*	sending_player;
				char*		chat_message;
				
				if(NetGetMostRecentChatMessage(&sending_player, &chat_message)) {
					// should actually do something with the message here
					// be sure to copy any info you need as the next call to
					// NetUpdateJoinState could overwrite the storage we're pointing at.
				}
			}
			break;

		default:
			// LP change:
			assert(false);
			// halt();
	}
	last_join_state= join_state;

	GetDialogItem(dialog, iJOIN_NAME, &item_type, &item_handle, &item_rect);
	GetDialogItemText(item_handle, ptemporary);
	if (join_state == NONE && *temporary)
		modify_control(dialog, iOK, CONTROL_ACTIVE, NONE);
	else
		modify_control(dialog, iOK, CONTROL_INACTIVE, NONE);

	SetPort(old_port);
	
	return handled ? true : general_filter_proc(dialog, event, item_hit);
}

/*************************************************************************************************
 *
 * Function: game_setup_filter_proc
 * Purpose:  the dialog filter procedure passed to ModalDialog() for the net game setup dialog
 *
 *************************************************************************************************/
static pascal Boolean game_setup_filter_proc(
	DialogPtr dialog,
	EventRecord *event,
	short *item_hit)
{
	Rect     item_rect;
	short    item_type;
	Handle   item_handle;
	GrafPtr  old_port;

	GetPort(&old_port);
	SetPort(GetWindowPort(GetDialogWindow(dialog)));
	GetDialogItem(dialog, iGATHER_NAME, &item_type, &item_handle, &item_rect);
	GetDialogItemText(item_handle, ptemporary);
	if (*temporary)
		modify_control(dialog, iOK, CONTROL_ACTIVE, NONE);
	else
		modify_control(dialog, iOK, CONTROL_INACTIVE, NONE);
	SetPort(old_port);

	return general_filter_proc(dialog, event, item_hit);
}

/*************************************************************************************************
 *
 * Function: setup_network_list_box
 * Purpose:  allocates or clears the list that will list all the players in a zone.
 *
 *************************************************************************************************/
static void setup_network_list_box(
	WindowPtr window,
	Rect *frame,
	unsigned char *zone)
{
	Cell cell;
	
	if (!network_list_box)
	{
		Rect bounds;

		/* allocate the list */

		SetPt(&cell, 0, 0);
		SetRect(&bounds, 0, 0, 1, 0);
	
#if LIST_BOX_AS_CONTROL
		ControlRef control;
		OSStatus err;
		
		GetDialogItemAsControl( GetDialogFromWindow(window), iNETWORK_LIST_BOX, &control );
		err = GetListBoxListHandle( control, &network_list_box );
#else
		Rect adjusted_frame;
		adjusted_frame= *frame;
		adjusted_frame.right-= SCROLLBAR_WIDTH-1;
		network_list_box= LNew(&adjusted_frame, &bounds, cell, 0, window, false, false, false, true);
#endif
		assert(network_list_box);
		LSetDrawingMode(true, network_list_box);	
		(*network_list_box)->selFlags= lOnlyOne;
	}
	else
	{
		/* the list is already allocated; delete all rows and close the existing network name lookup */
		
		LDelRow(0, 0, network_list_box);
		NetLookupClose();
	}
	found_players.clear();

	/* spawn an asynchronous network name lookup */
	NetLookupOpen_SSLP(PLAYER_TYPE, get_network_version(), found_player_callback, lost_player_callback, player_name_changed_callback);

	return;
}

/*************************************************************************************************
 *
 * Function: dispose_network_list_box
 * Purpose:  destroys the list that contains the list of players for a zone.
 *
 *************************************************************************************************/
static void dispose_network_list_box(
	void)
{
	assert(network_list_box);
	
	found_players.clear();
	
	NetLookupClose();

#if !LIST_BOX_AS_CONTROL
	// JTP: Only dispose of it if we created it
	LDispose(network_list_box);
#endif
	network_list_box= (ListHandle) NULL;
	
	return;
}

/*************************************************************************************************
 *
 * Function: update_player_list_item
 * Purpose:
 *
 *************************************************************************************************/
static pascal void update_player_list_item(
	DialogPtr dialog, 
	short item_num)
{
	Rect         item_rect, name_rect;
	short        i, num_players;
	short        item_type;
	short        height;
	Handle       item_handle;
	GrafPtr      old_port;
	FontInfo     finfo;

// LP: the Classic version I've kept theme-less for simplicity
#if TARGET_API_MAC_CARBON
	ThemeDrawingState savedState;
	ThemeDrawState curState =
		IsWindowActive(GetDialogWindow(dialog))?kThemeStateActive:kThemeStateInactive;
#endif

	GetPort(&old_port);
	SetPort(GetWindowPort(GetDialogWindow(dialog)));
	
#if TARGET_API_MAC_CARBON
	GetThemeDrawingState(&savedState);
#endif
	
	GetDialogItem(dialog, item_num, &item_type, &item_handle, &item_rect);
	
#if TARGET_API_MAC_CARBON
	DrawThemePrimaryGroup (&item_rect, curState);
#endif
	
	GetFontInfo(&finfo);
	height = finfo.ascent + finfo.descent + finfo.leading;
	MoveTo(item_rect.left + 3, item_rect.top+height);
	num_players = NetNumberOfPlayerIsValid() ? NetGetNumberOfPlayers() : 0;
	SetRect(&name_rect, item_rect.left, item_rect.top, item_rect.left+NAME_BOX_WIDTH, item_rect.top+NAME_BOX_HEIGHT);
	for (i = 0; i < num_players; i++)
	{
		draw_player_box_with_team(&name_rect, i);
		if (!(i % 2))
		{
			OffsetRect(&name_rect, NAME_BOX_WIDTH+BOX_SPACING, 0);
		}
		else
		{
			OffsetRect(&name_rect, -(NAME_BOX_WIDTH+BOX_SPACING), NAME_BOX_HEIGHT + BOX_SPACING);
		}
	}
	
#if TARGET_API_MAC_CARBON
	SetThemeDrawingState(savedState, true);
#endif
	
	SetPort(old_port);
}

static void calculate_box_colors(
	short color_index,
	RGBColor *highlight_color,
	RGBColor *bar_color,
	RGBColor *shadow_color)
{
	_get_player_color(color_index, highlight_color);

	bar_color->red = (highlight_color->red * 7) / 10;
	bar_color->blue = (highlight_color->blue * 7) / 10;
	bar_color->green = (highlight_color->green * 7) / 10;
	
	shadow_color->red = (highlight_color->red * 2) / 10;
	shadow_color->blue = (highlight_color->blue * 2) / 10;
	shadow_color->green = (highlight_color->green * 2) / 10;
}

/*************************************************************************************************
 *
 * Function: reassign_player_colors
 * Purpose:  This function used to reassign a player's color if it conflicted with another
 *           player's color. Now it reassigns everyone's colors. for the old function, see the
 *           obsoleted version (called check_player_info) at the end of this file.
 *
 *************************************************************************************************/
/* Note that we now only force unique colors across teams. */

// ZZZ: moved this function to network.cpp so it can be shared between Mac and SDL versions.



/*************************************************************************************************
 *
 * Function: menu_index_to_level_entry
 * Purpose:
 *
 *************************************************************************************************/
// ZZZ: exposed this function
void menu_index_to_level_entry(
	short menu_index, 
	long entry_flags,
	struct entry_point *entry)
{
	short  i, map_index;

#if !TARGET_API_MAC_CARBON
	// JTP: Aqua will put up a watch if we take too long
	SetCursor(*GetCursor(watchCursor));
#endif

	map_index= 0;
	for (i= 0; i<menu_index; i++)
	{
		get_indexed_entry_point(entry, &map_index, entry_flags);
	}
	
#if !TARGET_API_MAC_CARBON
	SetCursor(&qd.arrow);
#endif
	return;
}

static MenuHandle get_popup_menu_handle(
	DialogPtr dialog,
	short item)
{
	MenuHandle menu;
	short item_type;
	ControlHandle control;
	Rect bounds;

	/* Add the maps.. */
	GetDialogItem(dialog, item, &item_type, (Handle *) &control, &bounds);

	menu= GetControlPopupMenuHandle(control);
	assert(menu);

	return menu;
}

#ifdef TEST_NET_STATS_DIALOG
static void fake_initialize_stat_data(void)
{
	short i, j;
	
	for (i = 0; i < MAXIMUM_NUMBER_OF_PLAYERS; i++)
	{
		(players+i)->monster_damage_taken.damage = abs(Random()%200);
		(players+i)->monster_damage_taken.kills = abs(Random()%30);
		(players+i)->monster_damage_given.damage = abs(Random()%200);
		(players+i)->monster_damage_given.kills = abs(Random()%30);
		
		(players+i)->netgame_parameters[0] = abs(Random()%5000000);
		(players+i)->netgame_parameters[1] = abs(Random()%6);
		
		for (j = 0; j < MAXIMUM_NUMBER_OF_PLAYERS; j++)
		{
			(players+i)->damage_taken[j].damage = abs(Random()%200);
			(players+i)->damage_taken[j].kills = abs(Random()%6);
		}
	}
}
#endif

// ZZZ: new function used by setup_dialog_for_game_type
void set_limit_text(DialogPtr dialog, short radio_item, short radio_stringset_id, short radio_string_index,
	short units_item, short units_stringset_id, short units_string_index)
{
	Handle item;
	short item_type;
	Rect bounds;

	GetDialogItem(dialog, radio_item, &item_type, &item, &bounds);
	getpstr(ptemporary, radio_stringset_id, radio_string_index);
	SetControlTitle((ControlHandle) item, ptemporary);
	
	GetDialogItem(dialog, units_item, &item_type, &item, &bounds);
	getpstr(ptemporary, units_stringset_id, units_string_index);
	SetDialogItemText(item, ptemporary);
}

/* For join & gather dialogs. */
static void draw_player_box_with_team(
	Rect *rectangle, 
	short player_index)
{
	player_info *player= (player_info *) NetGetPlayerData(player_index);
	RGBColor highlight_color, bar_color, shadow_color;
	Rect team_badge, color_badge, text_box;
	RGBColor old_color;
	short index;

	/* Save the color */
	GetForeColor(&old_color);

#define TEAM_BADGE_WIDTH 16	
	/* Setup the rectangles.. */
	team_badge= color_badge= *rectangle;
	team_badge.right= team_badge.left+TEAM_BADGE_WIDTH;
	color_badge.left= team_badge.right;

	/* Determine the colors */
	calculate_box_colors(player->team, &highlight_color,
		&bar_color, &shadow_color);

	/* Erase the team badge area. */
	RGBForeColor(&bar_color);
	PaintRect(&team_badge);
	
	/* Draw the highlight for this one. */
	RGBForeColor(&highlight_color);
	for (index = 0; index < NAME_BEVEL_SIZE; index++)
	{
		MoveTo(team_badge.left+index, team_badge.bottom-index);
		LineTo(team_badge.left+index, team_badge.top+index);
		LineTo(team_badge.right, team_badge.top+index);
	}
	
	/* Draw the drop shadow.. */
	RGBForeColor(&shadow_color);
	for (index = 0; index < NAME_BEVEL_SIZE; index++)
	{
		MoveTo(team_badge.left+index, team_badge.bottom-index);
		LineTo(team_badge.right, team_badge.bottom-index);
	}

	/* Now draw the player color. */
	calculate_box_colors(player->color, &highlight_color,
		&bar_color, &shadow_color);

	/* Erase the team badge area. */
	RGBForeColor(&bar_color);
	PaintRect(&color_badge);
	
	/* Draw the highlight for this one. */
	RGBForeColor(&highlight_color);
	for (index = 0; index < NAME_BEVEL_SIZE; index++)
	{
		MoveTo(color_badge.left, color_badge.top+index);
		LineTo(color_badge.right-index, color_badge.top+index);
	}
	
	/* Draw the drop shadow.. */
	RGBForeColor(&shadow_color);
	for (index = 0; index < NAME_BEVEL_SIZE; index++)
	{
		MoveTo(color_badge.left, color_badge.bottom-index);
		LineTo(color_badge.right-index, color_badge.bottom-index);
		LineTo(color_badge.right-index, color_badge.top+index);
	}

	/* Finally, draw the name. */
	text_box= *rectangle;
	InsetRect(&text_box, NAME_BEVEL_SIZE, NAME_BEVEL_SIZE);
	CopyPascalStringToC(player->name, temporary);
	_draw_screen_text(temporary, (screen_rectangle *) &text_box, 
		_center_horizontal|_center_vertical, _net_stats_font, _white_color);		

	/* Restore the color */
	RGBForeColor(&old_color);
}


/* -------------------------- Statics for PostGame Carnage Report (redone) (sorta) */

// #include "network_games.h"

/* This function is used elsewhere */
//static void draw_beveled_text_box(bool inset, Rect *box, short bevel_size, 
//	RGBColor *brightest_color, char *text, short flags, bool name_box);

/* ------------ constants */
#define KILL_BAR_HEIGHT   21
#define DEATH_BAR_HEIGHT  14
#define GRAPH_LEFT_INSET  10
#define GRAPH_RIGHT_INSET 40
#define GRAPH_TOP_INSET   20
#define GRAPH_BAR_SPACING  5
#define RANKING_INSET      7
#define DEATH_BEVEL_SIZE   3
#define KILL_BEVEL_SIZE    4

/* ---------------------- globals */


/* ---------------------- prototypes */
static pascal Boolean display_net_stats_proc(DialogPtr dialog, EventRecord *event, short *item_hit);
static void update_damage_item(WindowPtr dialog);
static pascal void update_damage_item_proc(DialogPtr dialog, short item_num);
static short create_graph_popup_menu(DialogPtr dialog, short item);
/*
static void draw_names(DialogPtr dialog, struct net_rank *ranks, short number_of_bars,
	short which_player);
static void draw_player_graph(DialogPtr dialog, short index);
static void get_net_color(short index, RGBColor *color);
static void draw_kill_bars(DialogPtr dialog, struct net_rank *ranks, short num_players, 
	short suicide_index, bool do_totals, bool friendly_fire);
static short calculate_max_kills(short num_players);
*/
static void draw_beveled_box(bool inset, Rect *box, short bevel_size, RGBColor *brightest_color);
/*
static void draw_totals_graph(DialogPtr dialog);
static void draw_team_totals_graph(DialogPtr dialog);
static void draw_total_scores_graph(DialogPtr dialog);
static void draw_team_total_scores_graph(DialogPtr dialog);
*/
#ifdef USES_NIBS
static void calculate_maximum_bar(NetgameOutcomeData &Data, Rect *kill_bar_rect);
#else
static void calculate_maximum_bar(DialogPtr dialog, Rect *kill_bar_rect);
#endif
/*
static void draw_score_bars(DialogPtr dialog, struct net_rank *ranks, short bar_count);
*/
static bool will_new_mode_reorder_dialog(short new_mode, short previous_mode);

/* ---------------- code */

#ifdef USES_NIBS


static void SetupStatsSelectionMenu(ControlRef Ctrl)
{	
	MenuRef graph_popup;
	short index;
	short current_graph_selection;
	bool has_scores;

	/* Clear the graph popup */
	graph_popup= GetControlPopupMenuHandle(Ctrl);
	assert(graph_popup);
	while(CountMenuItems(graph_popup))
		DeleteMenuItem(graph_popup, 1);

	/* Setup the player names */
	for (index= 0; index<dynamic_world->player_count; index++)
	{
		struct player_data *player= get_player_data(rankings[index].player_index);
		
		CopyCStringToPascal(player->name, ptemporary);
		AppendMenu(graph_popup, "\p ");
		SetMenuItemText(graph_popup, index+1, ptemporary); // +1 since it is 1's based
	}
	
	/* Add in the separator line */
	AppendMenu(graph_popup, "\p-");

	/* Add in the total carnage.. */
	AppendMenu(graph_popup, getpstr(ptemporary, strNET_STATS_STRINGS, strTOTALS_STRING));
	current_graph_selection= CountMenuItems(graph_popup);
	
	/* Add in the scores */
	has_scores= get_network_score_text_for_postgame(temporary, false);
	if(has_scores)
	{
		Str255 pscore_temp;
		CopyCStringToPascal(temporary, pscore_temp);
		AppendMenu(graph_popup, pscore_temp);
		current_graph_selection= CountMenuItems(graph_popup);
	}
	
	/* If the game has teams, show the team stats. */
	if (!(dynamic_world->game_information.game_options & _force_unique_teams)) 
	{
		/* Separator line */
		if(has_scores) AppendMenu(graph_popup, "\p-");

		AppendMenu(graph_popup, getpstr(ptemporary, strNET_STATS_STRINGS, strTEAM_TOTALS_STRING));

		if(has_scores)
		{
			get_network_score_text_for_postgame(temporary, true);
			Str255 ppostgame;
			CopyCStringToPascal(temporary, ppostgame);
			AppendMenu(graph_popup, ppostgame);
		}
	}
	
	SetControl32BitMaximum(Ctrl, CountMenuItems(graph_popup));
	SetControl32BitValue(Ctrl, current_graph_selection);
}


static void StatsDisplayDrawer(ControlRef Ctrl, void *UserData)
{
	NetgameOutcomeData *DPtr = (NetgameOutcomeData *)(UserData);
	NetgameOutcomeData &Data = *DPtr;

	// Paint the background
	Rect Bounds;
	GetControlBounds(Data.DisplayCtrl, &Bounds);
	ForeColor(whiteColor);
	PaintRect(&Bounds);

	draw_new_graph(Data);
	
	// Paint the boundary
	ForeColor(blackColor);
	FrameRect(&Bounds);
}


static void NetgameOutcome_Handler(ParsedControl& Ctrl, void *UserData)
{
	NetgameOutcomeData *DPtr = (NetgameOutcomeData *)(UserData);
	NetgameOutcomeData &Data = *DPtr;
	
	short MaxID;
	
	switch(Ctrl.ID.signature)
	{
	case StatsDisplay_Player:
		
		/* Find if they clicked in an area.. */
		switch(find_graph_mode(Data, NULL))
		{
		case _player_graph:
		case _total_carnage_graph:
		case _total_scores_graph:
			MaxID= dynamic_world->player_count;
			break;
			
		case _total_team_carnage_graph:
		case _total_team_scores_graph:
		default:
			MaxID= 0; /* Don't let them click in any of these. (what would you do?) */
			break;
		}
		if (Ctrl.ID.id >= 0 && Ctrl.ID.id < MaxID)	
		{
			if (Ctrl.ID.id != (GetControl32BitValue(Data.SelectCtrl)-1))
			{
				SetControl32BitValue(Data.SelectCtrl, Ctrl.ID.id+1);
				Draw1Control(Data.DisplayCtrl);
			}
		}	
		
		break;
	
	case 0:
		switch(Ctrl.ID.id)
		{
		case iGRAPH_POPUP:
			Draw1Control(Data.DisplayCtrl);
			break;
		}
		break;
	}	
}


void display_net_game_stats()
{
	// eat all stray keypresses
	FlushEvents(everyEvent, 0);
	
	AutoNibWindow Window(GUI_Nib, Window_Network_Outcome);
	
	NetgameOutcomeData Data;
	
	Data.SelectCtrl = GetCtrlFromWindow(Window(), 0, iGRAPH_POPUP);
	
	/* Calculate the rankings (once) for the entire graph */
	calculate_rankings(rankings, dynamic_world->player_count);
	qsort(rankings, dynamic_world->player_count, sizeof(struct net_rank), rank_compare);
	
	SetupStatsSelectionMenu(Data.SelectCtrl);
	
	Data.DisplayCtrl = GetCtrlFromWindow(Window(), 0, iDAMAGE_STATS);
	
	AutoDrawability Drawability;
	Drawability(Data.DisplayCtrl, StatsDisplayDrawer, &Data);
	
	// Overall bounds of stats display
	Rect Bounds;
	GetControlBounds(Data.DisplayCtrl, &Bounds);
	
	// Bounds of each player button in that display
	Rect CtrlBounds;
	SetRect(&CtrlBounds, 0, 0, NAME_BOX_WIDTH, NAME_BOX_HEIGHT);
	OffsetRect(&CtrlBounds, Bounds.left+GRAPH_LEFT_INSET, Bounds.top+GRAPH_TOP_INSET);
	
	AutoHittability Hittability;
	for (int k=0; k<MAXIMUM_NUMBER_OF_PLAYERS; k++)
	{
		OSStatus err;
		
		// Create a button for each player
		err = CreateUserPaneControl(
				Window(), &CtrlBounds,
				0, &Data.PlayerButtonCtrls[k]
				);
		vassert(err == noErr, csprintf(temporary,"CreateUserPaneControl error: %d",err));
		
		// Add ID so that the dialog's hit tester can recognize it
		ControlID ID;
		ID.signature = StatsDisplay_Player;
		ID.id = k;
		
		err = SetControlID(Data.PlayerButtonCtrls[k], &ID);
		vassert(err == noErr, csprintf(temporary,"SetControlID error: %d",err));
		
		// Make it hittable -- clicking on it will create an item-hit event
		Hittability(Data.PlayerButtonCtrls[k]);
		
		// It lives in the stats-display area, of course!
		EmbedControl(Data.PlayerButtonCtrls[k], Data.DisplayCtrl);
		
		// Advance to the next position
		OffsetRect(&CtrlBounds, 0, RECTANGLE_HEIGHT(&CtrlBounds)+GRAPH_BAR_SPACING);	
	}
	
	Data.KillsTextCtrl = GetCtrlFromWindow(Window(), 0, iTOTAL_KILLS);
	Data.DeathsTextCtrl = GetCtrlFromWindow(Window(), 0, iTOTAL_DEATHS);
	
	RunModalDialog(Window(), false, NetgameOutcome_Handler, &Data);
}

#else

void display_net_game_stats(
	void)
{
	Rect item_rect;
	short item_hit, item_type, value, current_graph_selection;
	Handle item_handle;
	GrafPtr old_port;
	DialogPtr dialog;
	ModalFilterUPP stats_dialog_upp;
	UserItemUPP update_damage_item_upp;
	short previous_mode;
	
	// eat all stray keypresses
	{
		EventRecord event;
		
		while (WaitNextEvent(keyDownMask|keyUpMask|autoKeyMask, &event, 0, (RgnHandle) NULL))
			;
	}
	
	dialog = myGetNewDialog(dlogNET_GAME_STATS, NULL, (WindowPtr) -1, refNETWORK_CARNAGE_DIALOG);
	assert(dialog);
	stats_dialog_upp= NewModalFilterUPP(display_net_stats_proc);
	assert(stats_dialog_upp);
	update_damage_item_upp = NewUserItemUPP(update_damage_item_proc);

	GetDialogItem(dialog, iDAMAGE_STATS, &item_type, &item_handle, &item_rect);
	SetDialogItem(dialog, iDAMAGE_STATS, kUserDialogItem|kItemDisableBit,
		(Handle)update_damage_item_upp, &item_rect);

	/* Calculate the rankings (once) for the entire graph */
	calculate_rankings(rankings, dynamic_world->player_count);
	qsort(rankings, dynamic_world->player_count, sizeof(struct net_rank), rank_compare);

	/* Create the graph popup menu */
	current_graph_selection= create_graph_popup_menu(dialog, iGRAPH_POPUP);
	previous_mode= find_graph_mode(dialog, NULL);
	
	ShowWindow(GetDialogWindow(dialog));
	
	GetPort(&old_port);
	SetPort(GetWindowPort(GetDialogWindow(dialog)));

	do
	{
		ModalDialog(stats_dialog_upp, &item_hit);
		switch (item_hit)
		{
			case iGRAPH_POPUP:
				value= get_dialog_control_value(dialog, iGRAPH_POPUP);
				if (current_graph_selection != value)
				{
					short new_mode;
				
					/* Determine what we need to delete. */
					GetDialogItem(dialog, iDAMAGE_STATS, &item_type, &item_handle, &item_rect);
					Rect inval_rect(item_rect);
					InsetRect(&item_rect, 1, 1); /* Avoid the grey border */

					new_mode= find_graph_mode(dialog, NULL);
					if (!will_new_mode_reorder_dialog(new_mode, previous_mode))
					{
						item_rect.left += GRAPH_LEFT_INSET + NAME_BOX_WIDTH + GRAPH_BAR_SPACING - 1;
					}
					item_rect.top += GRAPH_TOP_INSET-1;
					previous_mode= new_mode;

					/* Erase! */
					EraseRect(&item_rect);
#if TARGET_API_MAC_CARBON
					InvalWindowRect(GetDialogWindow(dialog), &inval_rect);
#else
					InvalRect(&inval_rect);	// Assumed to be the dialog-box window
#endif

					current_graph_selection= value;
				}
				break;
		}
	} while(item_hit != iOK);

	SetPort(old_port);

	DisposeModalFilterUPP(stats_dialog_upp);
	DisposeDialog(dialog);
	DisposeUserItemUPP(update_damage_item_upp);

	return;
}


/* ------------------------- private code */
static short create_graph_popup_menu(
	DialogPtr dialog, 
	short item)
{
	MenuHandle graph_popup;
	short index, item_type;
	Handle item_handle;
	Rect item_rect;
	short current_graph_selection;
	bool has_scores;

	/* Clear the graph popup */
	graph_popup= get_popup_menu_handle(dialog, item);
	while(CountMenuItems(graph_popup))
		DeleteMenuItem(graph_popup, 1);

	/* Setup the player names */
	for (index= 0; index<dynamic_world->player_count; index++)
	{
		struct player_data *player= get_player_data(rankings[index].player_index);
		
#if TARGET_API_MAC_CARBON
		CopyCStringToPascal(player->name, ptemporary);
#else		
		strcpy(temporary, player->name); 
		c2pstr(temporary);
#endif
		AppendMenu(graph_popup, "\p ");
		SetMenuItemText(graph_popup, index+1, ptemporary); // +1 since it is 1's based
	}
	
	/* Add in the separator line */
	AppendMenu(graph_popup, "\p-");

	/* Add in the total carnage.. */
	AppendMenu(graph_popup, getpstr(ptemporary, strNET_STATS_STRINGS, strTOTALS_STRING));
	current_graph_selection= CountMenuItems(graph_popup);
	
	/* Add in the scores */
	has_scores= get_network_score_text_for_postgame(temporary, false);
	if(has_scores)
	{
		Str255 pscore_temp;
		CopyCStringToPascal(temporary, pscore_temp);
		AppendMenu(graph_popup, pscore_temp);
		current_graph_selection= CountMenuItems(graph_popup);
	}
	
	/* If the game has teams, show the team stats. */
	if (!(dynamic_world->game_information.game_options & _force_unique_teams)) 
	{
		/* Separator line */
		if(has_scores) AppendMenu(graph_popup, "\p-");

		AppendMenu(graph_popup, getpstr(ptemporary, strNET_STATS_STRINGS, strTEAM_TOTALS_STRING));

		if(has_scores)
		{
			get_network_score_text_for_postgame(temporary, true);
			Str255 ppostgame;
			CopyCStringToPascal(temporary, ppostgame);
			AppendMenu(graph_popup, ppostgame);
		}
	} 

	GetDialogItem(dialog, iGRAPH_POPUP, &item_type, &item_handle, &item_rect);
	SetControlMaximum((ControlHandle) item_handle, CountMenuItems(graph_popup));
	SetControlValue((ControlHandle) item_handle, current_graph_selection); 
	
	return current_graph_selection;
}


static pascal Boolean display_net_stats_proc(
	DialogPtr dialog,
	EventRecord *event,
	short *item_hit)
{
	GrafPtr old_port;
	bool handled= false;
	short item_type;
	Rect item_rect;
	Handle item_handle;
	short key, value, max;

	/* preprocess events */	
	GetPort(&old_port);
	SetPort(GetWindowPort(GetDialogWindow(dialog)));
	
	switch(event->what)
	{
		case updateEvt:
			if ((DialogPtr)event->message==dialog)
			{
				/* update the damage stats */
				update_damage_item(GetDialogWindow(dialog));
			}
			break;
			
		case keyDown: 
		case autoKey:
			key = event->message & charCodeMask;
			GetDialogItem(dialog, iGRAPH_POPUP, &item_type, &item_handle, &item_rect);
			value= GetControlValue((ControlHandle) item_handle);
			max= GetControlMaximum((ControlHandle) item_handle);
			
			switch(key)
			{
				case kUP_ARROW:
				case kLEFT_ARROW:
				case kPAGE_UP:
					if(value>1)
					{
						short new_value= value-1;
	
						switch(new_value-dynamic_world->player_count-1)
						{
							case 0:
							case 3:
								/* This is a separator line-> skip it */
								new_value--;
								break;
						}
						
						SetControlValue((ControlHandle) item_handle, new_value);
						*item_hit= iGRAPH_POPUP; 
						handled= true;
					}
					break;
					
				case kDOWN_ARROW:
				case kRIGHT_ARROW:
				case kPAGE_DOWN:
					if(value<max)
					{
						short new_value= value+1;
	
						switch(new_value-dynamic_world->player_count-1)
						{
							case 0:
							case 3:
								/* This is a separator line-> skip it */
								new_value++;
								break;
						}

						SetControlValue((ControlHandle) item_handle, new_value);
						*item_hit= iGRAPH_POPUP; 
						handled= true;
					}
					break;
			
				default:
					break;
			}
			break;
			
		case mouseDown:
			{
				Rect   box;
				short  index, max;
				Point  where;
				bool in_box = false;

				where = event->where;
				GlobalToLocal(&where);
				GetDialogItem(dialog, iDAMAGE_STATS, &item_type, &item_handle, &item_rect);
				SetRect(&box, 0, 0, NAME_BOX_WIDTH, NAME_BOX_HEIGHT);
				OffsetRect(&box, item_rect.left+GRAPH_LEFT_INSET, item_rect.top+GRAPH_TOP_INSET);
				
				/* Find if they clicked in an area.. */
				switch(find_graph_mode(dialog, NULL))
				{
					case _player_graph:
					case _total_carnage_graph:
					case _total_scores_graph:
						max= dynamic_world->player_count;
						break;

					case _total_team_carnage_graph:
					case _total_team_scores_graph:
						max= 0; /* Don't let them click in any of these. (what would you do?) */
						break;
						
					default:
						// LP change:
						assert(false);
						// halt();
						break;
				}
				
				/* Find the one clicked in.. */
				for (index= 0; index<max; index++)
				{
					if (PtInRect(where, &box))
					{
						in_box = true;
						break;
					}
					OffsetRect(&box, 0, RECTANGLE_HEIGHT(&box)+GRAPH_BAR_SPACING);
				}
				
				/* IF the one that they clicked in isn't the current one.. */
				GetDialogItem(dialog, iGRAPH_POPUP, &item_type, &item_handle, &item_rect);
				if (in_box && (index+1) != GetControlValue((ControlHandle) item_handle))
				{
					struct net_rank ranks[MAXIMUM_NUMBER_OF_PLAYERS];
	
					/* Use a private copy to avoid boning things */
					objlist_copy(ranks, rankings, dynamic_world->player_count);

					if(find_graph_mode(dialog, NULL) == _total_scores_graph)
					{
						/* First qsort the rankings arrray by game_ranking.. */
						qsort(ranks, dynamic_world->player_count,
							sizeof(struct net_rank), score_rank_compare);
					}

					bool last_in_box= false;
					RGBColor color;

					_get_player_color(ranks[index].color, &color);
								
					while (StillDown())
					{
						GetMouse(&where);
						in_box = PtInRect(where, &box);

						if (last_in_box != in_box)
						{
							if(ranks[index].player_index==NONE)
							{
								draw_beveled_text_box(in_box, &box, NAME_BEVEL_SIZE, &color, 
									"", _center_horizontal|_center_vertical, true);
							} else {
								struct player_data *player= get_player_data(ranks[index].player_index);
							
								draw_beveled_text_box(in_box, &box, NAME_BEVEL_SIZE, &color, 
									player->name, _center_horizontal|_center_vertical, true);
							}
							last_in_box= in_box;
						}
					}
					
					/* IF we were still in at the end.. */
					if (in_box)
					{
						// Find the right value for an adjusted sorting
						int popup_index;
						for(popup_index= 0; popup_index < dynamic_world->player_count;  popup_index++)
						{
							if(ranks[index].player_index == rankings[popup_index].player_index)
							{
								break;
							}
						}
						GetDialogItem(dialog, iGRAPH_POPUP, &item_type, &item_handle, &item_rect);
						SetControlValue((ControlHandle) item_handle, popup_index+1); // 1 based
						*item_hit = iGRAPH_POPUP; 
						handled = true;

						// One last set of drawing... reset the box bevel
						if(ranks[index].player_index==NONE)
						{
							draw_beveled_text_box(false, &box, NAME_BEVEL_SIZE, &color, 
								"", _center_horizontal|_center_vertical, true);
						} else {
							struct player_data *player= get_player_data(ranks[index].player_index);
						
							draw_beveled_text_box(false, &box, NAME_BEVEL_SIZE, &color, 
								player->name, _center_horizontal|_center_vertical, true);
						}
					}
				}
			}
			break;
	}

	SetPort(old_port);

	return handled ? true : general_filter_proc(dialog, event, item_hit);
}



static pascal void update_damage_item_proc(
	DialogPtr dialog,
	short item_num)
{
	GrafPtr old_port;
  	TextSpec font_info;
	TextSpec old_font;
	
	GetPort(&old_port);
	SetPort(GetWindowPort(GetDialogWindow(dialog)));

	GetNewTextSpec(&font_info, fontTOP_LEVEL_FONT, 0);
	GetFont(&old_font);
	SetFont(&font_info);

	// ZZZ: ripped this out into separate function for better
	draw_new_graph(dialog);
	
	SetFont(&old_font);	
	SetPort(old_port);
}
	
static void update_damage_item(
	WindowPtr dialog)
{
#if 0
// Now handled as UPP
	GrafPtr old_port;
  	TextSpec font_info;
	TextSpec old_font;

	GetPort(&old_port);
	SetPort(GetWindowPort(dialog));

	GetNewTextSpec(&font_info, fontTOP_LEVEL_FONT, 0);
	GetFont(&old_font);
	SetFont(&font_info);

	// ZZZ: ripped this out into separate function for better
	draw_new_graph(GetDialogFromWindow(dialog));
	
	SetFont(&old_font);	
	SetPort(old_port);
#endif
}

#endif


/* This function takes a rank structure because the rank structure contains the team & is */
/*  sorted.  */
void draw_names(
#ifdef USES_NIBS
	NetgameOutcomeData &Data,
#else
	DialogPtr dialog,
#endif
	struct net_rank *ranks, 
	short number_of_bars,
	short which_player)
{
	Rect item_rect, name_rect;
	short item_type, i;
	Handle item_handle;
	RGBColor color;

	SetRect(&name_rect, 0, 0, NAME_BOX_WIDTH, NAME_BOX_HEIGHT);
#ifdef USES_NIBS
	GetControlBounds(Data.DisplayCtrl, &item_rect);
#else
	GetDialogItem(dialog, iDAMAGE_STATS, &item_type, &item_handle, &item_rect);
#endif
	OffsetRect(&name_rect, item_rect.left+GRAPH_LEFT_INSET, item_rect.top+GRAPH_TOP_INSET);
	for (i = 0; i <number_of_bars; i++)
	{
		if (ranks[i].player_index != NONE)
		{
			struct player_data *player= get_player_data(ranks[i].player_index);

			_get_player_color(ranks[i].color, &color);
			draw_beveled_text_box(which_player==i, &name_rect, NAME_BEVEL_SIZE, &color, player->name, 
				_center_horizontal|_center_vertical, true);
		}
		else
		{
			_get_player_color(ranks[i].color, &color);
			draw_beveled_box(false, &name_rect, NAME_BEVEL_SIZE, &color);
		}
		OffsetRect(&name_rect, 0, RECTANGLE_HEIGHT(&name_rect)+GRAPH_BAR_SPACING);
	}
	
	return;
}


void draw_kill_bars(
#ifdef USES_NIBS
	NetgameOutcomeData &Data,
#else
	DialogPtr dialog,
#endif 
	struct net_rank *ranks, 
	short num_players, 
	short suicide_index, 
	bool do_totals, 
	bool friendly_fire)
{
	char kill_string_format[65], death_string_format[65], suicide_string_format[65];
	Rect item_rect, kill_bar_rect, death_bar_rect, suicide_bar_rect;
	short i;
	short item_type, max_kills, max_width;
	Handle item_handle;
	RGBColor kill_color, suicide_color, death_color;

	get_net_color(_kill_color, &kill_color);
	get_net_color(_suicide_color, &suicide_color);
	get_net_color(_death_color, &death_color);

	getcstr(kill_string_format, strNET_STATS_STRINGS, strKILLS_STRING);
	getcstr(death_string_format, strNET_STATS_STRINGS, strDEATHS_STRING);
	getcstr(suicide_string_format, strNET_STATS_STRINGS, strSUICIDES_STRING);

#ifdef USES_NIBS
	GetControlBounds(Data.DisplayCtrl, &item_rect);
#else
	GetDialogItem(dialog, iDAMAGE_STATS, &item_type, &item_handle, &item_rect);
#endif
	kill_bar_rect.left = item_rect.left + GRAPH_LEFT_INSET + NAME_BOX_WIDTH + GRAPH_BAR_SPACING;
	kill_bar_rect.top = item_rect.top + GRAPH_TOP_INSET;
	kill_bar_rect.bottom = kill_bar_rect.top + KILL_BAR_HEIGHT;
	
	death_bar_rect.left = item_rect.left + GRAPH_LEFT_INSET + NAME_BOX_WIDTH + GRAPH_BAR_SPACING;
	death_bar_rect.top = item_rect.top + GRAPH_TOP_INSET + NAME_BOX_HEIGHT - DEATH_BAR_HEIGHT;
	death_bar_rect.bottom = death_bar_rect.top + DEATH_BAR_HEIGHT;
	
	if (do_totals)
	{
		for (i = 0, max_kills = 0; i < num_players; i++)
		{
			if (ranks[i].kills > max_kills) max_kills = ranks[i].kills;
			if (ranks[i].deaths > max_kills) max_kills = ranks[i].deaths;
		}
	}
	else
	{
		max_kills= calculate_max_kills(num_players);
	}
	max_width = item_rect.right - GRAPH_RIGHT_INSET - kill_bar_rect.left;
	
	for (i = 0; i < num_players; i++)
	{
		if (max_kills)
			kill_bar_rect.right = kill_bar_rect.left + ((ranks[i].kills * max_width) / max_kills);
		else
			kill_bar_rect.right = kill_bar_rect.left;
		if (max_kills)
			death_bar_rect.right = death_bar_rect.left + ((ranks[i].deaths * max_width) / max_kills);
		else
			death_bar_rect.right = death_bar_rect.left;

		if (suicide_index != i)
		{
			Rect              ranking_rect;
			short             diff = ranks[i].kills - ranks[i].deaths;
			screen_rectangle  rr;
			
			if (ranks[i].kills)
			{
				sprintf(temporary, kill_string_format, ranks[i].kills);
				draw_beveled_text_box(false, &kill_bar_rect, KILL_BEVEL_SIZE, &kill_color, temporary, _right_justified|_top_justified, false);
				if (diff > 0)
				{
					ranking_rect = kill_bar_rect;
					ranking_rect.top += KILL_BEVEL_SIZE;
					ranking_rect.bottom -= KILL_BEVEL_SIZE;
				}
				
			}
			if (ranks[i].deaths)
			{
				sprintf(temporary, death_string_format, ranks[i].deaths);
				draw_beveled_text_box(false, &death_bar_rect, DEATH_BEVEL_SIZE, &death_color, temporary, _right_justified|_center_vertical, false);
				if (diff < 0)
				{
					ranking_rect = death_bar_rect;
					ranking_rect.top += DEATH_BEVEL_SIZE;
					ranking_rect.bottom -= DEATH_BEVEL_SIZE;
				}
			}
			if (diff == 0)
			{
				ranking_rect.top = kill_bar_rect.top;
				ranking_rect.bottom = death_bar_rect.bottom;
				ranking_rect.left = kill_bar_rect.left;
				ranking_rect.right = MAX(kill_bar_rect.right, death_bar_rect.right);
			}
			rr.top = ranking_rect.top;
			rr.bottom = ranking_rect.bottom;
			rr.left = ranking_rect.right + RANKING_INSET;
			rr.right = rr.left + 1000; // just make it big enough.
			
			sprintf(temporary, "%+d", diff);
			_draw_screen_text(temporary, &rr, _center_vertical, _net_stats_font, _black_color);
		}
		else
		{
			if (ranks[i].kills)
			{
				SetRect(&suicide_bar_rect, kill_bar_rect.left, kill_bar_rect.top, kill_bar_rect.right , death_bar_rect.bottom);
				sprintf(temporary, suicide_string_format, ranks[i].kills);
				draw_beveled_text_box(false, &suicide_bar_rect, KILL_BEVEL_SIZE, &suicide_color, temporary, _right_justified|_center_vertical, false);
			}
		}
		OffsetRect(&kill_bar_rect, 0, NAME_BOX_HEIGHT + GRAPH_BAR_SPACING);
		OffsetRect(&death_bar_rect, 0, NAME_BOX_HEIGHT + GRAPH_BAR_SPACING);
	}

    // ZZZ: ripped this out into a new function for sharing with SDL version
#ifdef USES_NIBS
    update_carnage_summary(Data, ranks, num_players, suicide_index, do_totals, friendly_fire);
#else
    update_carnage_summary(dialog, ranks, num_players, suicide_index, do_totals, friendly_fire);
#endif

	return;
}

static void draw_beveled_text_box(
	bool inset, 
	Rect *box,
	short bevel_size,
	RGBColor *brightest_color,
	char *text,
	short flags,
	bool name_box)
{
	Rect inset_box;
	short color;
	screen_rectangle text_box;
	
	draw_beveled_box(inset, box, bevel_size, brightest_color);

	color = inset || !name_box ? _black_color : _white_color;
	inset_box = *box; 
	InsetRect(&inset_box, bevel_size, bevel_size);
	text_box.top = inset_box.top; text_box.bottom = inset_box.bottom;
	text_box.left = inset_box.left; text_box.right = inset_box.right;
	_draw_screen_text(text, &text_box, flags, _net_stats_font, color);		
}

static void draw_beveled_box(
	bool inset, 
	Rect *box,
	short bevel_size,
	RGBColor *brightest_color)
{
	short i;
	RGBColor second_color, third_color, old_color;
	
	GetForeColor(&old_color);
	
	second_color.red = (brightest_color->red * 7) / 10;
	second_color.blue = (brightest_color->blue * 7) / 10;
	second_color.green = (brightest_color->green * 7) / 10;
	
	third_color.red = (brightest_color->red * 2) / 10;
	third_color.blue = (brightest_color->blue * 2) / 10;
	third_color.green = (brightest_color->green * 2) / 10;
	
	RGBForeColor(&second_color);
	PaintRect(box);
	
	if (RECTANGLE_WIDTH(box) > 2*bevel_size && RECTANGLE_HEIGHT(box) > 2*bevel_size)
	{
		if (inset)
		{
			RGBForeColor(&third_color);
		}
		else
		{
			RGBForeColor(brightest_color);
		}
		
		for (i = 0; i < bevel_size; i++)
		{
			MoveTo(box->left+i, box->top+i);
			LineTo(box->right-i, box->top+i);
			MoveTo(box->left+i, box->top+i);
			LineTo(box->left+i, box->bottom-i);
		}
		
		if (inset)
		{
			RGBForeColor(brightest_color);
		}
		else
		{
			RGBForeColor(&third_color);
		}
		
		for (i = 0; i < bevel_size; i++)
		{
			MoveTo(box->right-i, box->bottom-i);
			LineTo(box->left+i, box->bottom-i);
			MoveTo(box->right-i, box->bottom-i);
			LineTo(box->right-i, box->top+i);
		}
		
	}

	RGBForeColor(&old_color);
	return;
}

static void calculate_maximum_bar(	
#ifdef USES_NIBS
	NetgameOutcomeData &Data,
#else
	DialogPtr dialog,
#endif
	Rect *kill_bar_rect)
{
	short item_type;
	Handle item_handle;
	Rect item_rect;

#ifdef USES_NIBS
	GetControlBounds(Data.DisplayCtrl, &item_rect);
#else
	GetDialogItem(dialog, iDAMAGE_STATS, &item_type, &item_handle, &item_rect);
#endif
	kill_bar_rect->left = item_rect.left + GRAPH_LEFT_INSET + NAME_BOX_WIDTH + GRAPH_BAR_SPACING;
	kill_bar_rect->right = item_rect.right - GRAPH_RIGHT_INSET;
	kill_bar_rect->top = item_rect.top + GRAPH_TOP_INSET;
	kill_bar_rect->bottom = kill_bar_rect->top + NAME_BOX_HEIGHT;
}

void draw_score_bars(
#ifdef USES_NIBS
	NetgameOutcomeData &Data,
#else
	DialogPtr dialog,
#endif 
	struct net_rank *ranks, 
	short bar_count)
{
	short index, maximum_width;
	long highest_ranking= LONG_MIN;
	long lowest_ranking= LONG_MAX;
	Rect maximum_bar, bar;
	RGBColor color;
	short item_type;
	Handle item_handle;
	
	for(index= 0; index<bar_count; ++index)
	{
		if(ranks[index].game_ranking>highest_ranking) highest_ranking= ranks[index].game_ranking;
		if(ranks[index].game_ranking<lowest_ranking) lowest_ranking= ranks[index].game_ranking;
	}

#ifdef USES_NIBS
	calculate_maximum_bar(Data, &maximum_bar);
#else
	calculate_maximum_bar(dialog, &maximum_bar);
#endif
	bar= maximum_bar;
	maximum_width= RECTANGLE_WIDTH(&bar);

	get_net_color(_score_color, &color);

	// Perform adjustments if ranking goes under 0.
	// It likely means a scoring system like golf or for tag, where the person with
	// the least score wins.
	if(lowest_ranking < 0)
	{
		highest_ranking = lowest_ranking;
	}
	
	if(highest_ranking)
	{
		for(index= 0; index<bar_count; ++index)
		{
			/* Get the text. */
			calculate_ranking_text_for_post_game(temporary, ranks[index].game_ranking);
	
			/* Build the bar. */		
			bar.right= bar.left + (ranks[index].game_ranking*maximum_width)/highest_ranking;
	
			/* Draw it! */
			draw_beveled_text_box(false, &bar, NAME_BEVEL_SIZE, &color, temporary, 
				_right_justified|_center_vertical, false);
	
			OffsetRect(&bar, 0, NAME_BOX_HEIGHT + GRAPH_BAR_SPACING);
		}
	}

	/* And clear the text. */
#ifdef USES_NIBS
	SetStaticCText(Data.KillsTextCtrl,"");
	SetStaticCText(Data.DeathsTextCtrl,"");
#else
	GetDialogItem(dialog, iTOTAL_DEATHS, &item_type, &item_handle, &bar);
	SetDialogItemText(item_handle, "\p");
	GetDialogItem(dialog, iTOTAL_KILLS, &item_type, &item_handle, &bar);
	SetDialogItemText(item_handle, "\p");
#endif
}

static bool will_new_mode_reorder_dialog(
	short new_mode,
	short previous_mode)
{
	bool may_reorder= false;
		
	switch(new_mode)
	{
		case _player_graph:
			switch(previous_mode)
			{
				case _player_graph:
				case _total_carnage_graph:
					may_reorder= false; 
					break;
					
				case _total_scores_graph:
				case _total_team_carnage_graph:
				case _total_team_scores_graph:
					may_reorder= true;
					break;
			}
			break;
		
		case _total_carnage_graph:
			switch(previous_mode)
			{
				case _player_graph:
				case _total_carnage_graph:
					may_reorder= false; 
					break;
					
				case _total_scores_graph:
				case _total_team_carnage_graph:
				case _total_team_scores_graph:
					may_reorder= true;
					break;
			}
			break;
			
		case _total_scores_graph:
			switch(previous_mode)
			{
				case _total_scores_graph:
					may_reorder= false; 
					break;
					
				case _total_carnage_graph:
				case _player_graph:
				case _total_team_carnage_graph:
				case _total_team_scores_graph:
					may_reorder= true;
					break;
			}
			break;
	
		case _total_team_carnage_graph:
			switch(previous_mode)
			{
				case _total_team_carnage_graph:
					may_reorder= false;
					break;
	
				case _player_graph:
				case _total_carnage_graph:
				case _total_scores_graph:
				case _total_team_scores_graph:
					may_reorder= true; 
					break;
			}
			break;
			
		case _total_team_scores_graph:
			switch(previous_mode)
			{
				case _total_team_scores_graph:
					may_reorder= false;
					break;
	
				case _player_graph:
				case _total_carnage_graph:
				case _total_scores_graph:
				case _total_team_carnage_graph:
					may_reorder= true; 
					break;
			}
			break;
			
		default:
			// LP change:
			assert(false);
			// halt();
			break;
	}
	
	return may_reorder;
}

/* Stupid function, here as a hack.. */
static bool key_is_down(
	short key_code)
{
	KeyMap key_map;
	
	GetKeys(key_map);
	return ((((byte*)key_map)[key_code>>3] >> (key_code & 7)) & 1);
}

#ifdef NETWORK_TEST_POSTGAME_DIALOG
static const char*    sTestingNames[] = {
	"Doctor Burrito",
	"Carnage Asada",
	"Bongo Bob",
	"The Napalm Man",
	"Kissy Monster",
	"lala",
	"Prof. Windsurf",
	"<<<-ZED-<<<"
};

// THIS ONE IS FAKE - used to test postgame report dialog without going through a game.
bool network_gather(bool inResumingGame) {
	short i, j;
	player_info thePlayerInfo;
	game_info   theGameInfo;

	show_cursor(); // JTP: Hidden one way or another
	if(network_game_setup(&thePlayerInfo, &theGameInfo, false)) {

	// Test the progress bar while we're at it
	#ifdef ALSO_TEST_PROGRESS_BAR
	#include "progress.h"
	open_progress_dialog(_distribute_physics_single);
	for(i=0; i < 100; i++)
	{
		//nanosleep(&(timespec){0,50000000},NULL);
		//idle_progress_bar();
		int TC = TickCount();	// Busy-waiting; dumb
		while (TickCount() < TC+4);
		draw_progress_bar(i, 100);
	}
	set_progress_dialog_message(_distribute_map_single);
	reset_progress_bar();
	for(i=0; i < 100; i++)
	{
		//nanosleep(&(timespec){0,90000000},NULL);
		int TC = TickCount();	// Busy-waiting; dumb
		while (TickCount() < TC+4);
		draw_progress_bar(i, 100);
	}
	close_progress_dialog();
	#endif
	
	for (i = 0; i < MAXIMUM_NUMBER_OF_PLAYERS; i++)
	{
		// make up a name
		/*int theNameLength = (local_random() % MAXIMUM_PLAYER_NAME_LENGTH) + 1;
		for(int n = 0; n < theNameLength; n++)
				players[i].name[n] = 'a' + (local_random() % ('z' - 'a'));

		players[i].name[theNameLength] = '\0';
*/
		strcpy(players[i].name, sTestingNames[i]);

		// make up a team and color
		players[i].color = local_random() % 8;
		int theNumberOfTeams = 2 + (local_random() % 3);
		players[i].team  = local_random() % theNumberOfTeams;

		(players+i)->monster_damage_taken.damage = abs(local_random()%200);
		(players+i)->monster_damage_taken.kills = abs(local_random()%30);
		(players+i)->monster_damage_given.damage = abs(local_random()%200);
		(players+i)->monster_damage_given.kills = abs(local_random()%30);
		
		players[i].netgame_parameters[0] = local_random() % 200;
		players[i].netgame_parameters[1] = local_random() % 200;
		
		for (j = 0; j < MAXIMUM_NUMBER_OF_PLAYERS; j++)
		{
			(players+i)->damage_taken[j].damage = abs(local_random()%200);
			(players+i)->damage_taken[j].kills = abs(local_random()%6);
		}
	}

	dynamic_world->player_count = MAXIMUM_NUMBER_OF_PLAYERS;

	game_data& game_information = dynamic_world->game_information;
	game_info* network_game_info = &theGameInfo;

	game_information.game_time_remaining= network_game_info->time_limit;
	game_information.kill_limit= network_game_info->kill_limit;
	game_information.game_type= network_game_info->net_game_type;
	game_information.game_options= network_game_info->game_options;
	game_information.initial_random_seed= network_game_info->initial_random_seed;
	game_information.difficulty_level= network_game_info->difficulty_level;

	display_net_game_stats();
	} // if setup box was OK'd
	hide_cursor();
	return false;
}
#endif
