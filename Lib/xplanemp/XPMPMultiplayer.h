/*
 * Copyright (c) 2004, Ben Supnik and Chris Serio.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#ifndef _XPLMMultiplayer_h_
#define _XPLMMultiplayer_h_

#include "XPLMDefs.h"

#ifndef XPMP_CLIENT_NAME
#define XPMP_CLIENT_NAME "A_PLUGIN"
#endif

#ifndef XPMP_CLIENT_LONGNAME
#define XPMP_CLIENT_LONGNAME "A Plugin"
#endif

#ifdef __cplusplus
extern "C" {
#endif


/************************************************************************************
 * X-PLANE MULTIPLAYER
 ************************************************************************************/

/*
	Multiplayer - THEORY OF OPERATION
	
	The multiplayer API allows plug-ins to control aircraft visible to other plug-ins and
	the user via x-plane.  It effectively provides glue between a series of observers
	that wish to render or in other ways act upon those planes.
	
	A plug-in can control zero or more planes, and zero or more plug-ins can control planes.
	However, each plane is controlled by exactly one plug-in.  A plug-in thus dynamically
	allocates planes to control.  A plug-in registers a callback which is used to pull
	information.  The plug-in may decide to not return information or state that that
	information is unchanged.
	
	A plug-in can also read the current aircrafts or any of their data.  Aircraft data is
	cached to guarantee minimum computing of data.
	
	Each 'kind' of data has an enumeration and corresponding structure.*/


/************************************************************************************
 * PLANE DATA TYPES
 ************************************************************************************/

/*
 * XPMPPosition_t
 *
 * This data structure contains the basic position info for an aircraft.
 * Lat and lon are the position of the aircraft in the world.  They are double-precision to
 * provide reasonably precise positioning anywhere.
 * Elevation is in feet above mean sea level.
 *
 * Pitch, roll, and heading define the aircraft's orientation.  Heading is in degrees, positive
 * is clockwise from north.  Pitch is the number of degrees, positive is nose up, and roll
 * is positive equals roll right.
 *
 * Offset scale should be between 0 & 1 and indicates how much of the surface
 * contact correction offset should be applied.  1 is fully corrected, 0 is no
 * correction.  This is so XSB can blend the correction out as the aircraft
 * leaves circling altitude.
 *
 * clampToGround enables the ground-clamping inside of libxplanemp.  If false,
 * libxplanemp will not clamp this particular aircraft.
 *
 * Note that there is no notion of aircraft velocity or acceleration; you will be queried for
 * your position every rendering frame.  Higher level APIs can use velocity and acceleration.
 */
typedef	struct {
	long	size;
	double	lat;
	double	lon;
	double	elevation;
	float	pitch;
	float	roll;
	float	heading;
	char 	label[32];
	float 	offsetScale;
	bool 	clampToGround;
    int     aiPrio = 0;     // Priority for AI/TCAS consideration, the lower the earlier
    float   label_color[4] = {1, 1, 0, 1};  // label base color
} XPMPPlanePosition_t;


/*
 * XPMPLightStatus
 *
 * This enum defines the settings for the lights bitfield in XPMPPlaneSurfaces_t
 *
 * The upper 16 bit of the light code (timeOffset) should be initialized only once
 * with a random number by the application. This number will be used to have strobes
 * flashing at different times.
 */
union xpmp_LightStatus {
	unsigned int lightFlags;
	struct {
		unsigned int timeOffset	: 16;

		unsigned int taxiLights : 1;
		unsigned int landLights	: 1;
		unsigned int bcnLights	: 1;
		unsigned int strbLights	: 1;
		unsigned int navLights	: 1;
		
		unsigned int flashPattern   : 4;
	};
};

/*
 * Light flash patterns
 */
enum {
	xpmp_Lights_Pattern_Default		= 0,	// Jets: one strobe flash, short beacon (-*---*---*---)
	xpmp_Lights_Pattern_EADS		= 1,	// Airbus+EADS: strobe flashes twice (-*-*-----*-*--), short beacon
	xpmp_Lights_Pattern_GA			= 2		// GA: one strobe flash, long beacon (-*--------*---)
};


