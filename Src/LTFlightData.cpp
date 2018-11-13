//
//  LTFlightData.cpp
//  LiveTraffic

/*
 * Copyright (c) 2018, Birger Hoppe
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
 */

#include "LiveTraffic.h"

//
//MARK: Global
//

// the global map of all received flight data,
// which also includes pointer to the simulated aircrafts
mapLTFlightDataTy mapFd;
// modifying the map is controlled by a mutex
// (note that mapFdMutex must be locked before dataAccessMutex
//  to avoid deadlocks, mapFdMutex is considered a higher-level lock)
std::mutex      mapFdMutex;

//
//MARK: Flight Data Subclasses
//
LTFlightData::FDDynamicData::FDDynamicData () :
gnd(false), inHg(0.0),                  // positional
brng(0.0), dst(0.0),                    // relative position
spd(0.0), vsi(0.0),                     // movement
ts(0),
pChannel(nullptr)
{
    // radar
    memset(&radar, 0, sizeof(radar));
    radar.size = sizeof(radar);
    radar.mode = xpmpTransponderMode_ModeC;
}

LTFlightData::FDStaticData::FDStaticData () :
trt(trt_Unknown),                       // communication
year(0), mil(false)                     // aircraft details
{}

LTFlightData::FDStaticData& LTFlightData::FDStaticData::operator |= (const FDStaticData& other)
{
    // copy filled, and only filled data over current data
    // do it field-by-field only for fields which are actually filled
    
    // a/c details
    if (!other.reg.empty()) reg = other.reg;
    if (!other.country.empty()) country = other.country;
    if (!other.acTypeIcao.empty()) acTypeIcao = other.acTypeIcao;
    if (!other.man.empty()) man = other.man;
    if (!other.mdl.empty()) mdl = other.mdl;
    if (other.year) year = other.year;
    if (other.mil) mil = other.mil;     // this only overwrite if 'true'...
    if (other.trt) trt = other.trt;
    
    // find DOC8643
    pDoc8643 = &(Doc8643::get(acTypeIcao));
    
    // flight
    if (!other.call.empty()) call = other.call;
    if (!other.originAp.empty()) originAp = other.originAp;
    if (!other.destAp.empty()) destAp = other.destAp;
    if (!other.flight.empty()) flight = other.flight;
    
    // operator / Airline
    if (!other.op.empty()) op = other.op;
    if (!other.opIcao.empty()) opIcao = other.opIcao;

    return *this;
}

// route (this is "originAp - destAp", but considers emoty txt)
std::string LTFlightData::FDStaticData::route () const
{
    // keep it an empty string if there is no info at all
    if (originAp.empty() && destAp.empty())
        return std::string();
    
    // if there is some info then replace missing info with question mark
    std::string s(originAp.empty() ? "?" : originAp);
    s += '-';
    s += destAp.empty() ? "?" : destAp;
    return s;
}

std::string LTFlightData::FDStaticData::flightRoute() const
{
    const std::string r(route());
    // keep it an empty string if there is no info at all
    if (flight.empty() && r.empty())
        return std::string();
    
    // if there is some info missing then just return the other
    if (flight.empty())
        return r;
    if (r.empty())
        return flight;
    
    // we have both...put it together
    return (flight + ": ") + r;
}

//
//MARK: Flight Data
//

// Constructor
LTFlightData::LTFlightData () :
transpIcaoInt(0),
rcvr(0),sig(0),
rotateTS(NAN),
youngestTS(0),
pAc(nullptr), probeRef(NULL),
bValid(true)
{}

// Copy Constructor (needed for emplace into map) doesn't copy mutex
LTFlightData::LTFlightData(const LTFlightData& fd)
{
    // all logic is in the copy assignment operator
    *this = fd;
}

// Destructor makes sure lock is available and aircraft is removed, too
LTFlightData::~LTFlightData()
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // make sure aircraft is removed, too
        DestroyAircraft();
        // Release probe handle
        if (probeRef)
            XPLMDestroyProbe(probeRef);
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// Copy assignment operator copies all but the mutex
LTFlightData& LTFlightData::operator=(const LTFlightData& fd)
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // copy data
        transpIcao          = fd.transpIcao;        // key
        transpIcaoInt       = fd.transpIcaoInt;
        rcvr                = fd.rcvr;
        sig                 = fd.sig;
        posDeque            = fd.posDeque;          // dynamic data
        dynDataDeque        = fd.dynDataDeque;
        rotateTS            = fd.rotateTS;
        youngestTS          = fd.youngestTS;
        statData            = fd.statData;          // static data
        pAc                 = fd.pAc;
        probeRef            = fd.probeRef;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return *this;
}

// setting the key is possible only once
void LTFlightData::SetKey( std::string key )
{
    if ( transpIcao.empty() ) {
        transpIcao = key;
        
        // convert the hex string to an unsigned int
        transpIcaoInt = (unsigned)strtoul ( key.c_str(), NULL, 16 );        
    }
}

