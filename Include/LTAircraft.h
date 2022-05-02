/// @file       LTAircraft.h
/// @brief      LTAircraft represents an individual tracked aircraft drawn into X-Plane's sky
/// @details    Defines helper classes MovingParam, AccelParam for flght parameters
///             that change in a controlled way (like flaps, roll, speed).\n
///             LTAircraft::FlightModel provides configuration values controlling flight modelling.\n
///             LTAircraft calculates the current position and configuration of the aircraft
///             in every flighloop cycle while being called from libxplanemp.
/// @author     Birger Hoppe
/// @copyright  (c) 2018-2020 Birger Hoppe
/// @copyright  Permission is hereby granted, free of charge, to any person obtaining a
///             copy of this software and associated documentation files (the "Software"),
///             to deal in the Software without restriction, including without limitation
///             the rights to use, copy, modify, merge, publish, distribute, sublicense,
///             and/or sell copies of the Software, and to permit persons to whom the
///             Software is furnished to do so, subject to the following conditions:\n
///             The above copyright notice and this permission notice shall be included in
///             all copies or substantial portions of the Software.\n
///             THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
///             IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
///             FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
///             AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
///             LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
///             OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
///             THE SOFTWARE.


#ifndef LTAircraft_h
#define LTAircraft_h

//
//MARK: MovingParam
//      Represents a parameter which changes over time, like e.g.
//      "gear", which takes some seconds to go up or down
//
struct MovingParam
{
public:
    // defining parameters
    double defMin, defMax, defDist, defDuration;
    // wrap around at max, i.e. start over at begin?
    // (good for heading, which goes from 0 to 360)
    const bool bWrapAround;
protected:
    // target values (tTime is NaN if we are _not_ moving
    double valFrom, valTo, valDist, timeFrom, timeTo;
    // increase ot decrease values? (really meaningful only if bWrapAround)
    bool bIncrease;
    // actual value
    double val;
    
public:
    // Constructor
    MovingParam(double _dur, double _max=1.0, double _min=0.0,
                bool _wrap_around=false);
    void SetVal (double _val);
    
    // are we in motion? (i.e. moving from val to target?)
    bool inMotion () const;
    // is a move programmed or already in motion?
    bool isProgrammed () const;

    // start a move to the given target value
    void moveTo ( double tval, double _startTS=NAN );
    inline void up   (double _startTS=NAN) { moveTo(defMin, _startTS); }
    inline void down (double _startTS=NAN) { moveTo(defMax, _startTS); }
    inline void half (double _startTS=NAN) { moveTo((defMin+defMax)/2, _startTS); }
    inline void min  (double _startTS=NAN) { moveTo(defMin, _startTS); }
    inline void max  (double _startTS=NAN) { moveTo(defMax, _startTS); }

    // pre-program a move, which is to start or finish by the given time
    void moveToBy (double _from, bool _increase, double _to,
                   double _startTS, double _by_ts,
                   bool _startEarly);
    // pre-program a quick move the shorter way (using wrap around if necessary)
    void moveQuickestToBy (double _from,        // NAN = current val
                           double _to,
                           double _startTS,     // NAN = now
                           double _by_ts,       // when finished with move?
                           bool _startEarly);   // start at _startTS? or finish at _by_ts?

    // get current value (might actually _change_ val if inMotion!)
    double get ();
    
    // non-moving status checks
    inline double is () const       { return val; }
    inline bool isUp () const       { return val <= defMin; }
    inline bool isDown () const     { return val >= defMax; }
    inline bool isIncrease () const { return bIncrease; }
    inline double fromVal () const  { return valFrom; }
    inline double toVal () const    { return valTo; }
    inline double dist () const     { return valDist; }
    inline double fromTS () const   { return timeFrom; }
    inline double toTS () const     { return timeTo; }
    double percDone () const;       ///< percent done of move, returns 1.0 if not in motion
};

// mimics acceleration / deceleration
struct AccelParam
{
protected:
    double startSpeed, targetSpeed, acceleration, targetDeltaDist;
    double startTime, accelStartTime, targetTime;
    double currSpeed_m_s, currSpeed_kt;      // set during getSpeed
public:
    // default only allows for object init
    AccelParam();
    // Set start/target [m/s], but no acceleration
    void SetSpeed (double speed);
    
    // get current value
    double m_s() const { return currSpeed_m_s; }
    double kt() const { return currSpeed_kt; }
    bool isZero() const { return currSpeed_m_s <= 0.01; }
    
