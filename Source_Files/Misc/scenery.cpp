/*
SCENERY.C
Thursday, December 1, 1994 11:56:43 AM  (Jason)

Friday, June 16, 1995 11:48:23 AM  (Jason)
	animated scenery; audible scenery.
Tuesday, October 10, 1995 10:30:58 AM  (Jason)
	destroyable scenery; new_scenery doesn�t bail on out-of-range scenery.

Jan 30, 2000 (Loren Petrich):
	Added some typecasts

Feb 15, 2000 (Loren Petrich):
	Added some code to load scenery definitions
	And to manage animated scenery with a growable list

May 17, 2000 (Loren Petrich):
	Added XML support

May 18, 2000 (Loren Petrich):
	If an object has destruction effect NONE, then don't create an effect.

May 26, 2000 (Loren Petrich):
	Added XML shapes support
*/

#include "cseries.h"
#include "map.h"
#include "render.h"
#include "interface.h"
#include "flood_map.h"
#include "effects.h"
#include "monsters.h"
#include "projectiles.h"
#include "player.h"
#include "platforms.h"
#include "scenery.h"
// LP addition:
#include "GrowableList.h"

#include <string.h>
#include <stdlib.h>
#include "ShapesParser.h"

#ifdef env68k
#pragma segment objects
#endif

/* ---------- constants */

enum
{
	MAXIMUM_ANIMATED_SCENERY_OBJECTS= 20
};

/* ---------- globals */

#include "scenery_definitions.h"


// LP change: growable list of animated scenery objects
static GrowableList<short> AnimatedSceneryObjects(16);
/*
static short animated_scenery_object_count;
static short *animated_scenery_object_indexes;
*/


/* ---------- private prototypes */

#ifdef DEBUG
struct scenery_definition *get_scenery_definition(short scenery_type);
#else
#define get_scenery_definition(i) (scenery_definitions+(i))
#endif

/* ---------- code */

void initialize_scenery(
	void)
{
	// LP: no longert necessary
	/*
	animated_scenery_object_indexes= (short *)malloc(sizeof(short)*MAXIMUM_ANIMATED_SCENERY_OBJECTS);
	assert(animated_scenery_object_indexes);
	*/
	
	return;
}

/* returns object index if successful, NONE otherwise */
short new_scenery(
	struct object_location *location,
	short scenery_type)
{
	short object_index= NONE;
	
	if (scenery_type<NUMBER_OF_SCENERY_DEFINITIONS)
	{
		struct scenery_definition *definition= get_scenery_definition(scenery_type);
		
		object_index= new_map_object(location, definition->shape);
		if (object_index!=NONE)
		{
			struct object_data *object= get_object_data(object_index);
			
			SET_OBJECT_OWNER(object, _object_is_scenery);
			SET_OBJECT_SOLIDITY(object, (definition->flags&_scenery_is_solid) ? true : false);
			object->permutation= scenery_type;
		}
	}
	
	return object_index;
}

void animate_scenery(
	void)
{
	short i;
	
	for (i= 0; i<AnimatedSceneryObjects.GetLength(); ++i)
	{
		animate_object(AnimatedSceneryObjects[i]);
	}
	/*
	for (i= 0; i<animated_scenery_object_count; ++i)
	{
		animate_object(animated_scenery_object_indexes[i]);
	}
	*/
	
	return;
}

void randomize_scenery_shapes(
	void)
{
	struct object_data *object;
	short object_index;
	
	// LP Change:
	AnimatedSceneryObjects.ResetLength();
	// animated_scenery_object_count= 0;
	
	for (object_index= 0, object= objects; object_index<MAXIMUM_OBJECTS_PER_MAP; ++object_index, ++object)
	{
		if (GET_OBJECT_OWNER(object)==_object_is_scenery)
		{
			struct scenery_definition *definition= get_scenery_definition(object->permutation);
			
			if (!randomize_object_sequence(object_index, definition->shape))
			{
				// LP change:
				assert(AnimatedSceneryObjects.Add(object_index));
				/*
				if (animated_scenery_object_count<MAXIMUM_ANIMATED_SCENERY_OBJECTS)
				{
					animated_scenery_object_indexes[animated_scenery_object_count++]= object_index;
				}
				*/
			}
		}
	}
	
	return;
}

void get_scenery_dimensions(
	short scenery_type,
	world_distance *radius,
	world_distance *height)
{
	struct scenery_definition *definition= get_scenery_definition(scenery_type);

	*radius= definition->radius;
	*height= definition->height;
	
	return;
}

