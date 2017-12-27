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


#ifndef TR_LOCAL_H
#define TR_LOCAL_H

#include "../qcommon/q_shared.h"
#include "../qcommon/qfiles.h"
#include "../qcommon/qcommon.h"
#include "tr_public.h"
#include "qgl.h"

#define GL_INDEX_TYPE GL_UNSIGNED_INT
typedef unsigned int glIndex_t;

extern const float s_flipMatrix[16];

#pragma pack(push, 1)
typedef struct {
	unsigned char	id_length, colormap_type, image_type;
	unsigned short	colormap_index, colormap_length;
	unsigned char	colormap_size;
	unsigned short	x_origin, y_origin, width, height;
	unsigned char	pixel_size, attributes;
} TargaHeader;
#pragma pack(pop)


// fast float to int conversion
//if id386 && !defined(__GNUC__)
//long myftol( float f );
//else
#define	myftol(x) ((int)(x))
//endif


// a trRefEntity_t has all the information passed in by the cgame
// as well as some locally derived info

struct trRefEntity_t {
	refEntity_t	e;

	float	axisLength;			// compensate for non-normalized axis

	qbool	lightingCalculated;
	vec3_t	lightDir;			// normalized direction towards light
	vec3_t	ambientLight;		// color normalized to 0-255
	int		ambientLightInt;	// 32 bit rgba packed
	vec3_t	directedLight;

	qbool	intShaderTime;		// is the shaderTime member an integer?
};


struct orientationr_t {
	vec3_t		origin;			// in world coordinates
	vec3_t		axis[3];		// orientation in world
	vec3_t		viewOrigin;		// viewParms->or.origin in local coordinates
	float		modelMatrix[16];
};


struct image_t {
	image_t* next;

	int		width, height;		// actual, ie after power of two, picmip, and clamp to MAX_TEXTURE_SIZE
	int		flags;				// IMG_ bits

	GLuint	texnum;				// gl texture binding
	GLenum	format;
	int		wrapClampMode;		// GL_CLAMP|GL_CLAMP_TO_EDGE or GL_REPEAT

	char	name[MAX_QPATH];	// game path, including extension
};


///////////////////////////////////////////////////////////////


typedef enum {
	SS_BAD,
	SS_PORTAL,			// mirrors, portals, viewscreens
	SS_ENVIRONMENT,		// sky box
	SS_OPAQUE,			// opaque

	SS_DECAL,			// scorch marks, etc.
	SS_SEE_THROUGH,		// ladders, grates, grills that may have small blended edges
						// in addition to alpha test
	SS_BANNER,

	SS_FOG,

	SS_UNDERWATER,		// for items that should be drawn in front of the water plane

	SS_BLEND0,			// regular transparency and filters
	SS_BLEND1,			// generally only used for additive type effects
	SS_BLEND2,
	SS_BLEND3,

	SS_BLEND6,
	SS_STENCIL_SHADOW,
	SS_ALMOST_NEAREST,	// gun smoke puffs

	SS_NEAREST			// blood blobs
} shaderSort_t;


#define MAX_SHADER_STAGES 8

typedef enum {
	GF_NONE,
	GF_SIN,
	GF_SQUARE,
	GF_TRIANGLE,
	GF_SAWTOOTH,
	GF_INVERSE_SAWTOOTH,
	GF_NOISE
} genFunc_t;

typedef enum {
	DEFORM_NONE,
	DEFORM_WAVE,
	DEFORM_NORMALS,
	DEFORM_BULGE,
	DEFORM_MOVE,
	DEFORM_PROJECTION_SHADOW,
	DEFORM_AUTOSPRITE,
	DEFORM_AUTOSPRITE2,
	DEFORM_TEXT0,
	DEFORM_TEXT1,
	DEFORM_TEXT2,
	DEFORM_TEXT3,
	DEFORM_TEXT4,
	DEFORM_TEXT5,
	DEFORM_TEXT6,
	DEFORM_TEXT7
} deform_t;

typedef enum {
	AGEN_IDENTITY,
	AGEN_SKIP,
	AGEN_ENTITY,
	AGEN_ONE_MINUS_ENTITY,
	AGEN_VERTEX,
	AGEN_ONE_MINUS_VERTEX,
	AGEN_LIGHTING_SPECULAR,
	AGEN_WAVEFORM,
	AGEN_PORTAL,
	AGEN_CONST
} alphaGen_t;

typedef enum {
	CGEN_BAD,
	CGEN_IDENTITY_LIGHTING,	// tr.identityLight
	CGEN_IDENTITY,			// always (1,1,1,1)
	CGEN_ENTITY,			// grabbed from entity's modulate field
	CGEN_ONE_MINUS_ENTITY,	// grabbed from 1 - entity.modulate
	CGEN_EXACT_VERTEX,		// tess.vertexColors
	CGEN_VERTEX,			// tess.vertexColors * tr.identityLight
	CGEN_ONE_MINUS_VERTEX,
	CGEN_WAVEFORM,			// programmatically generated
	CGEN_LIGHTING_DIFFUSE,
	CGEN_FOG,				// standard fog
	CGEN_CONST				// fixed color
} colorGen_t;

typedef enum {
	TCGEN_BAD,
	TCGEN_IDENTITY,			// clear to 0,0
	TCGEN_LIGHTMAP,
	TCGEN_TEXTURE,
	TCGEN_ENVIRONMENT_MAPPED,
	TCGEN_FOG,
	TCGEN_VECTOR			// S and T from world coordinates
} texCoordGen_t;

typedef enum {
	ACFF_NONE,
	ACFF_MODULATE_RGB,
	ACFF_MODULATE_RGBA,
	ACFF_MODULATE_ALPHA
} acff_t;

typedef struct {
	genFunc_t	func;

	double base;
	double amplitude;
	double phase;
	double frequency;
} waveForm_t;

#define TR_MAX_TEXMODS 4

typedef enum {
	TMOD_NONE,
	TMOD_TRANSFORM,
	TMOD_TURBULENT,
	TMOD_SCROLL,
	TMOD_SCALE,
	TMOD_STRETCH,
	TMOD_ROTATE,
	TMOD_ENTITY_TRANSLATE
} texMod_t;