    // start an acceleration now
    void StartAccel(double startSpeed, double targetSpeed, double accel,
                    double startTime=NAN);
    // reach target Speed by targetTime after deltaDist
    void StartSpeedControl(double startSpeed, double targetSpeed,
                           double deltaDist,
                           double startTime, double targetTime,
                           const LTAircraft* pAc);
    
    inline bool isChanging() const { return !std::isnan(acceleration); }
    
    // calculations (ts = timestamp, defaults to current sim time)
    double updateSpeed ( double ts = NAN );
    double getDeltaDist ( double ts = NAN ) const;
    double getRatio ( double ts = NAN ) const;
    inline double getTargetTime() const         { return targetTime; }
    inline double getTargetDeltaDist() const    { return targetDeltaDist; }
};

/// @brief Handles a quadratic Bezier curve based on flight data positions
/// @details Only using quadratic curves because in higher-level Bezier curves the parameter `t`
///          does no longer correspond well to distance and planes would appear slowing down
///          at beginning and end.
/// @details The constructors take positions from flight data,
///          the necessary end and control points of a Bezier Curve
///          are computed from that input.
/// @see https://en.wikipedia.org/wiki/B%C3%A9zier_curve#Constructing_B%C3%A9zier_curves
struct BezierCurve
{
protected:
    positionTy start;           ///< start point of the actual Bezier curve
    positionTy end;             ///< end point of the actual Bezier curve
    ptTy ptCtrl;                ///< Control point of the curve
public:
    BezierCurve () {}           ///< Standard constructor does nothing
    
    /// @brief Define a quadratic Bezier Curve based on the given flight data positions
    /// @param _start Start position of the Bezier curve
    /// @param _mid Mid position, current leg's end and next leg's starting point, the turning point, used as Bezier control point, ie. will not be reached
    /// @param _end End position of the curve
    void Define (const positionTy& _start,
                 const positionTy& _mid,
                 const positionTy& _end);
    
    /// @brief Define a quadratic Bezier Curve based on the given flight data positions, with the mid point being the intersection of the vectors
    /// @param _start Start position of the Bezier curve
    /// @param _end End position of the curve
    /// @return Could a reasonable mid point be derived and hence a Bezier curve be set up?
    bool Define (const positionTy& _start,
                 const positionTy& _end);
    
    /// Convert the geographic coordinates to meters, with `start` being the origin (0|0) point
    /// This is needed for accurate angle calculations
    void ConvertToMeter ();
    /// Convert the given geographic coordinates to meters
    void ConvertToMeter (ptTy& pt) const;

    /// Convert the given position back to geographic coordinates
    void ConvertToGeographic (ptTy& pt) const;
    
    /// Clear the definition, so that BezierCurve::isDefined() will return `false`
    void Clear ();
    /// Is a curve defined?
    bool isDefined () const { return ptCtrl.isValid(); }
    /// is defined and the given timestamp between start's and end's timestamp?
    bool isTsInbetween (double _ts) const
    { return isDefined() && start.ts() <= _ts && _ts <= end.ts(); }
    /// is defined and the given timestamp before end's timestamp?
    bool isTsBeforeEnd (double _ts) const
    { return isDefined() && _ts <= end.ts(); }

    /// Return the position as per given timestamp, if the timestamp is between `start` and `end`
    /// @param[in,out] pos Current position, to be overwritten with new position
    /// @param _calcTs Timestamp for the position we look for, used to calculate factor `f`
    /// @return if the position was adjusted
    bool GetPos (positionTy& pos, double _calcTs);

    /// Debug text output
    std::string dbgTxt() const;
};