// Search support: icao, registration, call sign, flight number matches?
bool LTFlightData::IsMatch (const std::string t) const
{
    // we can compare icao without lock
    if (transpIcao == t)
        return true;
    
    // everything else must be guarded
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // compare with registration, flight number
        if (statData.flight == t || statData.reg == t)
            return true;
        
        // finally compare with call sign
        if (statData.call == t)
            return true;
        
        // no match
        return false;
        
        // copy data
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return false;
}

bool LTFlightData::validForAcCreate(double simTime) const
{
    // the obvious stuff first: we need basic data
    if ( empty() || dynDataDeque.empty() || posDeque.size() < 2 )
        return false;

    // simTime defaults to 'now'
    if (isnan(simTime))
        simTime = dataRefs.GetSimTime();

    // so we have two positions...one in the past, one in the future?
    return
    posDeque[0].ts() <= simTime && simTime < posDeque[1].ts();
}

// is the data outdated (considered too old to be useful)?
bool LTFlightData::outdated (double simTime) const
{
    // general re-init necessary?
    if (dataRefs.IsReInitAll())
        return true;
    
    // a/c turned invalid (due to exceptions)?
    if (pAc && !pAc->IsValid())
        return true;
    
    // flown out of sight? -> outdated, remove
    if (pAc &&
        pAc->GetVecView().dist > dataRefs.GetFdStdDistance_m())
        return true;
    
    // cover the special case of finishing landing and roll-out without live positions
    // i.e. during approach and landing we don't destroy the aircraft
    //      until it finally stopped on the runway
    if (pAc &&
        pAc->GetFlightPhase() >= LTAircraft::FPH_APPROACH &&
        pAc->GetFlightPhase() <  LTAircraft::FPH_STOPPED_ON_RWY)
    {
        return false;
    }
    
    // invalid and
    // youngestTS longer ago than allowed?
    return posDeque.empty() &&
    youngestTS + dataRefs.GetAcOutdatedIntvl() < (isnan(simTime) ? dataRefs.GetSimTime() : simTime);
}

#define ADD_LABEL(b,txt) if (b && !txt.empty()) { labelStat += txt; labelStat += ' '; }
// update static data parts of the a/c label for reuse for performance reasons
void LTFlightData::UpdateStaticLabel()
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // the configuration: which parts to include in the label?
        const DataRefs::LabelCfgUTy cfg = dataRefs.GetLabelCfg();
        
        // add parts as per config
        labelStat.clear();
        ADD_LABEL(cfg.b.bIcaoType,    statData.acTypeIcao);
        ADD_LABEL(cfg.b.bTranspCode,  key());
        ADD_LABEL(cfg.b.bReg,         statData.reg);
        ADD_LABEL(cfg.b.bIcaoOp,      statData.opIcao);
        ADD_LABEL(cfg.b.bCallSign,    statData.call);
        ADD_LABEL(cfg.b.bFlightNo,    statData.flight);
        ADD_LABEL(cfg.b.bRoute,       statData.route());
        
        // this is the config we did the label for
        labelCfg = cfg;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// produce a/c label
#define ADD_LABEL_NUM(b,num) if (b) { label += std::to_string(long(num)); label += ' '; }
std::string LTFlightData::ComposeLabel() const
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

        // the configuration: which parts to include in the label?
        const DataRefs::LabelCfgTy cfg = dataRefs.GetLabelCfg().b;
        std::string label(labelStat);       // copy static parts
        
        // only possible if we have an aircraft
        if (pAc) {
            // current position of a/c
            const positionTy& pos = pAc->GetPPos();
            // add more items as per configuration
            if (cfg.bPhase) { label +=  pAc->GetFlightPhaseString(); label += ' '; }
            ADD_LABEL_NUM(cfg.bHeading,     pos.heading());
            ADD_LABEL_NUM(cfg.bAlt,         pos.alt_ft());
            ADD_LABEL_NUM(cfg.bHeightAGL,   pAc->GetPHeight_ft());
            ADD_LABEL_NUM(cfg.bSpeed,       pAc->GetSpeed_kt());
            ADD_LABEL_NUM(cfg.bVSI,         pAc->GetVSI_ft());
        }
        
        // remove the trailing space
        label.pop_back();
        
        return label;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return "?";
}


//
//MARK: Flight Data - Mutex-Controlled access Dynamic
//