/*
 * XPMPPlaneSurfaces_t
 *
 * This data structure will contain information about the external physical configuration of the plane,
 * things you would notice if you are seeing it from outside.  This includes flap position, gear position,
 * etc.
 *
 * Lights is a 32 bit field with flags as defined in XPMPLightStatus
 *
 */
typedef	struct {
	long					size;
	float                 gearPosition;
	float                 flapRatio;
	float                 spoilerRatio;
	float                 speedBrakeRatio;
	float                 slatRatio;
	float                 wingSweep;
	float                 thrust;
	float                 yokePitch;
	float                 yokeHeading;
	float                 yokeRoll;
	xpmp_LightStatus      lights;
} XPMPPlaneSurfaces_t;


/*
 * XPMPTransponderMode
 *
 * These enumerations define the way the transponder of a given plane is operating.
 *
 */
enum {
	xpmpTransponderMode_Standby,
	xpmpTransponderMode_Mode3A,
	xpmpTransponderMode_ModeC,
	xpmpTransponderMode_ModeC_Low,
	xpmpTransponderMode_ModeC_Ident
};
typedef	int	XPMPTransponderMode;

/*
 * XPMPPlaneRadar_t
 *
 * This structure defines information about an aircraft visible to radar.  Eventually it can include
 * information about radar profiles, stealth technology, radar jamming, etc.
 *
 */
typedef	struct {
	long					size;
	long					code;
	XPMPTransponderMode		mode;
} XPMPPlaneRadar_t;

/*
 * XPMPPlaneData
 *
 * This enum defines the different categories of aircraft information we can query about.
 *
 */
enum {
	xpmpDataType_Position 	= 1L << 1,
	xpmpDataType_Surfaces 	= 1L << 2,
	xpmpDataType_Radar 		= 1L << 3
};
typedef	int			XPMPPlaneDataType;

/*
 * XPMPPlaneCallbackResult
 *
 * This definfes the different responses to asking for information.
 *
 */
enum {
	xpmpData_Unavailable = 0,	/* The information has never been specified. */
	xpmpData_Unchanged = 1,		/* The information from the last time the plug-in was asked. */
	xpmpData_NewData = 2		/* The information has changed this sim cycle. */
};
typedef	int			XPMPPlaneCallbackResult;

/*
 * XPMPPlaneID
 *
 * This is a unique ID for an aircraft created by a plug-in.
 *
 */
typedef	void *		XPMPPlaneID;

/************************************************************************************
* Some additional functional by den_rain
************************************************************************************/

void actualVertOffsetInfo(const char *inMtl, char *outType, double *outOffset);
void setUserVertOffset(const char *inMtlCode, double inOffset);
void removeUserVertOffset(const char *inMtlCode);


/************************************************************************************
 * PLANE CREATION API
 ************************************************************************************/

/*
 * XPMPPlaneData_f
 *
 * This is the aircraft data providing function.  It is called no more than once per sim
 * cycle per data type by the plug-in manager to get data about your plane.  The data passed
 * in is a pointer to one of the above structures.  The function specifies the datatype, and the
 * last data you provided is passed in.
 *
 */
typedef	XPMPPlaneCallbackResult (* XPMPPlaneData_f)(
		XPMPPlaneID			inPlane,
		XPMPPlaneDataType	inDataType,
		void *				ioData,
		void *				inRefcon);

/*
 * XPMPMultiplayerInitLegacyData
 *
 * This routine initializes legacy portions of the multiplayer library.
 *
 * inPlaneList is a ptr to a fully qualified file name that is a text file of ICAO -> model
 * mappings.  The xsb_aircraft.txt file can be used as a template.  (Please note: other
 * XSB files are NOT necessary and are not necessarily available under open source license.)
 *
 * inDoc8643 is the path to the ICAO document 8643, available at
 * http://www.icao.int/anb/ais/TxtFiles/Doc8643.txt. This file lists all aircraft types
 * with their manufacturer, ICAO equipment code and aircraft category.
 *
 * The two prefs funcs each take an ini section and key and return a value and also take
 * a default.  The renderer uses them for configuration.  Currently the following keys are
 * needed:
 *
 * section	key					type	default	description
 * planes	full_distance		float	3.0
 * planes	max_full_count		int		50
 *
 * The return value is a string indicating any problem that may have gone wrong in a human-readable
 * form, or an empty string if initalizatoin was okay.
 *
 * Call this once from your XPluginStart routine.
 *
 * Depending on which plane packages are installed this can take between 30 seconds and 15 minutes.
 *
 * After transitioning to exclusively OBJ8-based packages, this function should no longer be required.
 * Instead, make separate calls to XPMPLoadCSLPackages and XPMPSetDefaultPlaneICAO.
 *
 */