#define	MAX_SHADER_DEFORMS	3
typedef struct {
	deform_t	deformation;			// vertex coordinate modification type

	vec3_t		moveVector;
	waveForm_t	deformationWave;
	float		deformationSpread;

	float		bulgeWidth;
	float		bulgeHeight;
	float		bulgeSpeed;
} deformStage_t;


typedef struct {
	texMod_t	type;

	// used for TMOD_TURBULENT and TMOD_STRETCH
	waveForm_t	wave;

	// used for TMOD_TRANSFORM
	float		matrix[2][2];	// s' = s * m[0][0] + t * m[1][0] + trans[0]
	float		translate[2];	// t' = s * m[0][1] + t * m[0][1] + trans[1]

	// used for TMOD_SCALE
	float		scale[2];		// s *= scale[0], t *= scale[1]

	// used for TMOD_SCROLL
	float		scroll[2];		// s' = s + scroll[0] * time, t' = t + scroll[1] * time

	// + = clockwise
	// - = counterclockwise
	float		rotateSpeed;

} texModInfo_t;


#define	MAX_IMAGE_ANIMATIONS	8

typedef struct {
	const image_t*	image[MAX_IMAGE_ANIMATIONS];
	int				numImageAnimations;
	double			imageAnimationSpeed;
	qbool			isVideoMap;		// shit code - no reason to have both of these
	int				videoMapHandle;
} textureBundle_t;

typedef enum {
	ST_DIFFUSE,
	ST_LIGHTMAP,
	ST_MAX
} stageType_t;

typedef struct {
	qbool		active;

	textureBundle_t	bundle;

	texCoordGen_t	tcGen;
	vec3_t			tcGenVectors[2];

	int				numTexMods;
	texModInfo_t	*texMods;

	waveForm_t		rgbWave;
	colorGen_t		rgbGen;

	waveForm_t		alphaWave;
	alphaGen_t		alphaGen;

	byte			constantColor[4];			// for CGEN_CONST and AGEN_CONST

	unsigned		stateBits;					// GLS_xxxx mask

	acff_t			adjustColorsForFog;

	qbool			isDetail;
	stageType_t		type;

	int				mtStages;	// number of subsequent stages also consumed by this stage (e.g. 1 for DxLM MT)
	GLint			mtEnv;
} shaderStage_t;


#define LIGHTMAP_2D			-3		// shader is for 2D rendering
#define LIGHTMAP_BY_VERTEX	-2		// pre-lit triangle models
#define LIGHTMAP_NONE		-1

typedef enum {
	CT_FRONT_SIDED,
	CT_BACK_SIDED,
	CT_TWO_SIDED
} cullType_t;

typedef enum {
	FP_NONE,		// surface is translucent and will just be adjusted properly
	FP_EQUAL,		// surface is opaque but possibly alpha tested
	FP_LE			// surface is trnaslucent, but still needs a fog pass (fog surface)
} fogPass_t;

typedef struct {
	float		cloudHeight;
	const image_t *outerbox[6], *innerbox[6];
} skyParms_t;

typedef struct {
	vec3_t	color;
	float	depthForOpaque;
} fogParms_t;


struct shader_t {
	char		name[MAX_QPATH];		// game path, including extension
	int			lightmapIndex;			// for a shader to match, both name and lightmapIndex must match

	int			index;					// this shader == tr.shaders[index]
	int			sortedIndex;			// this shader == tr.sortedShaders[sortedIndex]

	float		sort;					// lower numbered shaders draw before higher numbered

	qbool		defaultShader;			// we want to return index 0 if the shader failed to load,
										// but R_FindShader should still keep a name allocated for it,
										// so if something calls RE_RegisterShader again with
										// the same name, we don't try looking for it again

	qbool		explicitlyDefined;		// found in a .shader file

	int			surfaceFlags;			// if explicitlyDefined, this will have SURF_* flags
	int			contentFlags;

	qbool		entityMergable;			// multiple refentities can be combined in one batch (smoke, blood)

	skyParms_t	sky;
	fogParms_t	fogParms;

	float		portalRange;			// distance to fog out at

	cullType_t	cullType;				// CT_FRONT_SIDED, CT_BACK_SIDED, or CT_TWO_SIDED
	qbool		polygonOffset;			// set for decals and other items that must be offset

	int imgflags;	// nopicmip, nomipmaps, etc

	qbool	needsNormal;				// not all shaders will need all data to be gathered
	qbool	needsST1;
	qbool	needsST2;
	qbool	needsColor;

	int			numDeforms;
	deformStage_t	deforms[MAX_SHADER_DEFORMS];

	int			numStages;			// not counting fog pass (if any)
	shaderStage_t	*stages[MAX_SHADER_STAGES];
	int lightingStages[ST_MAX];

	fogPass_t	fogPass;			// draw a blended pass, possibly with depth test equals

	void		(*siFunc)();

	double clampTime;				// time this shader is clamped to
	double timeOffset;				// current time offset for this shader

	shader_t* next;
};


// skins allow models to be retextured without modifying the model file

struct skinSurface_t {
	char name[MAX_QPATH];
	shader_t* shader;
};

struct skin_t {
	char name[MAX_QPATH];
	int numSurfaces;
	skinSurface_t* surfaces[MD3_MAX_SURFACES];
};


typedef struct {
	int			originalBrushNumber;
	vec3_t		bounds[2];

	unsigned	colorInt;				// in packed byte format
	float		tcScale;				// texture coordinate vector scales
	fogParms_t	parms;

	// for clipping distance in fog when outside
	qbool		hasSurface;
	float		surface[4];
} fog_t;


typedef struct {
	orientationr_t	orient;
	orientationr_t	world;
	vec3_t		pvsOrigin;			// may be different than or.origin for portals
	qbool		isPortal;			// true if this view is through a portal
	qbool		isMirror;			// the portal is a mirror, invert the face culling
	int			frameSceneNum;		// copied from tr.frameSceneNum
	int			frameCount;			// copied from tr.frameCount
	cplane_t	portalPlane;		// clip anything behind this if mirroring
	int			viewportX, viewportY, viewportWidth, viewportHeight;
	float		fovX, fovY;
	float		projectionMatrix[16];
	cplane_t	frustum[4];
	vec3_t		visBounds[2];
	float		zFar;
} viewParms_t;