// based on buffered positions calculate the next position to fly to
// (usually called in a separate thread via TriggerCalcNewPos,
//  with 'simTime' slightly [~0.5s] into the future,
//  called by LTAircraft shortly before running out of positions and
//  calling TryFetchNewPos)
//
// simTime should only be set when called from LTAircraft,
// others should pass in NAN. Then no landing/take off detection takes place,
// which relies on the actual aircraft being in take of roll / final.
bool LTFlightData::CalcNextPos ( double simTime )
{
    bool bDoLandTODetect = true;
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // if our buffer of positions is completely empty we can't do much
        if ( posDeque.empty() || dynDataDeque.empty() )
            return false;
        
        // *** maintenance of flight data deque ***
        
        // if no simTime given use current
        if (isnan(simTime)) {
            simTime = dataRefs.GetSimTime();
            bDoLandTODetect = false;
        }

        // remove from front until [0] <= simTime < [1] (or just one element left)
        while (dynDataDeque.size() >= 2 && dynDataDeque[1].ts <= simTime)
            dynDataDeque.pop_front();
        
        // *** maintenance of buffered positions ***
        
        // Differs depending on: is there an a/c yet?
        const long sizeBefore = posDeque.size();
        if ( pAc ) {
            // if there is an a/c then we just remove all positions before 'simTime'
            while (!posDeque.empty() && posDeque[0].ts() <= simTime)
                posDeque.pop_front();
            
            // no positions left?
            if (posDeque.empty()) {
                if (dataRefs.GetDebugAcPos(key()))
                    LOG_MSG(logDEBUG,DBG_NO_MORE_POS_DATA,Positions2String().c_str());
                return false;
            }
        } else {
            // If there is no a/c yet then we need one past and
            // one or more future positions
            // If already the first pos is in the future then we aren't valid yet
            if (simTime < posDeque[0].ts())
                return false;
            
            // The first pos is in the past, good, make sure it's the only one
            // [0] <= simTime < [1]
            while (posDeque.size() >= 2 && posDeque[1].ts() <= simTime)
                posDeque.pop_front();
            
            // Unlikely, but theoretically there could now be just one (past) pos left
            if (posDeque.size() < 2)
                return false;
        }

#ifdef DEBUG
        std::string deb0 ( posDeque[0].dbgTxt() );
        std::string deb1 ( posDeque.size() >= 2 ? std::string(posDeque[1].dbgTxt()) : "<none>" );
        std::string vec  ( posDeque.size() >= 2 ? std::string(posDeque[0].between(posDeque[1])) : "<none>" );
#endif
        
        // *** Landing / Take-Off Detection ***
        
        if ( pAc && bDoLandTODetect ) {
            const positionTy& ppos = pAc->GetPPos();
            const LTAircraft::FlightModel& mdl = pAc->mdl;
            positionTy& to   = posDeque[0];
            vectorTy vec (ppos.between(to));
            
            // *** Landing ***
            // If current pos is in the air and next pos is approaching or touching ground
            // then we have live positional data on the ground.
            // However, if we would fly directly to next pos then we would touch down
            // then at next pos only (earliest), which might be a point far down the runway.
            // To simulate touching down at the _beginning_ of the runway and
            // then rolling out to (or through) next pos we determine this case
            // and then insert an artifical touch down position, which just keeps going with
            // previous vsi and speed down to the ground.

            if (ppos.onGrnd == positionTy::GND_OFF &&           // currently off ground
                to.onGrnd   >= positionTy::GND_APPROACHING &&   // future: approaching / on ground
                pAc->GetVSI_ft() < -mdl.VSI_STABLE) {           // descending considerably
                // Case determined: We are landing and have live positional
                //                  data down the runway
                const double timeToTouchDown = -(pAc->GetPHeight_m() / pAc->GetVSI_m_s());
                // but only reasonably a _new_ position if a few seconds before [1]
                if (timeToTouchDown > SIMILAR_TS_INTVL &&
                    ppos.ts() + timeToTouchDown + SIMILAR_TS_INTVL < to.ts()) {
                    vectorTy vec(ppos.heading(),                            // angle
                                 timeToTouchDown * pAc->GetSpeed_m_s(),     // distance
                                 pAc->GetVSI_m_s(),                         // vsi
                                 pAc->GetSpeed_m_s());                      // speed
                    // insert touch-down point at beginning of posDeque
                    positionTy& touchDownPos = posDeque.emplace_front(ppos.destPos(vec));
                    touchDownPos.onGrnd = positionTy::GND_ON;
                    TryDeriveGrndStatus(touchDownPos);          // will set correct terrain altitude
                    // then, however, next pos should also be on the ground, no longer just approaching
                    to.onGrnd = positionTy::GND_ON;
                    // output debug info on request
                    if (dataRefs.GetDebugAcPos(key())) {
                        LOG_MSG(logDEBUG,DBG_INVENTED_TD_POS,touchDownPos.dbgTxt().c_str());
                    }
                }
            } // (landing case)
            
            // *** Take Off ***
            
            // Similar issue as with landing, just reverse:
            // A/c is to take off (rotate and leave the runway) somewhere
            // between an on-ground position and an off-ground position.
            // We need to stick to the ground till that point. Where is that point?
            // If we'd just follow the known flight data then we likely
            // 'climb' with 100-200 ft/min from the start of the runway (A)
            // slowly to some point down the runway and only slightly in the air (B)
            // and then only change to initial climbing rate to a point
            // higher up in the air (C).
            // What we want is to stick to the ground, accelerate and leave
            // shortly before (B) at the right angle to extend climbing right
            // away to (C). (B)-(C) is a path really flown while (A)-(B)
            // is not 'flown' directly, but first there's the take-off-roll
            // followed by rotating and taking off.
            // So we extend the vector (B)-(C) _backwards_ to find the
            // take-off-point, achieved using vsi of (B)-(C).
            // Point of rotate is 3s earlier.
            
            else if ((ppos.onGrnd == positionTy::GND_ON || ppos.onGrnd == positionTy::GND_LEAVING) &&
                     (to.onGrnd == positionTy::GND_LEAVING || to.onGrnd == positionTy::GND_OFF))
            {
                // VSI needs to be high enough for actual take off
                if (vec.vsi_ft() > mdl.VSI_STABLE)
                {
                    
                    // Get the vsi and speed after 'to' (the (B)-(C) vector's)
                    double climbVsi = mdl.VSI_INIT_CLIMB * Ms_per_FTm;
                    double climbSpeed = mdl.SPEED_INIT_CLIMB / KT_per_M_per_S;
                    if (posDeque.size() >= 2) {     // take the data from the vector _after_ to
                        vectorTy climbVec (to.between(posDeque[1]));
                        climbVsi = climbVec.vsi;
                        climbSpeed = climbVec.speed;
                    }

                    // Determine how much before 'to' is that take-off point
                    // We want an accurate terrain altitude for this calc
                    const double toTerrAlt = YProbe_at_m(to);
                    const double height_m = to.alt_m() - toTerrAlt; // height to climb to reach 'to'?
                    const double toClimb_s = height_m / climbVsi;   // how long to climb to reach 'to'?
                    const double takeOffTS = to.ts() - toClimb_s;   // timestamp at which to start the climb, i.e. take off
                    
                    // Continue only for useful timestamps
                    if (ppos.ts() < takeOffTS && takeOffTS < to.ts())
                    {
                        vectorTy vecTO(fmod(vec.angle + 180, 360),  // angle (reverse!)
                                       climbSpeed * toClimb_s,      // distance
                                       -climbVsi,                   // vsi (reverse!)
                                       climbSpeed);                 // speed
                        // insert take-off point ('to' minus vector from take-off to 'to')
                        // at beginning of posDeque
                        positionTy& takeOffPos = posDeque.emplace_front(to.destPos(vecTO));
                        takeOffPos.onGrnd = positionTy::GND_ON;
                        takeOffPos.alt_m() = toTerrAlt;
                        takeOffPos.heading() = vec.angle;           // from 'reverse' back to forward
                        takeOffPos.ts() = takeOffTS;                // ts was computed forward...we need it backward
                        // then, however, 'to' pos should be off ground, no longer just leaving
                        to.onGrnd = positionTy::GND_OFF;
                        // and we store the timestamp when to rotate
                        rotateTS = takeOffTS - mdl.ROTATE_TIME;
                        // output debug info on request
                        if (dataRefs.GetDebugAcPos(key())) {
                            LOG_MSG(logDEBUG,DBG_INVENTED_TO_POS,takeOffPos.dbgTxt().c_str());
                        }

                    }
                }
                // * Don't yet take off *
                // else VSI is not enough for actual take off -> stick to ground!
                else {
                    to.onGrnd = positionTy::GND_ON;
                    TryDeriveGrndStatus(to);        // set alt to terrain
                }
            } // (take off case)
        } // (has a/c and do landing / take-off detection)
        
        // if something changed output all positional information as debug info on request
        if (sizeBefore != posDeque.size() && dataRefs.GetDebugAcPos(key())) {
            LOG_MSG(logDEBUG,DBG_POS_DATA,Positions2String().c_str());
        }
        
        // posDeque should still be sorted, i.e. no two adjacent positions a,b should be a > b
        LOG_ASSERT_FD(*this,
                      std::adjacent_find(posDeque.cbegin(), posDeque.cend(),
                                         [](const positionTy& a, const positionTy& b)
                                         {return a > b;}
                                         ) == posDeque.cend());
        
        // success
        return true;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    return false;
}