const char *	XPMPMultiplayerInitLegacyData(
		const char * inCSLFolder,
		const char * inRelatedPath,
		const char * inTexturePath,
		const char * inDoc8643,
		const char * inDefaltICAO,
		int (* inIntPrefsFunc)(const char *, const char *, int),
		float (* inFloatPrefsFunc)(const char *, const char *, float));

/*
 * XPMPMultiplayerInit
 *
 * The two prefs funcs each take an ini section and key and return a value and also take
 * a default.  The renderer uses them for configuration.  Currently the following keys are
 * needed:
 *
 * section	key					type	default	description
 * planes	full_distance		float	3.0
 * planes	max_full_count		int		50
 * 
 * Additionally takes a string path to the resource directory of the calling plugin for storing the
 * user vertical offset config file.
 *
 * The return value is a string indicating any problem that may have gone wrong in a human-readable
 * form, or an empty string if initalizatoin was okay.
 *
 * Call this once, typically from your XPluginStart routine.
 *
 */
const char *    XPMPMultiplayerInit(
		int (* inIntPrefsFunc)(const char *, const char *, int),
		float (* inFloatPrefsFunc)(const char *, const char *, float),
		const char * resourceDir);

/*
 * XPMPMultiplayerEnable
 *
 * Enable drawing of multiplayer planes.  Call this once from your XPluginEnable routine to
 * grab multiplayer; an empty string is returned on success, or a human-readable error message
 * otherwise.
 *
 */
const char *	XPMPMultiplayerEnable(void);

/*
 * XPMPMultiplayerOBJ7SupportEnable
 *
 * Sets the light texture to use for old OBJ7 models and initializes the required OpenGL hooks 
 * for OBJ7 rendering. An empty string is returned on success, or a human-readable error message
 * otherwise. Calling this function is required if you are going to use OBJ7 CSLs.
 */
const char * XPMPMultiplayerOBJ7SupportEnable(const char * inTexturePath);

/*
 * XPMPMultiplayerDisable
 *
 * Disable drawing of multiplayer planes.  Call this from XPluginDisable to release multiplayer.
 * Reverses the actions on XPMPMultiplayerEnable.
 */
void XPMPMultiplayerDisable(void);

/*
 * XPMPMultiplayerCleanup
 *
 * Clean up the multiplayer library. Call this from XPluginStop to reverse the actions of
 * XPMPMultiplayerInit as much as possible.
 */
void XPMPMultiplayerCleanup(void);
    
/*
 * XPMPHasControlOfAIAircraft
 *
 * Does XPMP control AI aircrafts (after a call to XPMPMultiplayerEnable)
 * and, hence, can fake TCAS display?
 */
bool XPMPHasControlOfAIAircraft(void);

/*
 * XPMPLoadCSLPackage
 *
 * Loads a collection of planes
 *
 * inPlaneList is a ptr to a fully qualified file name that is a text file of ICAO -> model
 * mappings.  The xsb_aircraft.txt file can be used as a template.  (Please note: other
 * XSB files are NOT necessary and are not necessarily available under open source license.)
 *
 * inDoc8643 is the path to the ICAO document 8643, available at
 * http://www.icao.int/anb/ais/TxtFiles/Doc8643.txt. This file lists all aircraft types
 * with their manufacturer, ICAO equipment code and aircraft category.
 *
 * This is fast if the planes are all OBJ8 (because the objects are loaded asynchronously,
 * on demand), otherwise it could take several seconds or minutes depending on the quantity
 * and fidelity of the planes.
 *
 */