/*
==============================================================================

SURFACES

==============================================================================
*/

typedef byte color4ub_t[4];

// any changes in surfaceType must be mirrored in rb_surfaceTable[]
typedef enum {
	SF_BAD,
	SF_SKIP,				// ignore
	SF_FACE,
	SF_GRID,
	SF_TRIANGLES,
	SF_POLY,
	SF_MD3,
	SF_FLARE,
	SF_ENTITY,				// beams, rails, lightning, etc that can be determined by entity

	SF_NUM_SURFACE_TYPES,
	SF_MAX = 0x7fffffff			// ensures that sizeof( surfaceType_t ) == sizeof( int )
} surfaceType_t;

struct drawSurf_t {
	unsigned			sort;			// bit combination for fast compares
	const surfaceType_t* surface;		// any of surface*_t
};

extern void (*rb_surfaceTable[SF_NUM_SURFACE_TYPES])( const void* );


struct litSurf_t {
	unsigned sort;						// bit combination for fast compares
	const surfaceType_t* surface;		// any of surface*_t
	litSurf_t* next;
};

struct dlight_t {
	vec3_t	origin;
	vec3_t	color;			// range from 0.0 to 1.0, should be color normalized
	float	radius;
	vec3_t	transformed;	// origin in local coordinate system
	qbool	active;			// actually shines into the frustum rather than just pvs
	litSurf_t* head;
	litSurf_t* tail;
};


#define	MAX_FACE_POINTS		64

#define	MAX_PATCH_SIZE		32			// max dimensions of a patch mesh in map file
#define	MAX_GRID_SIZE		65			// max dimensions of a grid mesh in memory


// for cgame to add raw polys to a scene
struct srfPoly_t {
	surfaceType_t	surfaceType;
	qhandle_t		hShader;
	int				fogIndex;
	int				numVerts;
	polyVert_t*		verts;
};


typedef struct srfFlare_s {
	surfaceType_t	surfaceType;
	vec3_t			origin;
	vec3_t			normal;
	vec3_t			color;
} srfFlare_t;


struct srfGridMesh_t {
	surfaceType_t	surfaceType;

	// culling information
	vec3_t			meshBounds[2];
	vec3_t			localOrigin;
	float			meshRadius;

	// lod information, which may be different
	// than the culling information to allow for
	// groups of curves that LOD as a unit
	vec3_t			lodOrigin;
	float			lodRadius;
	int				lodFixed;
	int				lodStitched;

	// vertexes
	int				width, height;
	float			*widthLodError;
	float			*heightLodError;
	drawVert_t		verts[1];		// variable sized
};


// a srfVert_t is essentially a "fully featured" drawVert_t
// in some cases, eg srfSurfaceFace_t, the normal is common to the plane
// and doesn't actually HAVE to be populated, but...
struct srfVert_t {
	vec3_t xyz;
	vec3_t normal;
	vec2_t st;   // diffuse TC
	vec2_t st2;  // lightmap TC
	color4ub_t rgba;
};

struct srfSurfaceFace_t {
	surfaceType_t	surfaceType;
	cplane_t		plane;

	int				numIndexes;
	int				*indexes;

	int				numVerts;
	srfVert_t		*verts;
};


// misc_models in maps are turned into direct geometry by q3map
struct srfTriangles_t {
	surfaceType_t	surfaceType;

	vec3_t			bounds[2];
	vec3_t			localOrigin;
	float			radius;

	int				numIndexes;
	int				*indexes;

	int				numVerts;
	srfVert_t		*verts;
};


///////////////////////////////////////////////////////////////


// trRefdef_t holds everything that comes in refdef_t,
// as well as the locally generated scene information

typedef struct {
	int			x, y, width, height;
	float		fov_x, fov_y;
	vec3_t		vieworg;
	vec3_t		viewaxis[3];		// transformation matrix

	int			time;				// time in milliseconds for shader effects and other time dependent rendering issues
	int			rdflags;			// RDF_NOWORLDMODEL, etc

	// 1 bits will prevent the associated area from rendering at all
	byte		areamask[MAX_MAP_AREA_BYTES];
	qbool		areamaskModified;	// qtrue if areamask changed since last scene

	double		floatTime;			// tr.refdef.time / 1000.0

	// text messages for deform text shaders
	char		text[MAX_RENDER_STRINGS][MAX_RENDER_STRING_LENGTH];

	int			num_entities;
	trRefEntity_t	*entities;

	int			num_dlights;
	dlight_t*	dlights;

	int numPolys;
	srfPoly_t* polys;

	int numDrawSurfs;
	drawSurf_t* drawSurfs;

	int numLitSurfs;
	litSurf_t* litSurfs;

} trRefdef_t;


/*
==============================================================================

BRUSH MODELS

==============================================================================
*/


//
// in memory representation
//

#define SIDE_FRONT	0
#define SIDE_BACK	1
#define SIDE_ON		2

struct msurface_t {
	int vcBSP;		// if == tr.viewCount, is in the PVS and BSP of this frame
	int vcVisible;	// if == tr.viewCount, is actually VISIBLE in this frame, i.e. passed facecull and has been added to the drawsurf list
	int lightCount;	// if == tr.lightCount, already added to the litsurf list for the current light
	const shader_t* shader;
	int fogIndex;

	const surfaceType_t* data; // any of srf*_t
};


#define CONTENTS_NODE -1

struct mnode_t {
	// common with leaf and node
	int			contents;		// -1 for nodes, to differentiate from leafs
	int			visframe;		// node needs to be traversed (is in PVS) if == tr.visCount
	vec3_t		mins, maxs;		// for bounding box culling
	struct mnode_t* parent;

	// node specific
	const cplane_t* plane;
	struct mnode_t* children[2];

	// leaf specific
	int			cluster;
	int			area;
	msurface_t	**firstmarksurface;
	int			nummarksurfaces;
};