// the mutex used to synch access to the list of keys which await pos calculation
std::mutex calcNextPosListMutex;
// and that list of pairs <key,simTime>
typedef std::deque<std::pair<std::string,double>> dequeStrDoubleTy;
dequeStrDoubleTy dequeKeyPosCalc;

// The main function for the position calculation thread
// It receives keys to work on in the dequeKeyPosCalc list and calls
// the CalcNextPos function on the respective flight data objects
void LTFlightData::CalcNextPosMain ()
{
    // loop till said to stop
    while ( !bFDMainStop ) {
        std::pair<std::string,double> pair (std::string(),0);
        
        // thread-safely access the list of keys to fetch one for processing
        try {
            std::lock_guard<std::mutex> lock (calcNextPosListMutex);
            if ( !dequeKeyPosCalc.empty() ) {   // something's in the list, take it
                pair = dequeKeyPosCalc.front();
                dequeKeyPosCalc.pop_front();
            }
        } catch(const std::system_error& e) {
            LOG_MSG(logERR, ERR_LOCK_ERROR, "CalcNextPosMain", e.what());
            pair = std::pair<std::string,double> (std::string(),0);
        }
        
        // there was something in the list to process? Do so!
        if (!pair.first.empty()) {
            try {
                // find the flight data object in the map and calc position
                LTFlightData& fd = mapFd.at(pair.first);
                
                // LiveTraffic Top Level Exception Handling:
                // CalcNextPos can cause exceptions. If so make fd object invalid and ignore it
                try {
                    if (fd.IsValid())
                        fd.CalcNextPos(pair.second);
                } catch (const std::exception& e) {
                    LOG_MSG(logERR, ERR_TOP_LEVEL_EXCEPTION, e.what());
                    fd.bValid = false;
                } catch (...) {
                    fd.bValid = false;
                }
                
            } catch(const std::out_of_range&) {
                // just ignore exception...fd object might have gone in the meantime
            }
        }
            
        // sleep till woken up for processing or stopping
        {
            std::unique_lock<std::mutex> lk(FDThreadSynchMutex);
            FDThreadSynchCV.wait(lk, []{return bFDMainStop || !dequeKeyPosCalc.empty();});
            lk.unlock();
        }
    }
}