const char *	XPMPLoadCSLPackage(
		const char * inCSLFolder,
		const char * inRelatedPath,
		const char * inDoc8643);

/*
 * XPMPLoadPlanesIfNecessary
 *
 * This routine checks what planes are loaded and loads any that we didn't get.
 * Call it after you oare enabled if it isn't the first time to set up models.
 *
 */
void			XPMPLoadPlanesIfNecessary(void);

/*
 * XPMPGetNumberOfInstalledModels
 *
 * This routine returns the number of found models.
 *
 */
int XPMPGetNumberOfInstalledModels(void);

/*
 * XPMPGetModelInfo
 *
 * Call this routine with an index to get all available info for this model. Valid
 * index is between 0 and XPMPGetNumberOfInstalledModels(). If you pass an index
 * out of this range, the out parameters are unchanged.
 * Make sure the size of all char arrays is big enough.
 *
 */
void XPMPGetModelInfo(int inIndex, const char **outModelName, const char **outIcao, const char **outAirline, const char **outLivery);

/*
 * XPMPCreatePlane
 *
 * This function creates a new plane for a plug-in and returns it.  Pass in an ICAO aircraft ID code,
 * a livery string and a data function for fetching dynamic information.
 *
 */
XPMPPlaneID	XPMPCreatePlane(
		const char *			inICAOCode,
		const char *			inAirline,
		const char *			inLivery,
		XPMPPlaneData_f			inDataFunc,
		void *					inRefcon);

XPMPPlaneID	XPMPCreatePlaneWithModelName(
		const char *			inModelName,
		const char *			inICAOCode,
		const char *			inAirline,
		const char *			inLivery,
		XPMPPlaneData_f			inDataFunc,
		void *                  inRefcon);

/*
 * XPMPDestroyPlane
 *
 * This function deallocates a created aircraft.
 *
 */
void			XPMPDestroyPlane(XPMPPlaneID);

/*
 * XPMPChangePlaneModel
 *
 * This routine lets you change an aircraft's model.  This can be useful if a remote
 * player changes planes or new information comes over the network asynchronously.
 *
 * this function returns an integer which, abstractly, represents the match quality.
 *
 * lower numbers are better, 2 or lower indicates an exact match on model.
 * negative values indicate failure to match at all.
 *
 */
int 	XPMPChangePlaneModel(
		XPMPPlaneID				inPlaneID,
		const char *			inICAOCode,
		const char *			inAirline,
		const char *			inLivery);

/*
 * XPMPGetPlaneModelName
 *
 * Return the name of the model in use
 * Returns required buf size, i.e. length of description.
 * Negative values indicate failure (wrong PlaneID).
 *
 */
int     XPMPGetPlaneModelName(
        XPMPPlaneID             inPlaneID,
        char *                  outTxtBuf,
        int                     outTxtBufSize);
    
    
/*
 * XPMPSetDefaultPlaneICAO
 *
 * This routine controls what ICAO is used as a backup search criteria for a not-found plane.
 *
 */
void	XPMPSetDefaultPlaneICAO(
		const char *			inICAO);

/************************************************************************************
 * PLANE OBSERVATION API
 ************************************************************************************/

/*
 * XPMPPlaneNotification
 *
 * These are the various notifications you receive when you register a notifier
 * function.
 *
 */
enum {
	xpmp_PlaneNotification_Created 			= 1,
	xpmp_PlaneNotification_ModelChanged 	= 2,
	xpmp_PlaneNotification_Destroyed	 	= 3
};
typedef int XPMPPlaneNotification;

/*
 * XPMPPlaneNotifier_f
 *
 * You can pass a notifier to find out when a plane is created or destroyed or other
 * data changes.
 *
 */
typedef	void (* XPMPPlaneNotifier_f)(
		XPMPPlaneID				inPlaneID,
		XPMPPlaneNotification	inNotification,
		void *					inRefcon);

/*
 * XPMPCountPlanes
 *
 * This function returns the number of planes in existence.
 *
 */