//
//MARK: LTAircraft
//      Represents an aircraft as displayed in XP by use of the
//      XP Multiplayer Lib
//
class LTAircraft : public XPMP2::Aircraft
{
public:
    class FlightModel {
    public:
        std::string modelName;
        double GEAR_DURATION =    10;     // time for gear up/down
        double GEAR_DEFLECTION =  0.5;    // [m] main gear deflection on meters during touch down
        double FLAPS_DURATION =   5;      // time for full flaps extension from 0% to 100%
        double VSI_STABLE =       100;    // [ft/min] less than this VSI is considered 'stable'
        double ROTATE_TIME =      4;      // [s] to rotate before lift off
        double VSI_FINAL =        -800;   // [ft/min] assumed vsi for final if vector unavailable
        double VSI_INIT_CLIMB =   1500;   // [ft/min] assumed vsi if take-off-vector not available
        double SPEED_INIT_CLIMB = 150;    // [kt] initial climb speed if take-off-vector not available
        double VSI_MAX =          4000;   // [ft/min] maximum vertical speed, beyond this considered invalid data
        double AGL_GEAR_DOWN =    1600;   // height AGL at which to lower the gear during approach
        double AGL_GEAR_UP =      100;    // height AGL at which to raise the gear during take off
        double AGL_FLARE =        25;     // [ft] height AGL to start flare in artifical pos mode
        double MAX_TAXI_SPEED =   45;     // below that: taxi, above that: take-off/roll-out
        double MIN_REVERS_SPEED = 80;     // [kn] User reversers down to this speed
        double TAXI_TURN_TIME =   30;     // seconds for a 360° turn on the ground
        double FLIGHT_TURN_TIME = 120;    ///< seconds for a typical 360° turn in flight
        double MIN_FLIGHT_TURN_TIME=60;   ///< [s] minimum allowable time for a 360° turn in flight
        double ROLL_MAX_BANK =    30;     // [°] max bank angle
        double ROLL_RATE =        5;      // [°/s] roll rate in normal turns
        double MIN_FLIGHT_SPEED = 100;    // [kn] minimum flight speed, below that not considered valid data
        double FLAPS_UP_SPEED =   180;    // below that: initial climb, above that: climb
        double FLAPS_DOWN_SPEED = 200;    // above that: descend, below that: approach
        double MAX_FLIGHT_SPEED = 600;    // [kn] maximum flight speed, above that not considered valid data
        double CRUISE_HEIGHT =    15000;  // above that height AGL we consider level flight 'cruise'
        double ROLL_OUT_DECEL =  -2.0;    // [m/s²] deceleration during roll-out
        double PITCH_MIN =        -2;     // [°] minimal pitch angle (aoa)
        double PITCH_MIN_VSI =    -1000;  // [ft/min] minimal vsi below which pitch is MDL_PITCH_MIN
        double PITCH_MAX =        15;     // [°] maximum pitch angle (aoa)
        double PITCH_MAX_VSI =    2000;   // [ft/min] maximum vsi above which pitch is MDL_PITCH_MAX
        double PITCH_FLAP_ADD =   4;      // [°] to add if flaps extended
        double PITCH_FLARE =      10;     // [°] pitch during flare
        double PITCH_RATE =       3;      // [°/s] pitch rate of change
        double PROP_RPM_MAX =     1200;   // [rpm] maximum propeller revolutions per minute
        double LIGHT_LL_ALT =     100000; // [ft] Landing Lights on below this altitude; set zero for climb/approach only (GA)
        float  LABEL_COLOR[4] = {1.0f, 1.0f, 0.0f, 1.0f};   // base color of a/c label
        double EXT_CAMERA_LON_OFS =-45;   // longitudinal external camera offset
        double EXT_CAMERA_LAT_OFS =  0;   // lateral...
        double EXT_CAMERA_VERT_OFS= 20;   // vertical...
        
        // modelName is key, so base comparison on it
    public:
        bool operator == (const FlightModel& o) const { return modelName == o.modelName; }
        bool operator <  (const FlightModel& o) const { return modelName <  o.modelName; }
        bool operator >  (const FlightModel& o) const { return modelName >  o.modelName; }
        operator bool () const { return !modelName.empty(); }
        /// Calculate max possible heading change in the time given [s] based on turn speed (max return: 180.0)
        double maxHeadChange (bool bOnGnd, double time_s) const;
        /// Is this modelling a glider?
        bool isGlider () const;
        
    public:
        static bool ReadFlightModelFile ();
        /// @brief Returns a model based on pAc's type, fd.statData's type or by trying to derive a model from statData.mdlName
        /// @param fd Flight Data of the plane in question, might be updated with found model
        /// @param bForceSearch (optional) If `true` then no cached values are returned but a full search in the model rules is done
        /// @param[out] pIcaoType (optional) receives determined ICAO type, empty if none could be determined
        static const FlightModel& FindFlightModel (LTFlightData& fd,
                                                   bool bForceSearch = false,
                                                   const std::string** pIcaoType = nullptr);
        static const FlightModel* GetFlightModel (const std::string& modelName);
        /// Tests if the given call sign matches typical call signs of ground vehicles
        static bool MatchesCar (const std::string& _callSign);
    };
    
public:
    static std::string FlightPhase2String (flightPhaseE phase);

public:
    // reference to the defining flight data
    LTFlightData& fd;
    /// Pointer to the flight model being used
    const FlightModel* pMdl;
    // pointer to the matching Doc8643
    const Doc8643* pDoc8643;
    