// Add a new key to the list of positions to calculate
// and wake up the calculation thread
void LTFlightData::TriggerCalcNewPos ( double simTime )
{
    // thread-safely add the key to the list and start the calc thread
    try {
        std::lock_guard<std::mutex> lock (calcNextPosListMutex);
        
        // search for key in the list, if already included update simTime and return
        for (dequeStrDoubleTy::value_type &i: dequeKeyPosCalc)
            if(i.first==key()) {
                i.second = fmax(simTime,i.second);   // update simTime to latest
                return;
            }
        
        // not in list, so add to list of keys to calculate including simTime
        dequeKeyPosCalc.emplace_back(std::pair(key(),simTime));
        
        // trigger the calc thread to wake up
        FDThreadSynchCV.notify_all();
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, "TriggerCalcNewPos", e.what());
    }
}


// calc heading from positions in a positionList around a given position (it)
// if there is only (it), then use heading from flight data
// if there are 2 positions then return heading from first to second
// if there are 3 positions, then calc both headings (from 1st to 2nd, from 2nd to 3rd)
//                           and return its average
//         ====>        <- this is the return value if 3 positions exist
//         P_it                p1   -> p_it =  30°
//          ^ \                p_it -> p3   = 150°
//         /   _|              return       = (30° + 150°) / 2 = 90°
//        /     P3
//       P1
//
// (Thinking behind it: By the time we reach P2 we should be flying a curve
//  and should be at heading as returned -- before changing course to P3)
//
// Also consider the distance between points: a distance less than 5m is
// considered "no point" for the purpose of this calculation.
// Such short distances will be seen when a/c stops on the gound:
// Very similar positions next to each other, which we don't want the entire
// a/c to turn to. Instead, favor the longer vector, or, if none of the
// vectors is long enough, fall back to the heading as reported in the
// flight data.
void LTFlightData::CalcHeading (dequePositionTy::iterator it)
{
    // vectors to / from the position at "it"
    vectorTy vecTo, vecFrom;
    
    // is there a predecessor to "it"?
    if (it != posDeque.cbegin()) {
        vecTo = std::prev(it)->between(*it);
        if (vecTo.dist < SIMILAR_POS_DIST)      // clear the vector if too short
            vecTo = vectorTy();
    }
    
    // is there a successor to it?
    if (std::next(it) != posDeque.cend()) {
        vecFrom = it->between(*std::next(it));
        if (vecFrom.dist < SIMILAR_POS_DIST)    // clear the vector if too short
            vecFrom = vectorTy();
    }
    
    // if both vectors are available return the average between both angles
    if (!isnan(vecTo.angle) && !isnan(vecFrom.angle))
        // with the linear factor in favor of the _shorter_ vector
        // (Idea: we have more time to turn on the longer side, so at the junction
        //        the heading should be closer to the shorter vector's heading)
        it->heading() = AvgHeading(vecTo.angle,
                                   vecFrom.angle,
                                   vecFrom.dist,
                                   vecTo.dist);
    // if just one vector is available take that one
    else if (!isnan(vecFrom.angle))
        it->heading() = vecFrom.angle;
    else if (!isnan(vecTo.angle))
        it->heading() = vecTo.angle;
    // if no vector is available
    else {
        // then we fall back to the heading delivered by the flight data
        FDDynamicData *pBefore = nullptr, *pAfter = nullptr;
        dequeFDDynFindAdjacentTS(it->ts(), pBefore, pAfter);
        // get the best heading out of it
        if (pAfter && pBefore)
            it->heading() = AvgHeading(pBefore->heading,
                                       pAfter->heading,
                                       pAfter->ts - it->ts(),   // factor "before" higher if close (and after is further away!)
                                       it->ts() - pBefore->ts); // factor "after" higher if close (and before is further away!)
        else if (pBefore)
            it->heading() = pBefore->heading;
        else if (pAfter)
            it->heading() = pAfter->heading;
        else
            it->heading() = 0;
    }
}


