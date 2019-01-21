#ifndef _XPCAircraft_h_
#define _XPCAircraft_h_

#include "XPMPMultiplayer.h"

class	XPCAircraft {
public:
	
	XPCAircraft(
			const char *			inICAOCode,
			const char *			inAirline,
			const char *			inLivery);
	virtual							~XPCAircraft();
	
	virtual	XPMPPlaneCallbackResult	GetPlanePosition(
			XPMPPlanePosition_t *	outPosition)=0;

	virtual	XPMPPlaneCallbackResult	GetPlaneSurfaces(
			XPMPPlaneSurfaces_t *	outSurfaces)=0;

	virtual	XPMPPlaneCallbackResult	GetPlaneRadar(
			XPMPPlaneRadar_t *	outRadar)=0;
protected:

	XPMPPlaneID			mPlane;

	static	XPMPPlaneCallbackResult	AircraftCB(
			XPMPPlaneID			inPlane,
			XPMPPlaneDataType	inDataType,
			void *				ioData,
			void *				inRefcon);

};	

#endif