typedef struct {
	vec3_t		bounds[2];		// for culling
	msurface_t	*firstSurface;
	int			numSurfaces;
} bmodel_t;

typedef struct {
	char		name[MAX_QPATH];		// ie: maps/tim_dm2.bsp
	char		baseName[MAX_QPATH];	// ie: tim_dm2

	int			dataSize;

	int			numShaders;
	dshader_t	*shaders;

	bmodel_t	*bmodels;

	int			numplanes;
	cplane_t	*planes;

	int			numnodes;		// includes leafs
	mnode_t		*nodes;

	int			numsurfaces;
	msurface_t	*surfaces;

	int			nummarksurfaces;
	msurface_t	**marksurfaces;

	int			numfogs;
	fog_t		*fogs;

	vec3_t		lightGridOrigin;
	vec3_t		lightGridSize;
	vec3_t		lightGridInverseSize;
	int			lightGridBounds[3];
	byte		*lightGridData;

	int			numClusters;
	int			clusterBytes;
	const byte	*vis;			// may be passed in by CM_LoadMap to save space
	byte		*novis;			// clusterBytes of 0xff

	char		*entityString;
	const char* entityParsePoint;
} world_t;


///////////////////////////////////////////////////////////////


typedef enum {
	MOD_BAD,
	MOD_BRUSH,
	MOD_MD3,
} modtype_t;

struct model_t {
	char		name[MAX_QPATH];
	int			index;				// model = tr.models[model->index]
	modtype_t	type;
	bmodel_t*		bmodel;				// type == MOD_BRUSH
	md3Header_t*	md3[MD3_MAX_LODS];	// type == MOD_MD3
	int			numLods;
	int			dataSize;			// just for listing purposes
};


// unfortunately, MAX_*NET*_MODELS is incorrectly already defined as "MAX_MODELS"
#define MAX_RENDERER_MODELS 1024

void R_ModelInit();
const model_t* R_GetModelByHandle( qhandle_t hModel );
int R_LerpTag( orientation_t *tag, qhandle_t handle, int startFrame, int endFrame, float frac, const char *tagName );
void R_ModelBounds( qhandle_t handle, vec3_t mins, vec3_t maxs );
void R_Modellist_f( void );


///////////////////////////////////////////////////////////////


struct font_t {
	char name[MAX_QPATH];
	fontInfo_t info;
	// WARNING! pointsize bears NO resemblance to ANYTHING "real" about a font
	// the ONLY reason we keep it around at all is to optimise duplicate RegisterFont() detection
	int pointsize;
};

const int MAX_FONTS = 64;

void R_InitFreeType();
void R_DoneFreeType();
qbool RE_RegisterFont( const char* fontName, int pointSize, fontInfo_t* info );


///////////////////////////////////////////////////////////////


extern	refimport_t		ri;

#define	MAX_DRAWIMAGES			2048
#define	MAX_LIGHTMAPS			256
#define	MAX_SKINS				1024

#define	MAX_DRAWSURFS			0x10000
#define	DRAWSURF_MASK			(MAX_DRAWSURFS-1)

/*
the drawsurf sort data is packed into a single 32 bit value so it can be
compared quickly during the qsorting process

the bits are allocated as follows:

17-31 : sorted shader index
7-16  : entity index
2-6   : fog index
1-0   : unused
*/
#define QSORT_SHADERNUM_SHIFT	17
#define QSORT_ENTITYNUM_SHIFT	7
#define QSORT_FOGNUM_SHIFT		2

#define QSORT_ENTITYNUM_MASK	0x0001FF80

#define MAX_SHADERS				16384 // 1 << (32 - QSORT_SHADERNUM_SHIFT)


#define MAX_TMUS 4

// the renderer front end should never modify glstate_t
typedef struct {
	int			currenttmu;
	int			texID[MAX_TMUS];
	int			texEnv[MAX_TMUS];
	qbool		finishCalled;
	int			faceCulling;
	unsigned	glStateBits;
} glstate_t;


// all state modified by the back end is separated from the front end state

typedef struct {
	trRefdef_t		refdef;
	viewParms_t		viewParms;
	orientationr_t	orient;
	trRefEntity_t*	currentEntity;

	qbool			projection2D;	// if qtrue, drawstretchpic doesn't need to change modes
	byte			color2D[4];
	trRefEntity_t	entity2D;		// currentEntity will point at this when doing 2D rendering

	int* pc; // current stats set, depending on projection2D
	int pc2D[RB_STATS_MAX];
	int pc3D[RB_STATS_MAX];
} backEndState_t;


#define FOG_TABLE_SIZE		256
#define FUNCTABLE_SIZE		1024
#define FUNCTABLE_SHIFT		10
#define FUNCTABLE_MASK		(FUNCTABLE_SIZE-1)