void LTFlightData::AddNewPos ( positionTy& pos )
{
    // should be an ok position
    LOG_ASSERT_FD(*this,pos.isNormal());
    
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);

        // if there is an a/c then we shall no longer add positions
        // before the current 'to' position of the a/c
        if (pAc && pos <= pAc->GetToPos()) {
            // pos is before or close to 'to'-position: don't add!
            if (dataRefs.GetDebugAcPos(key()))
                LOG_MSG(logDEBUG,DBG_SKIP_NEW_POS,pos.dbgTxt().c_str());
            return;
        }
        
        // *** ground status *** (plays a role in merge determination)
        TryDeriveGrndStatus(pos);
        
        // *** insert/merge position ***
        
        // based on timestamp find possible "similar" position
        // or insert position.
        // and if so merge with that position to avoid too many position in
        // a very short time frame, as that leads to zick-zack courses in a
        // matter of meters only as can happen when merging different data streams
        bool bJustMerged = false;
        dequePositionTy::iterator i =   // first: find a merge partner
        std::find_if(posDeque.begin(), posDeque.end(),
                     [&pos](const positionTy& p){return p.canBeMergedWith(pos);});
        if (i != posDeque.end()) {      // found merge partner!
            // make sure we don't overlap we predecessor/successor position
            if (((i == posDeque.begin()) || (*std::prev(i) < pos)) &&
                ((std::next(i) == posDeque.end()) || (*std::next(i) > pos)))
            {
                *i |= pos;                  // merge them
                bJustMerged = true;
                if (dataRefs.GetDebugAcPos(key()))
                    LOG_MSG(logDEBUG,DBG_MERGED_POS,pos.dbgTxt().c_str(),i->ts());
            }
            else
            {
                // pos would overlap with surrounding positions
                if (dataRefs.GetDebugAcPos(key()))
                    LOG_MSG(logDEBUG,DBG_SKIP_NEW_POS,pos.dbgTxt().c_str());
                return;
            }
        }
        else
        {
            // second: find insert-before position
            i = std::find_if(posDeque.begin(), posDeque.end(),
                             [&pos](const positionTy& p){return p > pos;});
            if (i == posDeque.end()) {      // new pos is to be added at end
                posDeque.emplace_back(pos);
                i = std::prev(posDeque.end());
            }
            else {                          // found real insert position: before i
                i = posDeque.emplace(i, pos);
            }
        }
        
        // i now points to the inserted/merged element
        positionTy& p = *i;
        
        // *** heading ***
        
        // Recalc heading of adjacent positions: before p, p itself, and after p
        if (i != posDeque.begin())              // is there anything before i?
            CalcHeading(std::prev(i));
        
        CalcHeading(i);                         // i itself
        
        if (std::next(i) != posDeque.end())     // is there anything after i?
            CalcHeading(std::next(i));
        
        // *** pitch ***
        // just a rough value, LTAircraft::CalcPPos takes care of the details
        if (p.onGrnd)
            p.pitch() = 0;
        else
            p.pitch() = 2;
        
        // *** roll ***
        // TODO: Calc roll
        p.roll() = 0;
        
        // *** last checks ***
        
        // should be fully valid position now
        LOG_ASSERT_FD(*this, p.isFullyValid());
        
        // posDeque should be sorted, i.e. no two adjacent positions a,b should be a > b
        LOG_ASSERT_FD(*this,
                      std::adjacent_find(posDeque.cbegin(), posDeque.cend(),
                                         [](const positionTy& a, const positionTy& b)
                                         {return a > b;}
                                         ) == posDeque.cend());
        
        // now the youngest timestamp is this one of the last known position:
        youngestTS = std::prev(posDeque.cend())->ts();
        
        // *** trigger recalc ***
        TriggerCalcNewPos(NAN);
        
        // print all positional information as debug info on request if not just merged
        if (!bJustMerged && dataRefs.GetDebugAcPos(key())) {
            LOG_MSG(logDEBUG,DBG_POS_DATA,Positions2String().c_str());
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// Called by a/c: reads available positions if lock available
LTFlightData::tryResult LTFlightData::TryFetchNewPos (dequePositionTy& acPosList,
                                                      double& _rotateTS)
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( !lock )                            // didn't get the lock -> return
            return TRY_NO_LOCK;
        
        // the very first call (i.e. FD doesn't even know the a/c's ptr yet)?
        if (!pAc) {
            // there must be two positions, one in the past, one in the future!
            LOG_ASSERT_FD(*this, validForAcCreate());
            // copy the first two positions, so that the a/c can start flying from/to
            acPosList.emplace_back(posDeque[0]);
            acPosList.emplace_back(posDeque[1]);
        } else {
            // there is an a/c...only copy stuff past current 'to'-pos
            const positionTy& to = pAc->GetToPos();
            LOG_ASSERT_FD(*this, !isnan(to.ts()));
            
            // find the first position beyond current 'to' (is usually right away the first one!)
            dequePositionTy::const_iterator i =
            std::find_if(posDeque.cbegin(), posDeque.cend(),
                         [&to](const positionTy& p){return to << p;});
            
            // nothing???
            if (i == posDeque.cend())
                return TRY_NO_DATA;
            
            // add that next position to the a/c
            acPosList.emplace_back(*i);
        }
        
        // store rotate timestamp if there is one (never overwrite with NAN!)
        if (!isnan(rotateTS))
            _rotateTS = rotateTS;
        
        // output all positional information as debug info on request
        if (dataRefs.GetDebugAcPos(key()))
            LOG_MSG(logDEBUG,DBG_POS_DATA,Positions2String().c_str());
        
        // return success as something has been added
        return TRY_SUCCESS;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Caught some error
    return TRY_TECH_ERROR;
}


