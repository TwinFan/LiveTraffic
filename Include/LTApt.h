/// @file       LTApt.h
/// @brief      Access to X-Plane's `apt.dat` file(s) and data
/// @details    Scans `apt.dat` file for airport, runway, and taxiway information.\n
///             Finds potential runway for an auto-land flight.\n
///             Finds center lines on runways and taxiways to snap positions to.
/// @author     Birger Hoppe
/// @copyright  (c) 2020 Birger Hoppe
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

#ifndef LTApt_h
#define LTApt_h

/// Start reading apt.dat file(s), build an index
bool LTAptEnable ();

/// Update the airport data with airports around current camera position
void LTAptRefresh ();

/// @brief Return the best possible runway to auto-land at
/// @param _mdl flight model definitions to use for calculations
/// @param _from Start pos/heading for seach
/// @param _speed_m_s Speed during approach
/// @param _logTxt (optional) Shall decision be logged? If yes, with which id text?
/// @return Position of matching runway touch-down point, incl. timestamp and heading (of runway); positionTy() if nothing found
/// @note Call from separate thread, like from CalcNextPos
positionTy LTAptFindRwy (const LTAircraft::FlightModel& _mdl,
                         const positionTy& _from,
                         double _speed_m_s,
                         const std::string& _logTxt = "");

/// @brief Return the best possible runway to auto-land at based on current pos/heading/speed of `_ac`
/// @param _ac Aircraft in search for a landing spot. It's last go-to position and VSI as well as its model are of importance
/// @param bDoLogging (optional) Shall decision be logged?
/// @return Position of matching runway touch-down point, incl. timestamp and heading (of runway)
/// @note Call from separate thread, like from CalcNextPos
inline positionTy LTAptFindRwy (const LTAircraft& _ac, bool bDoLogging = false)
{
    return LTAptFindRwy (_ac.mdl, _ac.GetToPos(), _ac.GetSpeed_m_s(),
                         bDoLogging ? std::string(_ac) : "");
}

/// @brief Snaps the passed-in position to the nearest rwy or taxiway if appropriate
/// @param fd Flight data object to be analyzed
/// @param[in,out] posIter Iterator into LTFlightData::posDeque, points to position to analyze, might change due to inserted taxi positions
/// @param bInsertTaxiTurns Shall additional taxi turning points be added into LTFlightData::posDeque?
/// @return Changed the position?
bool LTAptSnap (LTFlightData& fd, dequePositionTy::iterator& posIter,
                bool bInsertTaxiTurns);

/// Cleanup
void LTAptDisable ();

#ifdef APT_DUMP
/// @brief Dumps the entire taxi network into a CSV file readable by GPS Visualizer
/// @see https://www.gpsvisualizer.com/
void LTAptDump (const std::string& _aptId);
#endif

#endif /* LTApt_h */