/*
** trGlobals_t
**
** Most renderer globals are defined here.
** backend functions should never modify any of these fields,
** but may read fields that aren't dynamically modified
** by the frontend.
*/
typedef struct {
	qbool	registered;		// cleared at shutdown, set at beginRegistration

	int		visCount;		// incremented every time a new vis cluster is entered
	int		frameCount;		// incremented every frame
	int		sceneCount;		// incremented every scene
	int		viewCount;		// incremented every view (twice a scene if portaled) and every R_MarkFragments call
	int		lightCount;		// incremented for each dlight in the view

	int		frameSceneNum;	// zeroed at RE_BeginFrame

	qbool			worldMapLoaded;
	world_t*		world;

	const byte*		externalVisData;	// from RE_SetWorldVisData, shared with CM_Load

	image_t*		defaultImage;
	image_t*		whiteImage;		// { 255, 255, 255, 255 }
	image_t*		fogImage;
	image_t*		scratchImage[16];

	shader_t*		defaultShader;

	shader_t				*flareShader;

	int						numLightmaps;
	image_t					*lightmaps[MAX_LIGHTMAPS];

	trRefEntity_t			*currentEntity;
	trRefEntity_t			worldEntity;		// point currentEntity at this when rendering world
	int						currentEntityNum;
	int						shiftedEntityNum;	// currentEntityNum << QSORT_ENTITYNUM_SHIFT
	const model_t* currentModel;

	viewParms_t				viewParms;

	float					identityLight;		// 1.0 / ( 1 << overbrightBits )
	int						identityLightByte;	// identityLight * 255

	orientationr_t			orient;					// for current entity

	trRefdef_t				refdef;

	int						viewCluster;
	dlight_t* light;	// current light during R_RecursiveLightNode

	int pc[RF_STATS_MAX];

	//
	// put large tables at the end, so most elements will be
	// within the +/32K indexed range on risc processors
	//
	int numModels;
	model_t* models[MAX_RENDERER_MODELS];

	int numImages;
	image_t* images[MAX_DRAWIMAGES];

	int numFonts;
	font_t* fonts[MAX_FONTS];

	// shader indexes from other modules will be looked up in tr.shaders[]
	// shader indexes from drawsurfs will be looked up in sortedShaders[]
	// lower indexed sortedShaders must be rendered first (opaque surfaces before translucent)
	int			numShaders;
	shader_t*	shaders[MAX_SHADERS];
	shader_t*	sortedShaders[MAX_SHADERS];

	int			numSkins;
	skin_t*		skins[MAX_SKINS];

	float		sinTable[FUNCTABLE_SIZE];
	float		squareTable[FUNCTABLE_SIZE];
	float		triangleTable[FUNCTABLE_SIZE];
	float		sawToothTable[FUNCTABLE_SIZE];
	float		inverseSawToothTable[FUNCTABLE_SIZE];
	float		fogTable[FOG_TABLE_SIZE];
} trGlobals_t;

extern backEndState_t	backEnd;
extern trGlobals_t	tr;
extern glstate_t	glState;		// outside of TR since it shouldn't be cleared during ref re-init


//
// cvars
//

// r_mode
#define VIDEOMODE_DESKTOPRES	0	// no mode change, render size = desktop size
#define VIDEOMODE_UPSCALE		1	// no mode change, render size < desktop size
#define VIDEOMODE_CHANGE		2	// mode change - only makes sense for CRT users
#define VIDEOMODE_MAX			2

// r_blitMode
#define BLITMODE_ASPECT		0	// aspect-ratio preserving stretch
#define BLITMODE_CENTERED	1	// no stretch, displayed at the center
#define BLITMODE_STRETCHED	2	// dumb stretch, takes the full screen
#define BLITMODE_MAX		2

extern cvar_t	*r_verbose;				// used for verbose debug spew

extern cvar_t	*r_measureOverdraw;		// enables stencil buffer overdraw measurement

extern cvar_t	*r_lodbias;				// push/pull LOD transitions
extern cvar_t	*r_lodscale;

extern cvar_t	*r_fastsky;				// controls whether sky should be cleared or drawn
extern cvar_t	*r_noportals;			// controls portal/mirror "second view" drawing
extern cvar_t	*r_dynamiclight;		// dynamic lights enabled/disabled

extern	cvar_t	*r_norefresh;			// bypasses the ref rendering
extern	cvar_t	*r_drawentities;		// disable/enable entity rendering
extern	cvar_t	*r_drawworld;			// disable/enable world rendering
extern	cvar_t	*r_speeds;				// various levels of information display
extern  cvar_t	*r_detailTextures;		// enables/disables detail texturing stages
extern	cvar_t	*r_novis;				// disable/enable usage of PVS
extern	cvar_t	*r_nocull;
extern	cvar_t	*r_nocurves;

extern cvar_t	*r_mode;				// see VIDEOMODE_*
extern cvar_t	*r_blitMode;			// see BLITMODE_*
extern cvar_t	*r_fullscreen;
extern cvar_t	*r_displayRefresh;		// optional display refresh option

extern cvar_t	*r_intensity;
extern cvar_t	*r_gamma;
extern cvar_t	*r_greyscale;
extern cvar_t	*r_lightmap;			// render lightmaps only
extern cvar_t	*r_fullbright;			// avoid lightmap pass

extern cvar_t	*r_ext_max_anisotropy;
extern cvar_t	*r_msaa;

extern	cvar_t	*r_nobind;				// turns off binding to appropriate textures
extern	cvar_t	*r_singleShader;		// make most world faces use default shader
extern	cvar_t	*r_roundImagesDown;
extern	cvar_t	*r_colorMipLevels;		// development aid to see texture mip usage
extern	cvar_t	*r_picmip;				// controls picmip values
extern	cvar_t	*r_finish;

extern	cvar_t	*r_swapInterval;
extern	cvar_t	*r_textureMode;

extern	cvar_t	*r_vertexLight;			// vertex lighting mode for better performance
extern	cvar_t	*r_uiFullScreen;		// ui is running fullscreen

extern	cvar_t	*r_showtris;			// enables wireframe rendering of the world
extern	cvar_t	*r_showsky;				// forces sky in front of all surfaces
extern	cvar_t	*r_shownormals;			// draws wireframe normals
extern	cvar_t	*r_clear;				// force screen clear every frame

extern	cvar_t	*r_lockpvs;
extern	cvar_t	*r_portalOnly;

extern	cvar_t	*r_subdivisions;
extern	cvar_t	*r_lodCurveError;

extern	cvar_t	*r_ignoreGLErrors;

extern	cvar_t	*r_brightness;
extern	cvar_t	*r_mapBrightness;

extern	cvar_t	*r_debugSurface;

extern	cvar_t	*r_showImages;
extern	cvar_t	*r_debugSort;

extern cvar_t	*r_flares;
extern cvar_t	*r_flareSize;
extern cvar_t	*r_flareFade;
extern cvar_t	*r_flareCoeff;


void  R_NoiseInit();
double R_NoiseGet4f( double x, double y, double z, double t );

void R_SwapBuffers( int );

void R_RenderView( const viewParms_t* parms );

void R_AddMD3Surfaces( trRefEntity_t *e );

void R_AddPolygonSurfaces();

void R_AddDrawSurf( const surfaceType_t* surface, const shader_t* shader, int fogIndex );
void R_AddLitSurf( const surfaceType_t* surface, const shader_t* shader, int fogIndex );
void R_DecomposeSort( unsigned sort, int *entityNum, const shader_t **shader, int *fogNum );