// determine ground-status based on comparing altitude to terrain
// Note: If pos.onGnd == GND_ON then this will not change, but the altitude will be set to terrain altitude
//       If pos.onGnd != GND_ON then onGnd will be decided based on comparing altitude to terrain altitude
bool LTFlightData::TryDeriveGrndStatus (positionTy& pos)
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( lock )
        {
            // what's the terrain altitude at that pos?
            double terrainAlt = YProbe_at_m(pos);
            if (isnan(terrainAlt))
                return false;
            
            // Now 2 options:
            // If position already says itself: I'm on the ground, then keep it like that
            // Otherwise decide based on altitude _if_ it's on the ground
            
            if (pos.onGrnd != positionTy::GND_ON &&
                // say it's on the ground if below terrain+10ft
                pos.alt_m() < terrainAlt + FD_GND_AGL)
                pos.onGrnd = positionTy::GND_ON;

            // if it was or now is on the ground correct the altitue to exact terrain altitude
            if (pos.onGrnd == positionTy::GND_ON)
                pos.alt_m() = terrainAlt;
            else
                // make sure it's either GND_ON or GND_OFF, nothing lese
                pos.onGrnd = positionTy::GND_OFF;

            // successfully determined a status
            return true;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return false;
}

// determine terrain alt at pos
double LTFlightData::YProbe_at_m (const positionTy& pos)
{
    return ::YProbe_at_m(pos, probeRef);
}

// returns vector at timestamp (which has speed, direction and the like)
LTFlightData::tryResult LTFlightData::TryGetVec (double ts, vectorTy& vec) const
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( lock )
        {
            // find positions around timestamp
            dequePositionTy::const_iterator i =
            std::adjacent_find(posDeque.cbegin(),posDeque.cend(),
                               [ts](const positionTy& a, const positionTy& b)
                               {return a.ts() <= ts && ts <= b.ts();});
            
            // no pair of positions found -> can't compute vector
            if (i == posDeque.cend())
                return TRY_NO_DATA;
            
            // found a pair, return the vector between them
            vec = i->between(*(std::next(i)));
            return TRY_SUCCESS;
        }
        else
            return TRY_NO_LOCK;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return TRY_TECH_ERROR;
}

// stringify all position information - mainly for debugging purposes
std::string LTFlightData::Positions2String () const
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // 0. current sim time
        time_t t = time_t(dataRefs.GetSimTime());
        struct tm tm = *gmtime(&t);
        
        char szBuf[50];
        snprintf(szBuf,sizeof(szBuf),
                 "a/c %s SimTime: %.1f - ",
                 key().c_str(),
                 dataRefs.GetSimTime());
        std::string ret(szBuf);

        strftime(szBuf,
                 sizeof(szBuf) - 1,
                 "%F %T",
                 &tm);
        ret += szBuf;
        ret += '\n';
        
        // 1. the data actually used by the a/c
        if(pAc) {
            ret += *pAc;
        } else {
            ret += "pAc == <null>\n";
        }
        
        // 2. flight data buffer as read from original source
        ret += "posDeque:\n";
        ret += positionDeque2String(posDeque);
        
        return ret;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // if we get here something's wrong
    return std::string();
}

// add dynamic data (if new one is more up-to-date)
void LTFlightData::AddDynData (const FDDynamicData& inDyn,
                               int _rcvr, int _sig,
                               positionTy* pos)
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // only need to bother adding data if it is newer than current data
        if (dynDataDeque.empty() || dynDataDeque[0] < inDyn)
        {
            // must not yet have similar timestamp in our list
            if (std::find_if(dynDataDeque.cbegin(),dynDataDeque.cend(),
                             [&inDyn](const FDDynamicData& i){return inDyn.similarTo(i);}) == dynDataDeque.cend())
            {
                // add to list and keep sorted
                dynDataDeque.emplace_back(inDyn);
                std::sort(dynDataDeque.begin(),dynDataDeque.end());
            }
            
            // either way: we 'like' this receiver
            rcvr = _rcvr;
            sig = _sig;
        }
            
        // also store the pos (lock is held recursively)
        if (pos)
            AddNewPos(*pos);
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// tries to lock, then copies, returns true if copy took place
bool LTFlightData::TryGetSafeCopy ( FDDynamicData& outDyn ) const
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock );
        if ( lock )
        {
            // we got the lock, return a copy of the data
            outDyn = dynDataDeque.empty() ? FDDynamicData() : dynDataDeque[0];
            // Success!
            return true;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return false;
}

// waits for lock and returns a copy
LTFlightData::FDDynamicData LTFlightData::WaitForSafeCopyDyn (bool bFirst) const
{
    LTFlightData::FDDynamicData ret;
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // copy the the data under lock protection
        if (!dynDataDeque.empty())
            ret = bFirst ? dynDataDeque.front() : dynDataDeque.back();
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    // return the data (ideally the copy created under lock protection)
    return ret;
}

LTFlightData::FDDynamicData LTFlightData::GetUnsafeDyn() const
{
    return dynDataDeque.empty() ? FDDynamicData() : dynDataDeque[0];
}