long			XPMPCountPlanes(void);

/*
 * XPMPGetNthPlane
 *
 * This function returns the plane ID of the Nth plane.
 *
 */
XPMPPlaneID	XPMPGetNthPlane(
		long 					index);


/*
 * XPMPGetPlaneICAOAndLivery
 *
 * Given a plane, this function returns optionally its ICAO code or livery.  Pass string buffers
 * or NULL if you do not want the information.
 *
 */
void			XPMPGetPlaneICAOAndLivery(
		XPMPPlaneID				inPlane,
		char *					outICAOCode,	// Can be NULL
		char *					outLivery);		// Can be NULL

/*
 * XPMPRegisterPlaneCreateDestroyFunc
 *
 * This function registers a notifier functionfor obeserving planes being created and destroyed.
 *
 */
void			XPMPRegisterPlaneNotifierFunc(
		XPMPPlaneNotifier_f		inFunc,
		void *					inRefcon);

/*
 * XPMPUnregisterPlaneCreateDestroyFunc
 *
 * This function canceles a registration for a notifier functionfor obeserving
 * planes being created and destroyed.
 */
void			XPMPUnregisterPlaneNotifierFunc(
		XPMPPlaneNotifier_f		inFunc,
		void *					inRefcon);

/*
 * XPMPGetPlaneData
 *
 * This function fetches specific data about a plane in the sim.  Pass in a plane ID, a data type
 * and a pointer to a struct for the data.  The struct's size field must be filled in!  The data
 * will be returned if possible, as well as an enum code indicating whether we are returning new
 * data, old data, or we have no data at all.
 *
 */
XPMPPlaneCallbackResult XPMPGetPlaneData(
		XPMPPlaneID					inPlane,
		XPMPPlaneDataType			inDataType,
		void *						outData);

/*
 * XPMPIsICAOValid
 *
 * This functions searches through our global vector of valid ICAO codes and returns true if there
 * was a match and false if there wasn't.
 *
 */
bool			XPMPIsICAOValid(
		const char *				inICAO);

/*
 * XPMPGetPlaneModelQuality
 *
 * This function returns the quality level for the nominated plane's
 * current model.
 */
int 		XPMPGetPlaneModelQuality(
		XPMPPlaneID 				inPlane);

/*
 * XPMPModelMatchQuality
 *
 * This functions searches through our model list and returns the pass
 * upon which a match was found, and -1 if one was not.
 *
 * This can be used for assessing if it's worth using a partial update
 * to update the model vs previous efforts.
 */
int			XPMPModelMatchQuality(
		const char *				inICAO,
		const char *				inAirline,
		const char *				inLivery);

/************************************************************************************
 * PLANE RENDERING API
 ************************************************************************************/

/*
 * XPMPRenderPlanes_f
 *
 * You can register a callback to draw planes yourself.  If you do this, the XPMP will not
 * draw multiplayer planes out the cockpit window; do it yourself!  Use the data access API
 * to get plane info and then draw them.  You are responsible for all planes.
 *
 */
typedef	void (* XPMPRenderPlanes_f)(
		int							inIsBlend,
		void * 						inRef);

/*
 * XPMPSetPlaneRenderer
 *
 * This function setse the plane renderer.  You can pass NULL for the function to restore
 * the default renderer.
 *
 */
void		XPMPSetPlaneRenderer(
		XPMPRenderPlanes_f  		inRenderer,
		void * 						inRef);

/*
 * XPMPDumpOneCycle
 *
 * This causes the plane renderer implementation to dump debug info to the error.out for one
 * cycle after it is called - useful for figuring out why your models don't look right.
 *
 */
void		XPMPDumpOneCycle(void);

/*
 * XPMPEnableAircraftLabels
 * XPMPDisableAircraftLabels
 *
 * These functions enable and disable the drawing of aircraft labels above the aircraft
 *
 */
void				  XPMPEnableAircraftLabels(void);

void				  XPMPDisableAircraftLabels(void);

bool				  XPMPDrawingAircraftLabels(void);

#ifdef __cplusplus
}
#endif


#endif