    // absolute positions (max 3: last, current destination, next)
    // as basis for calculating ppos per frame
    dequePositionTy      posList;
    
    std::string         labelInternal;  // internal label, e.g. for error messages
protected:
    // this is "ppos", the present simulated position,
    // where the aircraft is to be drawn
    positionTy          ppos;
    // and this the current vector from 'from' to 'to'
    vectorTy            vec;
    
    // timestamp we last requested new positions from flight data
    double              tsLastCalcRequested;
    
    // dynamic parameters of the plane
    flightPhaseE         phase;          // current flight phase
    double              rotateTs;       // when to rotate?
    double              vsi;            // vertical speed (ft/m)
    bool                bOnGrnd;        // are we touching ground?
    bool                bArtificalPos;  // running on artifical positions for roll-out?
    bool                bNeedSpeed = false;     ///< need speed calculation?
    bool                bNeedCCBezier = false;  ///< need Bezier calculation due to cut-corner case?
    AccelParam          speed;          // current speed [m/s] and acceleration control
    BezierCurve         turn;           ///< position, heading, roll while flying a turn
    MovingParam         heading;        ///< heading movement if not using a Bezier curve
    MovingParam         corrAngle;      ///< correction angle for cross wind
    MovingParam         gear;
    MovingParam         flaps;
    MovingParam         pitch;
    MovingParam         reversers;      ///< reverser open ratio
    MovingParam         spoilers;       ///< spoiler extension ratio
    MovingParam         tireRpm;        ///< models slow-down after take-off
    MovingParam         gearDeflection; ///< main gear deflection in meters during touch-down
    
    // Y-Probe
    double              probeNextTs;    // timestamp of NEXT probe
    double              terrainAlt_m;   ///< terrain altitude in meters
    
    // bearing/dist from viewpoint to a/c
    vectorTy            vecView;        // degrees/meters
    
#ifdef DEBUG
    bool                bIsSelected = false;    // is selected for logging/debugging?
#endif
    bool                bChangeModel = false;   ///< shall the model be updated at next chance?
    bool                bSendNewInfoData = false; ///< is there new static data to announce?
    // visibility
    bool                bSetVisible = true;     // manually set visible?
    bool                bAutoVisible = true;    // visibility handled automatically?
    
    /// Nearest airport
    std::string nearestAirport;
    positionTy  nearestAirportPos;
    float       lastNearestAirportCheck = 0.0f;

public:
    LTAircraft(LTFlightData& fd);
    ~LTAircraft() override;
    