#define	CULL_IN		0		// completely unclipped
#define	CULL_CLIP	1		// clipped by one or more planes
#define	CULL_OUT	2		// completely outside the clipping planes

int R_CullLocalBox( const vec3_t bounds[2] );
int R_CullPointAndRadius( const vec3_t origin, float radius );
int R_CullLocalPointAndRadius( const vec3_t origin, float radius );

void R_RotateForEntity( const trRefEntity_t* ent, const viewParms_t* viewParms, orientationr_t* orient );

/*
** GL wrapper/helper functions
*/
void GL_Bind( const image_t* image );
void GL_SelectTexture( int unit );
void GL_TextureMode( const char *string );
void GL_CheckErrors();
void GL_State( unsigned long stateVector );
void GL_TexEnv( int env );
void GL_Cull( int cullType );
void GL_Program();

#define GLS_SRCBLEND_ZERO						0x00000001
#define GLS_SRCBLEND_ONE						0x00000002
#define GLS_SRCBLEND_DST_COLOR					0x00000003
#define GLS_SRCBLEND_ONE_MINUS_DST_COLOR		0x00000004
#define GLS_SRCBLEND_SRC_ALPHA					0x00000005
#define GLS_SRCBLEND_ONE_MINUS_SRC_ALPHA		0x00000006
#define GLS_SRCBLEND_DST_ALPHA					0x00000007
#define GLS_SRCBLEND_ONE_MINUS_DST_ALPHA		0x00000008
#define GLS_SRCBLEND_ALPHA_SATURATE				0x00000009
#define		GLS_SRCBLEND_BITS					0x0000000f

#define GLS_DSTBLEND_ZERO						0x00000010
#define GLS_DSTBLEND_ONE						0x00000020
#define GLS_DSTBLEND_SRC_COLOR					0x00000030
#define GLS_DSTBLEND_ONE_MINUS_SRC_COLOR		0x00000040
#define GLS_DSTBLEND_SRC_ALPHA					0x00000050
#define GLS_DSTBLEND_ONE_MINUS_SRC_ALPHA		0x00000060
#define GLS_DSTBLEND_DST_ALPHA					0x00000070
#define GLS_DSTBLEND_ONE_MINUS_DST_ALPHA		0x00000080
#define		GLS_DSTBLEND_BITS					0x000000f0

#define GLS_DEPTHMASK_TRUE						0x00000100

#define GLS_POLYMODE_LINE						0x00001000

#define GLS_DEPTHTEST_DISABLE					0x00010000
#define GLS_DEPTHFUNC_EQUAL						0x00020000

#define GLS_ATEST_GT_0							0x10000000
#define GLS_ATEST_LT_80							0x20000000
#define GLS_ATEST_GE_80							0x40000000
#define		GLS_ATEST_BITS						0x70000000

#define GLS_DEFAULT			GLS_DEPTHMASK_TRUE

void	RE_StretchRaw (int x, int y, int w, int h, int cols, int rows, const byte *data, int client, qbool dirty);
void	RE_UploadCinematic (int w, int h, int cols, int rows, const byte *data, int client, qbool dirty);

void		RE_LoadWorldMap( const char *mapname );
void		RE_SetWorldVisData( const byte *vis );
qhandle_t	RE_RegisterModel( const char *name );
qhandle_t	RE_RegisterSkin( const char *name );

const char*	R_GetMapName();

qbool	R_GetEntityToken( char *buffer, int size );

model_t* R_AllocModel();

void	R_Init();
void	R_ConfigureVideoMode( int desktopWidth, int desktopHeight );	// writes to glConfig and glInfo

#define IMG_NOPICMIP 0x0001  // images that must never be downsampled
#define IMG_NOMIPMAP 0x0002  // 2D elements that will never be "distant" - implies IMG_NOPICMIP
#define IMG_NOIMANIP 0x0004  // used for math by shaders (normal maps etc) so don't imageprocess them
const image_t* R_FindImageFile( const char* name, int flags, int glWrapClampMode );
image_t* R_CreateImage( const char* name, byte* pic, int width, int height, GLenum format, int flags, int wrapClampMode );

void	R_SetColorMappings();

void	R_ImageList_f( void );
void	R_SkinList_f( void );

void	R_InitFogTable();
float	R_FogFactor( float s, float t );
void	R_InitImages();
void	R_DeleteTextures();
void	R_InitSkins();
const skin_t* R_GetSkinByHandle( qhandle_t hSkin );

const void *RB_TakeVideoFrameCmd( const void *data );

//
// tr_shader.c
//
qhandle_t RE_RegisterShader( const char* name );
qhandle_t RE_RegisterShaderNoMip( const char* name );
qhandle_t RE_RegisterShaderFromImage( const char* name, const image_t* image );

shader_t	*R_FindShader( const char *name, int lightmapIndex, qbool mipRawImage );
const shader_t* R_GetShaderByHandle( qhandle_t hShader );
void		R_InitShaders();
void		R_ShaderList_f( void );

/*
====================================================================

IMPLEMENTATION SPECIFIC FUNCTIONS

====================================================================
*/

// OpenGL initialization:
// - loading OpenGL and getting core function pointers
// - creating a window and changing video mode if needed,
//   respecting r_fullscreen, r_mode, r_width, r_height
// - creating a valid OpenGL context and making it current
// - filling up the right glconfig fields (see glconfig_t definition)
void	Sys_GL_Init();

// OpenGL shutdown:
// - unloading OpenGL and zeroing the core function pointers
// - destroying the GL context, window and other associated resources
// - resetting the proper video mode if necessary
void	Sys_GL_Shutdown();

// Swaps buffers and applies r_swapInterval. 
void	Sys_GL_EndFrame();


/*
====================================================================

TESSELATOR/SHADER DECLARATIONS

====================================================================
*/


struct stageVars_t
{
	color4ub_t	colors[SHADER_MAX_VERTEXES];
	vec2_t		texcoords[SHADER_MAX_VERTEXES];
};