// find two positions around given timestamp ts
// pBefore and pAfter can come back NULL!
// if pbSimilar is not NULL then function also checks for 'similar' pos
// if a 'similar' pos (ts within SIMILAR_TS_INTVL) is found then
// *pbSimilar is set to true and pBefore points to that one.
// Calling function must own lock to ensure pointers remain valid
void LTFlightData::dequeFDDynFindAdjacentTS (double ts,
                                             LTFlightData::FDDynamicData*& pBefore,
                                             LTFlightData::FDDynamicData*& pAfter,
                                             bool* pbSimilar)
{
    // init
    pBefore = pAfter = nullptr;
    if (pbSimilar)
        *pbSimilar = false;
    
    // loop
    for (FDDynamicData& d: dynDataDeque) {
        
        // test for similarity
        if (pbSimilar) {
            if (abs(d.ts-ts) < SIMILAR_TS_INTVL) {
                *pbSimilar = true;
                pBefore = &d;
                return;
            }
        }
        
        // test for range before/after
        if (d.ts <= ts)
            pBefore = &d;           // while less than timestamp keep pBefore updated
        else {
            pAfter = &d;            // now found (first) data greater then ts
            return;                 // short-cut...ts in dynDataDeque would only further increase
        }
    }
}


//
//MARK: Flight Data - Mutex-Controlled access Static
//

// update static data
void LTFlightData::UpdateData (const LTFlightData::FDStaticData& inStat)
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // merge inStat into our statData (copy only filled fields):
        statData |= inStat;
        
        // update the static parts of the label
        UpdateStaticLabel();
        
   } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
}

// tries to lock, then copies, returns true if copy took place
bool LTFlightData::TryGetSafeCopy ( LTFlightData::FDStaticData& outStat ) const
{
    try {
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock);
        if ( lock )
        {
            // we got the lock, return a copy of the data
            outStat = statData;
            // Success!
            return true;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    
    // Either didn't get the lock or some caught error
    return false;
}

// waits for lock and returns a copy
LTFlightData::FDStaticData LTFlightData::WaitForSafeCopyStat() const
{
    LTFlightData::FDStaticData ret;
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        // copy the data under lock protection
        ret = statData;
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    // return the data (ideally the copy created under lock protection)
    return ret;
}

//
//MARK: Flight Data - Aircraft Maintenance
//      (call from flight loop as possibly XPMP/XPLM calls are invoked)
//

// checks if initial position to be calculated or aircraft to be created
// returns if a/c is to be deleted
bool LTFlightData::AircraftMaintenance ( double simTime )
{
    try {
        // try to lock data access
        std::unique_lock<std::recursive_mutex> lock (dataAccessMutex, std::try_to_lock);
        if ( !lock )                // we didn't get the lock, just return w/o deletion
            return false;
        
        // if outdated just return 'delete me'
        if ( outdated(simTime) )
            return true;
        
        // do we need to recalc the static part of the a/c label due to config change?
        if (dataRefs.GetLabelCfg().i != labelCfg.i)
            UpdateStaticLabel();
        
        // doesn't yet have an associated aircraft but two positions?
        if ( !hasAc() && posDeque.size() >= 2 ) {
            // is already valid for a/c creation?
            if ( validForAcCreate(simTime) )
                // then do create the aircraft
                CreateAircraft(simTime);
            else // not yet valid
                // but the oldest position is at or before current simTime?
                // then chances are good that we can calculate positions
                if ( posDeque[0].ts() <= simTime)
                    // start thread for position calculation...next time we might be valid for creation
                    TriggerCalcNewPos(NAN);
        }
        
        // don't delete me
        return false;
        
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
        // if an exception occurs this object is declared invalid and removed
        bValid = false;
    } catch(...) {
        // if an exception occurs this object is declared invalid and removed
        bValid = false;
    }
    
    // in case of error return 'delete me'
    return true;
}



// create (at most one) aircraft from this flight data
bool LTFlightData::CreateAircraft ( double simTime )
{
    static bool bTooManyAcMsgShown = false;
    
    // short-cut if exists already
    if ( hasAc() ) return true;
    
    // short-cut if too many aircrafts created already
    if ( dataRefs.GetNumAircrafts() >= dataRefs.GetMaxNumAc() ) {
        if ( !bTooManyAcMsgShown )              // show warning once only per session
            SHOW_MSG(logWARN,MSG_TOO_MANY_AC,dataRefs.GetMaxNumAc());
        bTooManyAcMsgShown = true;
        return false;
    }
    
    try {
        // get the  mutex, not so much for protection,
        // but to speed up creation (which read-accesses lots of data and
        // thus makes many calls to the lock, which are now just quick recursive calls)
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // make sure positional data is up-to-date
        // (also does a last validation...and now with lock, so that state is secured)
        if ( !CalcNextPos(simTime) )
            return false;

        // create the object (constructor will recursively re-access the lock)
        pAc = new LTAircraft(*this);
        if (!pAc)
        {
            LOG_MSG(logERR,ERR_NEW_OBJECT,key().c_str());
            return false;
        }
    } catch(const std::system_error& e) {
        LOG_MSG(logERR, ERR_LOCK_ERROR, key().c_str(), e.what());
    }
    // success
    return true;
}

// remove the linked aircraft
void LTFlightData::DestroyAircraft ()
{
    if ( pAc )
        delete pAc;
    pAc = nullptr;
}