    // key for maps
    inline const std::string& key() const { return fd.key().key; }
    void LabelUpdate();
    /// @brief Return a value for dataRef .../tcas/target/flight_id
    /// @returns "Any Id"
    std::string GetFlightId() const override;
    // stringify e.g. for debugging info purposes
    operator std::string() const;
    // current position
    inline const positionTy& GetPPos() const { return ppos; }
    inline positionTy GetPPosLocal() const { return positionTy(ppos).WorldToLocal(); }
    /// @brief position heading to (usually posList[1], ppos if ppos > posList[1])
    /// @param[out] pHeading Receives heading towards to-position
    const positionTy& GetToPos (double* pHeading = nullptr) const;
    // have no more viable positions left, in need of more?
    bool OutOfPositions() const;
    /// periodically find the nearest airport and return a nice position string relative to it
    std::string RelativePositionText ();
    /// nearest airport
    const std::string& GetNearestAirport () const { return nearestAirport; }
    // current a/c configuration
    inline flightPhaseE GetFlightPhase() const { return phase; }
    std::string GetFlightPhaseString() const { return FlightPhase2String(phase); }
    std::string GetFlightPhaseRwyString() const;        ///< GetFlightPhaseString() plus rwy id in case of approach
    inline bool IsOnGrnd() const { return bOnGrnd; }
    bool IsOnRwy() const;               ///< is the aircraft on a rwy (on ground and at least on pos on rwy)
    inline double GetHeading() const { return ppos.heading() + corrAngle.is(); }
    inline double GetTrack() const { return vec.angle; }
    inline double GetFlapsPos() const { return flaps.is(); }
    inline double GetGearPos() const { return gear.is(); }
    inline double GetReverserPos() const { return reversers.is(); }
    inline double GetSpeed_kt() const { return speed.kt(); }                     // kt
    inline double GetSpeed_m_s() const { return speed.m_s(); }   // m/s
    inline double GetVSI_ft() const { return vsi; }                         // ft/m
    inline double GetVSI_m_s() const { return vsi * Ms_per_FTm; }           // m/s
    inline double GetPitch() const { return ppos.pitch(); }
    inline double GetRoll() const { return ppos.roll(); }
    float GetLift() const override;     ///< Lift produced for wake system, typically mass * 9.81, but blends in during rotate and blends out while landing
    inline double GetAlt_ft() const { return ppos.alt_ft(); }
    inline double GetAlt_m() const { return ppos.alt_m(); }
    inline double GetTerrainAlt_ft() const { return terrainAlt_m / M_per_FT; }  ///< terrain alt converted to ft
    inline double GetTerrainAlt_m() const { return terrainAlt_m; }              ///< terrain alt in meter
    inline double GetPHeight_m() const { return ppos.alt_m() - terrainAlt_m; }  ///< height above ground in meter
    inline double GetPHeight_ft() const { return GetPHeight_m() / M_per_FT; }   ///< height above ground converted to ft
    inline vectorTy GetVec() const { return vec; }
    inline vectorTy GetVecView() const { return vecView; }
    std::string GetLightsStr() const;
    void CopyBulkData (LTAPIAircraft::LTAPIBulkData* pOut, size_t size) const;       ///< copies a/c info into bulk structure
    void CopyBulkData (LTAPIAircraft::LTAPIBulkInfoTexts* pOut, size_t size) const;  ///< copies a/c text info into bulk structure
    bool ShallUpdateModel () const { return bChangeModel; }
    void SetUpdateModel () { bChangeModel = true; }
    inline bool ShallSendNewInfoData () const { return bSendNewInfoData; }
    inline void SetSendNewInfoData () { bSendNewInfoData = true; }
    // Visibility
    inline bool IsAutoVisible() const { return bAutoVisible; }
    void SetVisible (bool _bVisible) override;  // define visibility, overrides auto
    bool SetAutoVisible (bool b);       // returns bVisible after auto setting
    // external camera view
    void ToggleCameraView();             // start an external view on this a/c
    void CalcCameraViewPos();
    inline bool IsInCameraView() const { return pExtViewAc == this; }
    static bool IsCameraViewOn() { return pExtViewAc != NULL; }
    static void SetCameraAcExternally (LTAircraft* pCamAc) { pExtViewAc = pCamAc; }

protected:
    void CalcLabelInternal (const LTFlightData::FDStaticData& statDat);
    // based on current sim time and posList calculate the present position
    bool CalcPPos ();
    // determine other parameters like gear, flap, roll etc. based on flight model assumptions
    void CalcFlightModel (const positionTy& from, const positionTy& to);
    /// determine roll, based on a previous and a current heading
    void CalcRoll (double _prevHeading);
    /// determine correction angle
    void CalcCorrAngle ();
    /// determines terrain altitude via XPLM's Y Probe
    bool YProbe ();
    // determines if now visible
    bool CalcVisible ();
    /// Determines AI priority based on bearing to user's plane and ground status
    void CalcAIPrio ();
    
    /// @brief change the model (e.g. when model-defining static data changed)
    /// @note Should be used in main thread only
    void ChangeModel ();

protected:
    // *** Camera view ***
    static LTAircraft*  pExtViewAc;             // the a/c to show in external view, NULL if none/stop ext view
    static positionTy   posExt;                 // external camera position
    static XPViewTypes  prevView;               // View before activating camera
    static XPLMCameraPosition_t extOffs;        // Camera offset from initial tail position

    // callback for external camera view
    static int CameraCB (XPLMCameraPosition_t* outCameraPosition,    /* Can be NULL */
                         int                   inIsLosingControl,
                         void *                inRefcon);

    // command handling during camera view for camera movement
    static void CameraRegisterCommands(bool bRegister);
    static int CameraCommandsCB(
        XPLMCommandRef      inCommand,
        XPLMCommandPhase    inPhase,
        void *              inRefcon);

protected:
    /// XPMP Aircraft Updates
    void UpdatePosition (float, int cycle) override;
};

#endif /* LTAircraft_h */