struct shaderCommands_t
{
	ALIGN(16) glIndex_t indexes[SHADER_MAX_INDEXES];
	vec4_t		xyz[SHADER_MAX_VERTEXES];
	vec4_t		normal[SHADER_MAX_VERTEXES];
	vec2_t		texCoords[SHADER_MAX_VERTEXES][2];
	color4ub_t	vertexColors[SHADER_MAX_VERTEXES];

	enum { TP_BASE, TP_LIGHT } pass;

	stageVars_t	svars;

	const shader_t* shader;
	double		shaderTime;
	int			fogNum;

	int			numIndexes;
	int			numVertexes;

	const dlight_t* light;

	// info extracted from current shader
	void		(*siFunc)();
	const shaderStage_t** xstages;
};

extern shaderCommands_t tess;

void RB_BeginSurface( const shader_t* shader, int fogNum );
void RB_EndSurface();
void RB_CheckOverflow( int verts, int indexes );
#define RB_CHECKOVERFLOW(v,i) if (tess.numVertexes + (v) >= SHADER_MAX_VERTEXES || tess.numIndexes + (i) >= SHADER_MAX_INDEXES ) {RB_CheckOverflow(v,i);}

void R_ComputeColors( const shaderStage_t* pStage, stageVars_t& svars );
void R_ComputeTexCoords( const shaderStage_t* pStage, stageVars_t& svars );
void R_BindAnimatedImage( const textureBundle_t* bundle );

void RB_FogPass();
void RB_StageIteratorSky();

void RB_AddQuadStamp( vec3_t origin, vec3_t left, vec3_t up, byte *color );
void RB_AddQuadStampExt( vec3_t origin, vec3_t left, vec3_t up, byte *color, float s1, float t1, float s2, float t2 );

void RB_ShowImages();


/*
============================================================

WORLD MAP

============================================================
*/

void R_AddBrushModelSurfaces( const trRefEntity_t* re );
void R_AddWorldSurfaces();
qbool R_inPVS( const vec3_t p1, const vec3_t p2 );


/*
============================================================

FLARES

============================================================
*/

void R_ClearFlares( void );
void RB_AddFlare( void *surface, int fogNum, vec3_t point, vec3_t color, vec3_t normal );
void RB_AddDlightFlares( void );
void RB_RenderFlares (void);

/*
============================================================

LIGHTS

============================================================
*/

void R_SetupEntityLighting( const trRefdef_t *refdef, trRefEntity_t *ent );
void R_TransformDlights( int count, dlight_t* dl, const orientationr_t* orient );
qbool R_LightForPoint( const vec3_t point, vec3_t ambientLight, vec3_t directedLight, vec3_t lightDir );


/*
============================================================

SKIES

============================================================
*/

void R_InitSkyTexCoords( float cloudLayerHeight );


/*
============================================================

CURVE TESSELATION

============================================================
*/

#define PATCH_STITCHING

srfGridMesh_t *R_SubdividePatchToGrid( int width, int height,
								drawVert_t points[MAX_PATCH_SIZE*MAX_PATCH_SIZE] );
srfGridMesh_t *R_GridInsertColumn( srfGridMesh_t *grid, int column, int row, vec3_t point, float loderror );
srfGridMesh_t *R_GridInsertRow( srfGridMesh_t *grid, int row, int column, vec3_t point, float loderror );
void R_FreeSurfaceGridMesh( srfGridMesh_t *grid );

/*
============================================================

MARKERS, POLYGON PROJECTION ON WORLD POLYGONS

============================================================
*/

int R_MarkFragments( int numPoints, const vec3_t *points, const vec3_t projection,
		int maxPoints, vec3_t pointBuffer, int maxFragments, markFragment_t *fragmentBuffer );


/*
============================================================

SCENE GENERATION

============================================================
*/

// clears counters and back-end commands
void R_ClearFrame();

void RE_ClearScene();
void RE_AddRefEntityToScene( const refEntity_t *ent, qbool intShaderTime );
void RE_AddPolyToScene( qhandle_t hShader , int numVerts, const polyVert_t *verts, int num );
void RE_AddLightToScene( const vec3_t org, float radius, float r, float g, float b );
void RE_RenderScene( const refdef_t *fd );


/*
=============================================================

ANIMATED MODELS

=============================================================
*/

void	R_TransformModelToClip( const vec3_t src, const float *modelMatrix, const float *projectionMatrix,
							vec4_t eye, vec4_t dst );
void	R_TransformClipToWindow( const vec4_t clip, const viewParms_t *view, vec4_t normalized, vec4_t window );

void	RB_DeformTessGeometry();

void	RB_CalcEnvironmentTexCoords( float *dstTexCoords );
void	RB_CalcFogTexCoords( float *dstTexCoords );
void	RB_CalcScrollTexCoords( const float scroll[2], float *dstTexCoords );
void	RB_CalcRotateTexCoords( float rotSpeed, float *dstTexCoords );
void	RB_CalcScaleTexCoords( const float scale[2], float *dstTexCoords );
void	RB_CalcTurbulentTexCoords( const waveForm_t *wf, float *dstTexCoords );
void	RB_CalcTransformTexCoords( const texModInfo_t *tmi, float *dstTexCoords );
void	RB_CalcModulateColorsByFog( unsigned char *dstColors );
void	RB_CalcModulateAlphasByFog( unsigned char *dstColors );
void	RB_CalcModulateRGBAsByFog( unsigned char *dstColors );
void	RB_CalcWaveAlpha( const waveForm_t *wf, unsigned char *dstColors );
void	RB_CalcWaveColor( const waveForm_t *wf, unsigned char *dstColors );
void	RB_CalcAlphaFromEntity( unsigned char *dstColors );
void	RB_CalcAlphaFromOneMinusEntity( unsigned char *dstColors );
void	RB_CalcStretchTexCoords( const waveForm_t *wf, float *texCoords );
void	RB_CalcColorFromEntity( unsigned char *dstColors );
void	RB_CalcColorFromOneMinusEntity( unsigned char *dstColors );
void	RB_CalcSpecularAlpha( unsigned char *alphas );
void	RB_CalcDiffuseColor( unsigned char *colors );

/*
=============================================================

RENDERER BACK END FUNCTIONS

=============================================================
*/

void RB_ExecuteRenderCommands( const void *data );