void damage_scenery(
	short object_index)
{
	struct object_data *object= get_object_data(object_index);
	struct scenery_definition *definition= get_scenery_definition(object->permutation);
	
	if (definition->flags&_scenery_can_be_destroyed)
	{
		object->shape= definition->destroyed_shape;
		// LP addition: don't create a destruction effect if the effect type is NONE
		if (definition->destroyed_effect != NONE)
			new_effect(&object->location, object->polygon, definition->destroyed_effect, object->facing);
		SET_OBJECT_OWNER(object, _object_is_normal);
	}
	
	return;
}

/* ---------- private code */

#ifdef DEBUG
struct scenery_definition *get_scenery_definition(
	short scenery_type)
{
	assert(scenery_type>=0 && scenery_type<NUMBER_OF_SCENERY_DEFINITIONS);
	
	return scenery_definitions + scenery_type;
}
#endif


// For being more specific about the shapes -- either normal or destroyed
class XML_SceneryShapesParser: public XML_ElementParser
{
	public:
	shape_descriptor *DescPtr;
	
	bool Start()
	{
		Shape_SetPointer(DescPtr);
		return true;
	}
	
	XML_SceneryShapesParser(const char *_Name): XML_ElementParser(_Name) {}
};

static XML_SceneryShapesParser SceneryNormalParser("normal"), SceneryDestroyedParser("destroyed");


class XML_SceneryObjectParser: public XML_ElementParser
{
	int Index;
	scenery_definition Data;
	
	// What is present?
	bool IndexPresent;
	enum {NumberOfValues = 4};
	bool IsPresent[NumberOfValues];
	
public:
	bool Start();
	bool HandleAttribute(const char *Tag, const char *Value);
	bool AttributesDone();
	
	XML_SceneryObjectParser(): XML_ElementParser("object") {}
};

bool XML_SceneryObjectParser::Start()
{
	IndexPresent = false;
	for (int k=0; k<NumberOfValues; k++)
		IsPresent[k] = false;
	
	return true;
}

bool XML_SceneryObjectParser::HandleAttribute(const char *Tag, const char *Value)
{
	if (strcmp(Tag,"index") == 0)
	{
		if (ReadBoundedNumericalValue(Value,"%d",Index,int(0),int(NUMBER_OF_SCENERY_DEFINITIONS-1)))
		{
			IndexPresent = true;
			return true;
		}
		else return false;
	}
	else if (strcmp(Tag,"flags") == 0)
	{
		if (ReadNumericalValue(Value,"%hu",Data.flags))
		{
			IsPresent[0] = true;
			return true;
		}
		else return false;
	}
	else if (strcmp(Tag,"radius") == 0)
	{
		if (ReadNumericalValue(Value,"%hd",Data.radius))
		{
			IsPresent[1] = true;
			return true;
		}
		else return false;
	}
	else if (strcmp(Tag,"height") == 0)
	{
		if (ReadNumericalValue(Value,"%hd",Data.height))
		{
			IsPresent[2] = true;
			return true;
		}
		else return false;
	}
	else if (strcmp(Tag,"destruction") == 0)
	{
		if (ReadBoundedNumericalValue(Value,"%hd",Data.destroyed_effect,short(NONE),short(NUMBER_OF_EFFECT_TYPES-1)))
		{
			IsPresent[3] = true;
			return true;
		}
		else return false;
	}
	UnrecognizedTag();
	return false;
}

bool XML_SceneryObjectParser::AttributesDone()
{
	// Verify...
	if (!IndexPresent)
	{
		AttribsMissing();
		return false;
	}
	scenery_definition& OrigData = scenery_definitions[Index];
	
	if (IsPresent[0]) OrigData.flags = Data.flags;
	if (IsPresent[1]) OrigData.radius = Data.radius;
	if (IsPresent[2]) OrigData.height = Data.height;
	if (IsPresent[3]) OrigData.destroyed_effect = Data.destroyed_effect;
	
	SceneryNormalParser.DescPtr = &OrigData.shape;
	SceneryDestroyedParser.DescPtr = &OrigData.destroyed_shape;
			
	return true;
}

static XML_SceneryObjectParser SceneryObjectParser;


static XML_ElementParser SceneryParser("scenery");


// XML-parser support
XML_ElementParser *Scenery_GetParser()
{
	SceneryNormalParser.AddChild(Shape_GetParser());
	SceneryDestroyedParser.AddChild(Shape_GetParser());
	SceneryObjectParser.AddChild(&SceneryNormalParser);
	SceneryObjectParser.AddChild(&SceneryDestroyedParser);
	SceneryParser.AddChild(&SceneryObjectParser);
	
	return &SceneryParser;
}

