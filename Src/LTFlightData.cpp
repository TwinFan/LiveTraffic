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

// returns flight, call sign, registration, or trans hex code
std::string LTFlightData::FDStaticData::acId (const std::string _default) const
{
    return
    !flight.empty() ?   flight  :
    !call.empty() ?     call    :
    !reg.empty() ?      reg     :
    _default;
}

// route (this is "originAp-destAp", but considers empty txt)
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

std::string LTFlightData::FDStaticData::getMan() const
{
    return pDoc8643 && !pDoc8643->manufacturer.empty() ? pDoc8643->manufacturer : man;
}

std::string LTFlightData::FDStaticData::getMdl() const
{
    return pDoc8643 && !pDoc8643->model.empty() ? pDoc8643->model : mdl;
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
    posDeque.front().ts() <= simTime && simTime < posDeque[1].ts();
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
        ADD_LABEL(cfg.b.bAnyAcId,     statData.acId(key()));
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
        if (!label.empty())
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
    bool bChanged = false;          // change any positions?
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // if our buffer of positions is completely empty we can't do much
        if ( posDeque.empty() || dynDataDeque.empty() )
            return false;
        
        // *** maintenance of flight data deque ***

        const LTAircraft::FlightModel stdMdl;
        const LTAircraft::FlightModel& mdl = pAc ? pAc->mdl : stdMdl;

        // if no simTime given use a/c's 'to' position, or current sim time
        if (isnan(simTime)) {
            if (pAc)
                simTime = pAc->GetToPos().ts();
            else
                simTime = dataRefs.GetSimTime();
            bDoLandTODetect = false;
        }

        // remove from front until [0] <= simTime < [1] (or just one element left)
        while (dynDataDeque.size() >= 2 && dynDataDeque[1].ts <= simTime)
            dynDataDeque.pop_front();
        
        // *** maintenance of buffered positions ***
        
        // Differs depending on: is there an a/c yet?
        if ( pAc ) {
            // if there is an a/c then we just remove all positions before 'simTime'
            while (!posDeque.empty() && posDeque.front().ts() <= simTime - 0.01f) {
                posDeque.pop_front();
                bChanged = true;
            }
            
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
            if (simTime < posDeque.front().ts())
                return false;
            
            // The first pos is in the past, good, make sure it's the only one
            // [0] <= simTime < [1]
            while (posDeque.size() >= 2 && posDeque[1].ts() <= simTime) {
                posDeque.pop_front();
                bChanged = true;
            }
            
            // Unlikely, but theoretically there could now be just one (past) pos left
            if (posDeque.size() < 2)
                return false;
        }
        
        // *** Data Cleansing ***
        //
        // Remove weird positions. A position is 'weird', if
        // - VSI would be more than +/- 2 * mdl.VSI_INIT_CLIMB
        // - heading change would be more than
        if (( pAc && posDeque.size() >= 1) ||
            (!pAc && posDeque.size() >= 3))
        {
            positionTy pos1;
            vectorTy v2;
            double h1 = NAN;
            dequePositionTy::const_iterator iter = posDeque.cbegin();
            
            // position _before_ the first position in the deque
            if (pAc) {
                pos1 = pAc->GetToPos();
                h1 = pAc->GetTrack();       // and the heading towards pos1
                // if (still) the to-Pos is current iter pos then increment
                // (could be that plane's current 'to' is still the first
                //  in out queue)
                while (iter != posDeque.end() &&
                       pos1.cmp(*iter) == 0)
                    ++iter;
            } else {
                // in this case we have at least 3 positions
                pos1 = *std::next(iter);
                vectorTy v1 = iter->between(pos1);
                h1 = v1.dist > SIMILAR_POS_DIST ?
                     v1.angle : pos1.heading();
                std::advance(iter, 2);
            }
            
            // loop over (remaining) pos and verify their validity
            while (iter != posDeque.cend())
            {
                // maximum turn allowed depends on 'on ground' or not
                const double maxTurn = iter->onGrnd == positionTy::GND_ON ?
                MDL_MAX_TURN_GND : MDL_MAX_TURN;

                // vector from previous to current and angle between, i.e. turn angle at iter
                v2 = pos1.between(*iter);
                const double hDiff2 = v2.dist > SIMILAR_POS_DIST ?
                                        HeadingDiff(h1, v2.angle) : 0;

                // Too much of a turn? VSI/speed out of range?
                if (-maxTurn > hDiff2 || hDiff2 > maxTurn   ||
                    v2.vsi_ft() < -3 * mdl.VSI_INIT_CLIMB   ||
                    v2.vsi_ft() > 3 * mdl.VSI_INIT_CLIMB    ||
                    (!isnan(v2.speed_kn()) && v2.speed_kn() > 4 * mdl.FLAPS_DOWN_SPEED))
                {
                    if constexpr (VERSION_BETA) {
                        LOG_MSG(logDEBUG, "%s %s: Invalid v2: %s with hDiff2 = %.0f",
                                key().c_str(), statData.acId("-").c_str(),
                                std::string(v2).c_str(), hDiff2 );
                    }

                    // is there any valid pos to go to after iter?
                    bool bFoundValidNext = false;
                    dequePositionTy::const_iterator next = std::next(iter);
                    for (;
                         next != posDeque.cend();
                         ++next)
                    {
                        // vector from previous to next and angle between, i.e. turn angle at iter
                        vectorTy v3 = pos1.between(*next);
                        const double hDiff3 = v3.dist > SIMILAR_POS_DIST ?
                                                HeadingDiff(h1, v3.angle) : 0;
                        
                        // OK position to go to?
                        if (-maxTurn < hDiff3 && hDiff3 < maxTurn   &&
                            v3.vsi_ft() >= -3 * mdl.VSI_INIT_CLIMB  &&
                            v3.vsi_ft() <=  3 * mdl.VSI_INIT_CLIMB  &&
                            (!isnan(v3.speed_kn()) && v3.speed_kn() <= 4 * mdl.FLAPS_DOWN_SPEED))
                        {
                            bFoundValidNext = true;

                            if constexpr (VERSION_BETA) {
                                LOG_MSG(logDEBUG, "%s %s:   Valid v3: %s with hDiff3 = %.0f",
                                        key().c_str(), statData.acId("-").c_str(),
                                        std::string(v3).c_str(), hDiff3 );
                                LOG_MSG(logDEBUG, Positions2String().c_str() );
                            }
                            // that means we need to remove all positions from
                            // 'iter' to _before_ next.
                            // BUT because std::deque::erase can invalidate _all_ iterators
                            // (including cend!) we cannot do it in one go...after the first erase all iterators are invalid.
                            // Make use of the fact that the deque is sorted by timestamp.
                            const double rmTsFrom = iter->ts();
                            const double rmTsTo = next->ts();
                            while (iter != posDeque.cend() && iter->ts() < rmTsTo)
                            {
                                // if current iter falls into the to-be-deleted range
                                if (rmTsFrom <= iter->ts() && iter->ts() < rmTsTo) {
                                    LOG_MSG(logDEBUG, DBG_INV_POS_REMOVED,
                                        key().c_str(), statData.acId("-").c_str(),
                                        iter->dbgTxt().c_str());
                                    posDeque.erase(iter);               // now all iterators are invalid!
                                    bChanged = true;
                                    iter = posDeque.cbegin();
                                }
                                else {
                                    // otherwise just try next
                                    ++iter;
                                }
                            }
                            
                            // break out of search loop
                            // (iter now points to where 'next' was before, but with updated iterators9
                            LOG_ASSERT_FD(*this, iter != posDeque.cend() && iter->ts() == rmTsTo);
                            break;
                        } // if found valid next pos
                    } // for searching valid next pos
                    
                    // did we find nothing???
                    if (!bFoundValidNext)
                    {
                        // Well...let's hope some better data is coming later
                        // from the network -> stop cleansing here.

                        // There is just one thing we want to avoid:
                        // That the non-continguous data is the next to be picked
                        // by the aircraft. That would make rocket/sliding
                        // planes. So if there is no good data left -> remove
                        // the aircraft to avoid rocketing / sliding.
                        if (pAc && iter == posDeque.cbegin()) {
                            pAc->SetInvalid();
                            if constexpr (VERSION_BETA) {
                                LOG_MSG(logDEBUG, Positions2String().c_str());
                            }
                            LOG_MSG(logDEBUG, DBG_INV_POS_AC_REMOVED,
                                    key().c_str(), statData.acId("-").c_str());
                        }
                        
                        // stop cleansing
                        break;
                    } // if not found any valid next position
                    else if constexpr (VERSION_BETA) {
                        LOG_MSG(logDEBUG, Positions2String().c_str() );
                    }
                } // if invalid pos
                else
                {
                    // just move on to next position in deque
                    h1 = v2.angle;
                    pos1 = *iter;
                    ++iter;
                }
            } // inner while loop over positions
        } // outer if of data cleansing

#ifdef DEBUG
        std::string deb0   ( posDeque.front().dbgTxt() );
        std::string deb1   ( posDeque.size() >= 2 ? std::string(posDeque[1].dbgTxt()) : "<none>" );
        std::string debvec ( posDeque.size() >= 2 ? std::string(posDeque.front().between(posDeque[1])) : "<none>" );
#endif
        
        // *** Landing / Take-Off Detection ***
        
        if ( pAc && bDoLandTODetect ) {
            const positionTy& ppos_ac = pAc->GetPPos();
            positionTy& to_ac   = posDeque.front();
            
            // clear outdated rotate timestamp
            if (!isnan(rotateTS) && (rotateTS + 2 * mdl.ROTATE_TIME < simTime) )
                rotateTS = NAN;
            
            // *** Landing ***
            // If current pos is in the air and next pos is approaching or touching ground
            // then we have live positional data on the ground.
            // However, if we would fly directly to next pos then we would touch down
            // then at next pos only (earliest), which might be a point far down the runway.
            // To simulate touching down at the _beginning_ of the runway and
            // then rolling out to (or through) next pos we determine this case
            // and then insert an artifical touch down position, which just keeps going with
            // previous vsi and speed down to the ground.

            if (ppos_ac.onGrnd == positionTy::GND_OFF &&           // currently off ground
                to_ac.onGrnd   >= positionTy::GND_APPROACHING &&   // future: approaching / on ground
                pAc->GetVSI_ft() < -mdl.VSI_STABLE) {           // descending considerably
                // Case determined: We are landing and have live positional
                //                  data down the runway
                const double timeToTouchDown = -(pAc->GetPHeight_m() / pAc->GetVSI_m_s());
                // but only reasonably a _new_ position if a few seconds before [1]
                if (timeToTouchDown > SIMILAR_TS_INTVL &&
                    ppos_ac.ts() + timeToTouchDown + SIMILAR_TS_INTVL < to_ac.ts()) {
                    vectorTy vecTouch(ppos_ac.heading(),                            // angle
                                      timeToTouchDown * pAc->GetSpeed_m_s(),     // distance
                                      pAc->GetVSI_m_s(),                         // vsi
                                      pAc->GetSpeed_m_s());                      // speed
                    // insert touch-down point at beginning of posDeque
                    positionTy& touchDownPos = posDeque.emplace_front(ppos_ac.destPos(vecTouch));
                    touchDownPos.onGrnd = positionTy::GND_ON;
                    touchDownPos.flightPhase = LTAircraft::FPH_TOUCH_DOWN;
                    TryDeriveGrndStatus(touchDownPos);          // will set correct terrain altitude
                    // then, however, next pos should also be on the ground, no longer just approaching
                    to_ac.onGrnd = positionTy::GND_ON;
                    // output debug info on request
                    if (dataRefs.GetDebugAcPos(key())) {
                        LOG_MSG(logDEBUG,DBG_INVENTED_TD_POS,touchDownPos.dbgTxt().c_str());
                    }
                    bChanged = true;
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
            
            // for the take off case we look further ahead to catch the case
            // that a data point is right between start of rotating and actual lift off
            else for (int i = 0;
                      isnan(rotateTS) && i <= 1 && posDeque.size() >= i+1;
                      i++ )
            {
                // i == 0 is as above with actual a/c present position
                // in later runs we use future data from our queue
                const positionTy& ppos_i  = i == 0 ? pAc->GetPPos() : posDeque[i-1];
                positionTy& to_i          = posDeque[i];
                const vectorTy vec (ppos_i.between(to_i));

                if ((ppos_i.onGrnd == positionTy::GND_ON || ppos_i.onGrnd == positionTy::GND_LEAVING) &&
                    (to_i.onGrnd == positionTy::GND_LEAVING || to_i.onGrnd == positionTy::GND_OFF))
                {
                    // VSI needs to be high enough for actual take off
                    if (vec.vsi_ft() > mdl.VSI_STABLE)
                    {
                        
                        // Get the vsi and speed after 'to' (the (B)-(C) vector's)
                        double climbVsi = mdl.VSI_INIT_CLIMB * Ms_per_FTm;
                        double climbSpeed = mdl.SPEED_INIT_CLIMB / KT_per_M_per_S;
                        if (posDeque.size() >= i+2) {     // take the data from the vector _after_ to
                            vectorTy climbVec (to_i.between(posDeque[i+1]));
                            climbVsi = climbVec.vsi;
                            climbSpeed = climbVec.speed;
                        }
                        
                        // Determine how much before 'to' is that take-off point
                        // We want an accurate terrain altitude for this calc
                        const double toTerrAlt = YProbe_at_m(to_i);
                        const double height_m = to_i.alt_m() - toTerrAlt; // height to climb to reach 'to'?
                        const double toClimb_s = height_m / climbVsi;   // how long to climb to r	each 'to'?
                        const double takeOffTS = to_i.ts() - toClimb_s;   // timestamp at which to start the climb, i.e. take off
                        
                        // Continue only for timestamps in the future
                        if (ppos_i.ts() < takeOffTS)
                        {
                            rotateTS = takeOffTS - mdl.ROTATE_TIME;         // timestamp when to rotate

                            // find the TO position by applying a reverse vector to the pointer _after_ take off
                            vectorTy vecTO(fmod(vec.angle + 180, 360),  // angle (reverse!)
                                           climbSpeed * toClimb_s,      // distance
                                           -climbVsi,                   // vsi (reverse!)
                                           climbSpeed);                 // speed
                            // insert take-off point ('to' minus vector from take-off to 'to')
                            // at beginning of posDeque
                            positionTy takeOffPos = to_i.destPos(vecTO);
                            takeOffPos.onGrnd = positionTy::GND_ON;
                            takeOffPos.flightPhase = LTAircraft::FPH_LIFT_OFF;
                            takeOffPos.alt_m() = toTerrAlt;
                            takeOffPos.heading() = vec.angle;           // from 'reverse' back to forward
                            takeOffPos.ts() = takeOffTS;                // ts was computed forward...we need it backward
                            
                            // find insert position
                            dequePositionTy::iterator toIter = posDeque.end();
                            for (dequePositionTy::iterator iter = posDeque.begin();
                                 iter != posDeque.end();
                                 ++iter)
                            {
                                // before take off we stay on the ground
                                if (*iter < takeOffPos) {
                                    if (iter->onGrnd != positionTy::GND_ON) {
                                        iter->onGrnd = positionTy::GND_ON;
                                        TryDeriveGrndStatus(*iter);
                                        bChanged = true;
                                    }
                                } else {
                                    // found insert position!
                                    toIter = posDeque.insert(iter, takeOffPos);
                                    break;
                                }
                            }
                            
                            // found no insert position??? need to add it to the end
                            if (toIter == posDeque.end())
                                posDeque.push_back(takeOffPos);
                            
                            // output debug info on request
                            if (dataRefs.GetDebugAcPos(key())) {
                                LOG_MSG(logDEBUG,DBG_INVENTED_TO_POS,takeOffPos.dbgTxt().c_str());
                            }
                            bChanged = true;
                            
                            // leave loop of szenarios
                            break;
                        }
                    }
                    // * Don't yet take off *
                    // else VSI is not enough for actual take off -> stick to ground!
                    else {
                        to_i.onGrnd = positionTy::GND_ON;
                        TryDeriveGrndStatus(to_i);        // set alt to terrain
                        bChanged = true;
                    }
                    
                } // (take off case)
            } // loop over szenarios
        } // (has a/c and do landing / take-off detection)
        
        // if something changed
        if (bChanged) {
            // recalc all headings
            for (dequePositionTy::iterator iter = posDeque.begin();
                 iter != posDeque.end();
                 ++iter)
                CalcHeading(iter);
            
            // output all positional information as debug info on request
            if (dataRefs.GetDebugAcPos(key())) {
                LOG_MSG(logDEBUG,DBG_POS_DATA,Positions2String().c_str());
            }
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
        it->heading() = HeadingAvg(vecTo.angle,
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
            it->heading() = HeadingAvg(pBefore->heading,
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
    
    // just as a safeguard...they can't be many situations this triggers,
    // but we don't want nan values any longer after this
    if (isnan(it->heading()))
        it->heading() = 0;
}


void LTFlightData::AddNewPos ( positionTy& pos )
{
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
        // will set ground altitude if on ground
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
            // make sure we don't overlap with predecessor/successor position
            if (((i == posDeque.begin()) || (*std::prev(i) < pos)) &&
                ((std::next(i) == posDeque.end()) || (*std::next(i) > pos)))
            {
                *i |= pos;                  // merge them (if pos.heading is nan then i.heading prevails)
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
        
        CalcHeading(i);                         // i itself, latest here a nan heading is rectified
        
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
                         [&to](const positionTy& p){return to < p;});
            
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
        struct tm tm;
        gmtime_s(&tm, &t);

        char szBuf[50];
        snprintf(szBuf,sizeof(szBuf),
                 "a/c %s %s SimTime: %.1f - ",
                 key().c_str(),
                 statData.acId("-").c_str(),
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
        
        // We don't mix channels. They aren't in synch, mixing them leads
        // to planes jumping around and other weird behaviour.
        // We allow a change of channel only if the current channel seems
        // outdated and unresponsive.
        // We allow a change of channel if this prevents the aircraft from
        // disappearing, i.e. if this is the last update before a/c outdated period,
        // or in other words: new ts + refresh period > old ts + outdated period
        if (!dynDataDeque.empty()) {
            const FDDynamicData& last = dynDataDeque.back();
            if (last.pChannel != inDyn.pChannel) {
                // We compare timestamps of the actual positions and as a safeguard
                // accept positions only pointing roughly in the same direction
                // as we don't want a/c to turn back and forth due to channel switch.
                if (!pos) return;
                if (!posDeque.empty()) {
                    const positionTy& lastPos = posDeque.back();
                    if (pos->ts() + dataRefs.GetFdRefreshIntvl() <=
                        lastPos.ts() + dataRefs.GetAcOutdatedIntvl())
                        // not big enough a difference in timestamps yet
                        return;
                    
                    // check for weird heading changes, don't allow more than 90-120°
                    // lastPos.heading points to the direction of flight
                    // (because it's currently the last pos in the deque),
                    // determine heading to new pos and compare:
                    vectorTy vec (lastPos.between(*pos));
                    double diff = abs(HeadingDiff(lastPos.heading(), vec.angle));
                    if (lastPos.onGrnd  == positionTy::GND_ON &&
                        pos->onGrnd     == positionTy::GND_ON) {
                        if (diff > MDL_MAX_TURN_GND)
                            return;
                    } else {
                        if (diff > MDL_MAX_TURN)
                            return;
                    }
                }
                
                // accept channel switch!
                LOG_MSG(logDEBUG, DBG_AC_CHANNEL_SWITCH,
                        key().c_str(), statData.acId("-").c_str(),
                        last.pChannel ? last.pChannel->ChName() : "<null>",
                        inDyn.pChannel ? inDyn.pChannel->ChName() : "<null>")
            }
        }
        
        // only need to bother adding data if it is newer than current data
        if (dynDataDeque.empty() || dynDataDeque.front() < inDyn)
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
            outDyn = dynDataDeque.empty() ? FDDynamicData() : dynDataDeque.front();
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
    return dynDataDeque.empty() ? FDDynamicData() : dynDataDeque.front();
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
void LTFlightData::UpdateData (const LTFlightData::FDStaticData& inStat,
                               bool bFullInit)
{
    try {
        // access guarded by a mutex
        std::lock_guard<std::recursive_mutex> lock (dataAccessMutex);
        
        // merge inStat into our statData (copy only filled fields):
        statData |= inStat;
        if (bFullInit)
            statData.bInit = true;
        
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
        
        // if the a/c became invalid then remove the aircraft object,
        // but retain the remaining flight data
        if (pAc && !pAc->IsValid())
            DestroyAircraft();
        
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
                if ( posDeque.front().ts() <= simTime)
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