/*
=============================================================

RENDERER BACK END COMMAND QUEUE

=============================================================
*/

#define	MAX_RENDER_COMMANDS	0x40000

typedef struct {
	byte	cmds[MAX_RENDER_COMMANDS];
	int		used;
} renderCommandList_t;

typedef struct {
	int		commandId;
	float	color[4];
} setColorCommand_t;

typedef struct {
	int		commandId;
} beginFrameCommand_t;

typedef struct {
	int		commandId;
	image_t	*image;
	int		width;
	int		height;
	void	*data;
} subImageCommand_t;

typedef struct {
	int		commandId;
} swapBuffersCommand_t;

typedef struct {
	int		commandId;
	int		buffer;
} endFrameCommand_t;

typedef struct {
	int		commandId;
	const shader_t* shader;
	float	x, y;
	float	w, h;
	float	s1, t1;
	float	s2, t2;
} stretchPicCommand_t;

typedef struct {
	int		commandId;
	const shader_t* shader;
	float	x0, y0;
	float	x1, y1;
	float	x2, y2;
	float	s0, t0;
	float	s1, t1;
	float	s2, t2;
} triangleCommand_t;

typedef struct {
	int		commandId;
	trRefdef_t	refdef;
	viewParms_t	viewParms;
	int numDrawSurfs;
	drawSurf_t* drawSurfs;
} drawSurfsCommand_t;

typedef struct {
	int commandId;
	int x;
	int y;
	int width;
	int height;
	const char* fileName;
	enum ss_type { SS_TGA, SS_JPG } type;
	float conVis;	// if > 0, this is a delayed screenshot and we need to 
					// restore the console visibility to that value
} screenshotCommand_t;

const void* RB_TakeScreenshotCmd( const screenshotCommand_t* cmd );

typedef struct {
	int						commandId;
	int						width;
	int						height;
	byte					*captureBuffer;
	byte					*encodeBuffer;
	qbool			motionJpeg;
} videoFrameCommand_t;

typedef enum {
	RC_END_OF_LIST,
	RC_SET_COLOR,
	RC_STRETCH_PIC,
	RC_TRIANGLE,
	RC_DRAW_SURFS,
	RC_BEGIN_FRAME,
	RC_SWAP_BUFFERS,
	RC_SCREENSHOT,
	RC_VIDEOFRAME
} renderCommand_t;


#define MAX_DLIGHTS		32			// completely arbitrary now  :D
#define MAX_REFENTITIES	1023		// can't be increased without changing drawsurf bit packing

// all of the information needed by the back-end must be
// contained in a backEndData_t instance
typedef struct {
	drawSurf_t	drawSurfs[MAX_DRAWSURFS];
	litSurf_t	litSurfs[MAX_DRAWSURFS];
	dlight_t	dlights[MAX_DLIGHTS];
	trRefEntity_t	entities[MAX_REFENTITIES];
	srfPoly_t	*polys;
	polyVert_t	*polyVerts;
	renderCommandList_t	commands;
} backEndData_t;

extern	int		max_polys;
extern	int		max_polyverts;

extern	backEndData_t	*backEndData;


void *R_GetCommandBuffer( int bytes );
void RB_ExecuteRenderCommands( const void *data );

void R_SyncRenderThread( void );

void R_AddDrawSurfCmd( drawSurf_t* drawSurfs, int numDrawSurfs );

void RE_BeginFrame( stereoFrame_t stereoFrame );
void RE_EndFrame( int* pcFE, int* pc2D, int* pc3D );
void RE_SetColor( const float* rgba );
void RE_StretchPic( float x, float y, float w, float h,
		float s1, float t1, float s2, float t2, qhandle_t hShader );
void RE_DrawTriangle( float x0, float y0, float x1, float y1, float x2, float y2,
		float s0, float t0, float s1, float t1, float s2, float t2, qhandle_t hShader );

int SaveJPGToBuffer( byte* out, int quality, int image_width, int image_height, byte* image_buffer );
void RE_TakeVideoFrame( int width, int height,
		byte *captureBuffer, byte *encodeBuffer, qbool motionJpeg );


///////////////////////////////////////////////////////////////


// the "public" glconfig: screen size etc
extern glconfig_t glConfig;

// the "private" glconfig: implementation specifics for the renderer
struct glinfo_t {
	// used by platform layer
	qbool	winFullscreen;			// the window takes the entire screen
	qbool	vidFullscreen;			// change the video mode
	int		displayFrequency;
	int		winWidth, winHeight;

	// used by renderer
	GLint	maxTextureSize;
	GLint	maxDrawElementsI;
	GLint	maxDrawElementsV;
	GLint	maxAnisotropy;
};

extern glinfo_t glInfo;


// renderer allocs are always on the low heap
template <class T> T* RI_New() { return (T*)ri.Hunk_Alloc(sizeof(T), h_low); }
template <class T> T* RI_New( size_t c ) { return static_cast<T*>(ri.Hunk_Alloc(sizeof(T) * c, h_low)); }

struct RI_AutoPtr {
	RI_AutoPtr() : mp(0) {}
	RI_AutoPtr( size_t c ) { mp = (byte*)ri.Hunk_AllocateTempMemory(c); }
	~RI_AutoPtr() { if (mp) ri.Hunk_FreeTempMemory(mp); }
	void* Alloc( size_t c ) { assert(!mp); mp = (byte*)ri.Hunk_AllocateTempMemory(c); return mp; }
	operator byte*() const { return mp; }
	template <typename T> T* Get() const { return (T*)mp; }
private:
	RI_AutoPtr( const RI_AutoPtr& rhs );
	RI_AutoPtr& operator=( const RI_AutoPtr& rhs );
	byte* mp;
};


// tr_gl2.cpp
qbool	GL2_Init();
void	GL2_SetupDynLight();
void	GL2_StageIterator();
void	GL2_BeginFrame();
void	GL2_EndFrame();


extern int re_cameraMatrixTime;

extern screenshotCommand_t	r_delayedScreenshot;
extern qbool				r_delayedScreenshotPending;
extern int					r_delayedScreenshotFrame;


#endif //TR_LOCAL_H
