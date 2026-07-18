/// @file       Constants.h
/// @brief      Constant definitions for LiveTraffic
/// @details    Version Information.\n
///             Unit Conversions.\n
///             Flight Model defaults.\n
///             Menu item texts.\n
///             Informational, warning, and error message texts.\n
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

#ifndef Constants_h
#define Constants_h

//
// MARK: Version Information (CHANGE VERSION HERE)
//

/// Verson number combined as a single unsigned, like 3.2.1 = 30201
constexpr unsigned LT_VER_NO = 10000 * LIVETRAFFIC_VER_MAJOR + 100 * LIVETRAFFIC_VER_MINOR + LIVETRAFFIC_VER_PATCH;
extern unsigned verXPlaneOrg;       ///< version on X-Plane.org

//MARK: Window Position
constexpr int WIN_WIDTH = 450;      ///< initial Msg Wnd width
constexpr int WIN_FROM_TOP = 50;    ///< initial Msg Wnd position from top
constexpr int WIN_FROM_RIGHT = 10;  ///< initial Msg Wnd position from right

constexpr int WIN_TIME_DISPLAY=8;       // duration of displaying a message windows
constexpr int WIN_TIME_DISP_ERR=12;     // duration of displaying an error/fatal message
constexpr float WIN_TIME_REMAIN=1.0f;   // seconds to keep the msg window after last message

//MARK: Unit conversions
constexpr int M_per_NM      = 1852;     // meter per 1 nautical mile = 1/60 of a lat degree
constexpr double M_per_FT   = 0.3048;   // meter per 1 foot
constexpr int M_per_KM      = 1000;
constexpr double KT_per_M_per_S = 1.94384;  // 1m/s = 1.94384kt
constexpr double MSC_per_KMH = 0.2777777778;    ///< m/s per km/h
constexpr double NM_per_KM  = 1000.0 / double(M_per_NM);
constexpr double M_per_SM   = 1609.344; ///< meters per statute mile
constexpr int SEC_per_M     = 60;       // 60 seconds per minute
constexpr int SEC_per_H     = 3600;     // 3600 seconds per hour
constexpr int H_per_D       = 24;       // 24 hours per day
constexpr int M_per_D       = 1440;     // 24*60 minutes per day
constexpr int SEC_per_D     = SEC_per_H * H_per_D;        // seconds per day
constexpr double Ms_per_FTm = M_per_FT / SEC_per_M;     //1 m/s = 196.85... ft/min
constexpr double PI         = 3.1415926535897932384626433832795028841971693993751;
constexpr double EARTH_D_M  = 6371.0 * 2 * 1000;    // earth diameter in meter
constexpr double JAN_FIRST_2019 = 1546344000;   // 01.01.2019
constexpr double HPA_STANDARD   = 1013.25;      // air pressure
constexpr double INCH_STANDARD  = 29.92126;
constexpr double HPA_per_INCH   = HPA_STANDARD/INCH_STANDARD;
constexpr double TEMP_STANDARD  = 288.15f;      ///< Standard temperatur of 15°C in °Kelvin
constexpr double R_Lb_G0_M      = 0.1902632365f;///< -(R * Lb)/(g0 * M), @see https://www.mide.com/air-pressure-at-altitude-calculator
constexpr double TEMP_LAPS_R    = -0.0065f;     ///< K/m

//MARK: Flight Data-related
constexpr unsigned MAX_TRANSP_ICAO = 0xFFFFFF;  // max transponder ICAO code (24bit)
constexpr int    MAX_NUM_AIRCRAFT   = 300;      ///< maximum number of aircraft allowed to be rendered
constexpr double FLIGHT_LOOP_INTVL  = -5.0;     // call ourselves every 5 frames
constexpr double AC_MAINT_INTVL     = 2.0;      // seconds (calling a/c maintenance periodically)
constexpr double TIME_REQU_POS      = 0.5;      // seconds before reaching current 'to' position we request calculation of next position
constexpr double SIMILAR_TS_INTVL = 3;          // seconds: Less than that difference and position-timestamps are considered "similar" -> positions are merged rather than added additionally
constexpr double SIMILAR_POS_DIST = 10;         // [m] if distance between positions less than this then favor heading from flight data over vector between positions
constexpr double CLOSE_POS_TS_INTVL = 12;       // [s] if two positions are closer to each other than this on a straight line, we remove the middle one as it doesn't add value
constexpr double CLOSE_POS_HEADING  = 5;        // [deg] "straight" is defined as no more than this many degrees of difference
constexpr double GND_COLLISION_DIST = 15;       // [m] If another aircraft comes this close to a parked aircraft then the parked aircraft is removed
constexpr double HIGHEST_AIRPORT_M  = 4411.0;   // [m] Altitude of the world's highest airport, https://en.wikipedia.org/wiki/List_of_highest_airports

/// [m] Maximum distance the rendered (live-tracked) position of an aircraft
/// may be from a parked-feed gate position before the periodic parked
/// re-fetch is allowed to re-seed that aircraft with the gate position.
///
/// RT's parked DB updates on a daily cadence and lags real-world activity
/// by hours; once an aircraft has pushed back, taxied, or taken off, RT
/// can still be reporting it as "parked at gate X" for some time. Without
/// this skip, the periodic re-fetch (RT_PARKED_REFRESH_INTVL_S) silently
/// injects seed positions at the original gate into the *back* of the
/// aircraft's deque (at the current simTime + lookahead window). When the
/// render clock subsequently advances into those slots the aircraft
/// visually teleports back to the gate, then forward again as later live
/// data arrives. 50 m comfortably keeps us inside a stand footprint while
/// excluding anything past the nearest taxiway centerline.
constexpr double GATE_REFEED_MAX_DIST_M = 50;

/// Maximum distance, in metres, between an aircraft's held position and the
/// nearest apt.dat startup-location (gate / stand / ramp slot) for the
/// position to be treated as "at a gate". Used as the third gate-detection
/// path in `LTFlightData::AddNewPos`: when `bGroundHolding` flips true on a
/// live-tracked aircraft (one that never received a RealTraffic parked-feed
/// seed), we query `LTAptFindStartupLoc()` and set `bGateParked = true` only
/// when the apt.dat lookup returns a startup-loc within this radius. The
/// value is small on purpose — typical stand widths are 20–60 m, and a tight
/// threshold prevents false positives from runway hold-shorts, taxiway
/// crossings, or maintenance pads being misclassified as gates (which would
/// later mis-trigger the pushback state machine).
constexpr double GATE_DETECT_MAX_DIST_M = 30.0;

/// Maximum angular delta, in degrees, between the prior parked heading and
/// the first-motion feed heading for the feed to be considered a true-nose
/// source during a pushback. Used at the PB_NONE→PB_ACTIVE entry in
/// `LTFlightData::CalcHeading`: if `|feedHdg − prePosPb.heading()| <=` this
/// value, the feed value is locked in as the nose source for the rest of
/// the push (so the rendered nose tracks the aircraft's real rotation,
/// reported by the feed). Otherwise the feed is treated as course-over-
/// ground and the nose is derived from the motion track instead.
///
/// 30° is chosen because a true-nose feed reads exactly the parked heading
/// when the aircraft is stationary; once motion starts the feed catches up
/// within a fraction of a second, so the very first motion slot's feedHdg
/// is still within a handful of degrees of the parked value. A course feed,
/// on the other hand, reports motion direction once motion starts — and
/// during a pushback that is ~180° away from the parked heading, well
/// outside the 30° window. The threshold is therefore comfortably wider
/// than measurement noise yet far tighter than the course/nose gap.
constexpr double PB_FEED_NOSE_AGREE_DEG = 30.0;

/// Minimum groundspeed, in knots, for a slot to be classified as
/// "moving" by the pushback state machine in `LTFlightData::CalcHeading`.
/// Distinct from (and lower than) `GND_STATIONARY_GS_KT` because real
/// pushbacks roll at 0.4-1.4 kt — entirely below the global stationary
/// threshold (1.5 kt). Using the global threshold for the PB bMotion
/// check would mean the state machine never enters PB_ACTIVE for an
/// actual slow push, and the entry-gate logic in AddNewPos would keep
/// suppressing subsequent in-push slots (which depend on
/// `pbState != PB_NONE` to bypass the gate-hold).
///
/// 0.3 kt is a hair above the noise floor of derived gs at sub-second
/// dt: a 3 m position-fix jitter over a 5 s slot yields ~0.6 m/s ≈
/// 1.1 kt of false gs. We need a threshold under the actual slow-push
/// speed but above zero; 0.3 kt is comfortably both. Combined with the
/// upstream distance gate (GATE_HOLD_MIN_ACCEPT_M ≥ 30 m before the
/// first slot is accepted), this leaves no realistic path for noise to
/// trip the state machine.
constexpr double PB_MOTION_GS_KT        = 0.3;

/// Safety-valve maximum groundspeed, in knots, for the pushback state
/// machine. If a slot under PB_ACTIVE/PB_PAUSED arrives with `gs` above
/// this threshold, the state machine FORCES an exit to PB_NONE regardless
/// of the directional-resumed-motion test.
///
/// Why this safety valve is needed: the normal exit logic compares
/// resumed-motion track against `pbHeldNose` (within 90° → forward
/// taxi → exit). When the held nose was chosen incorrectly at entry
/// (e.g. TRACK+180 picked because the feed disagreed with the parked
/// heading, but the feed was actually right because the aircraft had
/// already rotated during the GATE_HOLD suppression window), the
/// directional exit test reads against the wrong reference. The
/// aircraft then taxis out at 20+ knots while the state machine still
/// thinks "tug is pushing me backward" and renders the nose stuck at
/// the wrong angle — aircraft appears tail-first / ass-forward.
///
/// real pushbacks roll at 1-5 kt; tugs cannot move a 60+ ton airframe
/// faster than that). Any sustained gs above 5 kt is definitively
/// taxi, not pushback, and the state machine is wrong to still be
/// active. Force-exit and let the normal heading logic take over.
constexpr double PB_MAX_GS_KT           = 5.0;

/// Minimum distance, in metres, between an incoming feed slot and the
/// latest accepted deque position for the slot to be admitted while the
/// aircraft is `bGateParked` and not yet in the pushback state machine.
/// Slots closer than this are treated as feed noise and silently dropped.
///
/// Why distance, not groundspeed or a slot counter: real pushbacks roll
/// at 0.4-1.4 kt, which is well below `GND_STATIONARY_GS_KT` (1.5 kt).
/// A counter that increments on "non-stationary" slots therefore never
/// advances during a real slow push, and any threshold based on that
/// counter would suppress legitimate pushback motion forever. RT-direct
/// noise at the gate, by contrast, manifests as one or two slots ~15-25 m
/// off the true position that then return to the gate; their distance
/// from the held position never sustains beyond ~25 m.
///
/// 30 m is a comfortable separator: it sits clearly above the 15-25 m
/// noise envelope (so single- or paired-slot anomalies stay dropped),
/// while a real push reaches 30 m within 2-3 slots at typical pushback
/// speeds. When a slot finally exceeds this distance, the suppression
/// accepts it AND clears `bGroundHolding` so subsequent in-push slots
/// (which are typically only 3-6 m from the previous accepted slot, and
/// would otherwise be trivial-dropped) flow through and the rendered
/// push continues smoothly.
constexpr double GATE_HOLD_MIN_ACCEPT_M = 30.0;
constexpr double FD_GND_AGL =       10;         // [m] consider pos 'ON GRND' if this close to YProbe
constexpr double FD_GND_AGL_EXT =   20;         // [m] consider pos 'ON GRND' if this close to YProbe - extended, e.g. for RealTraffic
constexpr double PROBE_HEIGHT_LIM[] = {5000,1000,500,-999999};  // if height AGL is more than ... feet
constexpr double PROBE_DELAY[]      = {  10,   1,0.5,    0.2};  // delay next Y-probe ... seconds.
constexpr double MAX_HOVER_AGL      = 2000;     // [ft] max hovering altitude for hover-along-the-runway detection
constexpr double KEEP_ABOVE_MAX_ALT    = 18000.0 * M_per_FT;///< [m] Maximum altitude to which the "keep above 2.5° glidescope" algorithm is applied (highest airports are below 15,000ft + 3,000 for approach)
constexpr double KEEP_ABOVE_MAX_AGL    =  3000.0 * M_per_FT;///< [m] Maximum height above ground to which the "keep above 2.5° glidescope" algorithm is applied (highest airports are below 15,000ft + 3,000 for approach)
constexpr double KEEP_ABOVE_RATIO      = 0.043495397807572; ///< = tan(2.5°), slope ratio for keeping a plane above the approach to a runway
constexpr float  EXPORT_USER_AC_PERIOD = 15.0f; ///< [s] how often to write user's aircraft data into the export file
constexpr const char* EXPORT_USER_CALL = "USER";///< call sign used for user's plabe

//MARK: Ground Behavior Stability
// -----------------------------------------------------------------------------
// Tunables that govern how aircraft are rendered while on the ground. The
// underlying problem is that public flight feeds (ADS-B, MLAT, multilat-fused
// channels) supply position samples roughly once per second with a few metres
// of positional noise. When an aircraft is stationary or slow-taxiing, that
// noise — if fed straight into the heading-from-position-delta math — produces
// wildly varying headings, which renders visually as the aircraft pivoting and
// "dancing" at the gate. The constants below enable a layered set of
// countermeasures: stationary detection, heading hysteresis, per-frame
// heading rate-limiting, a holding mode that ignores trivial position jitter
// after the aircraft has been parked for a while, hard-set ground attitude,
// and pushback detection. Every constant here is chosen for *why* documented
// inline; tweak with that rationale in mind.
// -----------------------------------------------------------------------------

/// [°] dead-band around the target heading inside which the rendered nose is
/// not moved. Empirical: RealTraffic feeds without a heading field (e.g. for
/// some channels) produce track-from-pos-delta wobbles of ~5–7° per slot at
/// slow taxi (1–6 kt, 10 m chunks). A 4° band absorbs most of that noise
/// while still letting genuine 5°+ taxi turns propagate. Original 0.5° was
/// far too tight to catch the real-world jitter envelope.
constexpr double GND_HEADING_HYSTERESIS_DEG     = 4.0;

/// [°/s] maximum rate at which the rendered heading is allowed to walk while
/// on the ground. Tuned to roughly match `TAXI_TURN_TIME` in the flight
/// model (30 s for a 360° turn = 12°/s natural rate). At this clamp a 7°
/// per-slot wobble takes ~0.6 s to walk through, which the eye reads as
/// smooth rotation; meanwhile real taxi turns of ~90° finish in ~7.5 s.
/// Previous value of 60°/s never actually engaged because per-frame heading
/// changes were always far below it.
constexpr double GND_HEADING_MAX_RATE_DPS       = 12.0;

/// [kn] groundspeed at-or-below which an aircraft is considered stationary
/// for the purposes of heading freezing and holding detection. Empirical:
/// parked aircraft at gates routinely have derived gs of 0.7–1.1 kt purely
/// from positional jitter in the feed (e.g. ±5 m over 10 s = 1 kt). Setting
/// the threshold above this band (1.5 kt) ensures parked aircraft stay in
/// the stationary regime while real slow taxi (≥2 kt observed) is still
/// classified as moving.
constexpr double GND_STATIONARY_GS_KT           = 1.5;

/// [s] continuous stationary streak after which the aircraft enters "holding"
/// mode. While holding, trivial position jitter is rejected (see
/// `GND_HOLDING_TRIVIAL_DIST_M`). 30 s was chosen as long enough that brief
/// taxi-pauses (e.g., at hold-short lines) do not trip the suppressor, while
/// short enough that genuinely parked aircraft become rock-steady within
/// half a minute of arriving at the stand.
constexpr double GND_HOLDING_TIMEOUT_S          = 30.0;

/// [m] inside holding mode, any new position update whose distance from the
/// current rendered position is below this threshold AND whose reported
/// groundspeed is below `GND_STATIONARY_GS_KT` is treated as feed noise and
/// silently dropped — the rendered aircraft does not move. Empirical:
/// parked-aircraft jitter envelope on the data we observed is up to ~7 m,
/// occasionally 12 m. 15 m gives comfortable margin so that all jitter is
/// caught while a single 15 m+ jump (typical of real taxi-leg starts) still
/// signals genuine motion and breaks the suppression.
constexpr double GND_HOLDING_TRIVIAL_DIST_M     = 15.0;

/// [m] minimum chord length (from→to in the local meters frame) below which
/// the ground-rendering Catmull-Rom spline is skipped and a linear position
/// interpolation is used instead.
///
/// Why this exists: when the aircraft is parked or barely creeping, all four
/// spline control points sit within the ADS-B/MLAT feed-noise envelope
/// (~3 m typical). A Catmull-Rom tangent through 4 near-coincident-but-noisy
/// points is dominated by noise — the spline-derived heading swings around
/// even though `CalcHeading` has correctly frozen the slot-level heading to
/// the last good value. The render path then overwrites that frozen heading
/// with the noisy tangent, producing visible z-axis wobble on stopped
/// aircraft.
///
/// 5 m is comfortably above the ~3 m noise floor and well below a meaningful
/// taxi step (a 1 kt creep over a 5 s slot is only ~2.6 m; real slow taxi
/// at 3 kt produces ~7.7 m per 5 s slot, well above the threshold). Below
/// 5 m we treat the leg as "no useful motion" and let `from.heading()` —
/// which is already the frozen lastGood value — pass through unchanged.
constexpr double GND_SPLINE_MIN_CHORD_M         = 5.0;

/// [kn] leg-average ground speed above which the ground-rendering spline is
/// skipped in favour of plain linear interpolation.
///
/// Why this exists: the centripetal Catmull-Rom spline earns its keep at
/// *taxi* speed, where the aircraft turns and linear-chord interpolation
/// would render it sliding sideways through the corner. At runway speed —
/// takeoff roll and landing rollout — the aircraft tracks a dead-straight
/// line down the centreline; it does not (and physically cannot) turn. In
/// that regime the spline adds only fragility:
///   * a single glitchy feed sample (observed: a 777 takeoff roll with
///     feed speeds jumping 187→87→159 kt within 3 s) becomes a control
///     point the curve bulges around — linear interpolation would just
///     draw a straight line slightly off, far less visible;
///   * feed positions during the roll are sparse and irregularly spaced
///     (observed: a 31 s gap between samples while accelerating), so the
///     per-segment curve shape and the arc-length LUT change drastically
///     leg to leg;
///   * the spline's arc-length reparameterisation interacts awkwardly
///     with the acceleration-profile parameter `speed.getRatio()`.
/// Linear interpolation renders a straight line exactly, is immune to all
/// of the above, and returns `from` precisely at f=0 so the segment-switch
/// continuity mechanism stays seamless.
///
/// 40 kn is chosen because normal taxi tops out around 20-25 kn and even
/// an aggressive high-speed runway turnoff is taken below ~40 kn — so at
/// 40 kn and above we are unambiguously in straight-line runway motion,
/// while every speed at which the aircraft actually turns still gets the
/// spline.
constexpr double GND_SPLINE_MAX_KT              = 40.0;

/// Neighbour weight for the ground-spline control-point smoothing kernel.
///
/// Why this exists: a centripetal Catmull-Rom spline *interpolates* — the
/// rendered curve passes exactly through control points P1 and P2. So a
/// single noisy feed sample at P1 or P2 produces a visibly noisy rendered
/// position, and no amount of look-ahead buffering changes that, because
/// the curve is still pinned to the raw (noisy) point. To actually reduce
/// jitter the spline has to *approximate* the data instead of interpolating
/// it.
///
/// We achieve that cheaply by pre-smoothing the *look-ahead* control point
/// P2 with its immediate neighbours using a 3-tap binomial kernel before
/// the curve is fit:
///   P2' = w·P1 + (1−2w)·P2 + w·P3
/// With w = 0.25 this is the classic [1,2,1]/4 kernel — a mild low-pass
/// that pulls a noisy point a quarter of the way toward the average of its
/// neighbours. The points P1..P3 are already cached for the spline, so no
/// deeper buffer is needed.
///
/// Only P2 is smoothed — never P1. The segment-switch logic in
/// `LTAircraft::CalcPPos` overwrites the new leg's start slot with the
/// current rendered position (`posList.front() = ppos`) so the leg
/// continues seamlessly from wherever the renderer is. That only works if
/// the spline returns P1 (== `from`) EXACTLY at u=0, which the unmodified
/// centripetal evaluator does. Smoothing P1 would make the evaluator
/// return P1' ≠ from at u=0, producing a visible position snap at every
/// segment switch (~6 s cadence). Smoothing P2 alone still removes the
/// jitter completely: each feed sample is reached as the smoothed P2'
/// endpoint of its leg and then carried into the next leg as `from`, so
/// the rendered path threads the smoothed points {P2'_k} without ever
/// visiting a raw noisy sample.
///
/// Trade-off: on a genuine sharp taxi turn the kernel pulls the apex inward
/// by w of its deviation (corner-cutting). At w = 0.25 this is visually
/// indistinguishable from a real aircraft arcing through a turn — aircraft
/// do not pivot on a point — and the centripetal parameterisation already
/// rounds corners gracefully. Set to 0.0 to disable smoothing entirely and
/// fall back to pure interpolation.
constexpr double GND_SPLINE_SMOOTH_WEIGHT       = 0.25;

/// [s] backward sim-time jump tolerated by `LTAircraft::NextCycle` before it
/// triggers a full plugin re-init.
///
/// X-Plane's sim time is supposed to be monotonic, but in practice it can step
/// backward by small amounts after a frame stutter, a brief pause/unpause, an
/// autosave hiccup, or any operation that retroactively adjusts the timestamp
/// of the current frame. A strict `diffTime < 0` test (the original behaviour)
/// trips on any of these, tearing the entire aircraft fleet down and rebuilding
/// from buffered data — a very visible "everything disappears, then traffic
/// fades back in over ~10 s as the deques refill" interruption that is well
/// out of proportion to a sub-second clock blip.
///
/// We tolerate backward jumps shallower than this threshold: the per-frame
/// interpolators read `simTime` directly so a small reversal just means the
/// next frame renders at a slightly earlier interpolation point (visually a
/// brief stutter at most, no state corruption). Genuine time-warps — the user
/// changing time-of-day in the X-Plane menu, or skipping ahead via the date
/// dialog — produce jumps far larger than 1 s and still trigger the safety.
///
/// Asymmetric on purpose: the *forward* limit stays at GetFdBufPeriod()
/// because forward-jumping past the buffer window means the data we have is
/// genuinely stale and re-init is the right response. Only the backward case
/// got the grace.
constexpr double TIME_NONLINEAR_BACKWARD_S      = -1.0;

/// Consecutive non-stationary feed updates required to actually exit holding.
/// A single isolated above-threshold slot (which is common — feed jitter can
/// transiently produce gs of 2 kt for one sample) should not break a stable
/// holding lock. Requiring two in a row means we're in real-taxi territory
/// before we trust the motion.
constexpr int    GND_HOLDING_EXIT_CONSEC        = 2;

/// [kn] groundspeed ceiling under which the feed-provided heading is
/// considered as a possible source for the rendered nose direction. This
/// is the OUTER bound — within this band a secondary cross-check against
/// the position-derived track decides which source actually wins. See
/// `GND_FEED_TRACK_AGREE_DEG` below. Above this speed the position track
/// is always preferred (real taxi / rollout / takeoff).
constexpr double GND_USE_FEED_HEADING_MAX_KT    = 10.0;

/// [°] agreement window between the feed-provided heading and the
/// position-derived track angle. Used inside the on-ground feed-heading
/// branch in `LTFlightData::CalcHeading` to decide whether the feed
/// value is fresh enough to trust or has gone stale during a taxi turn.
///
/// Why this matters: the heading reported in ADS-B/Mode S Enhanced
/// Surveillance (EHS) updates at a low rate — typically every 10 s,
/// sometimes slower, and not at all in regions without enhanced
/// interrogation coverage. Between EHS updates the feed value is held
/// constant by the ground station / receiver, so when an aircraft turns
/// during taxi the feed heading can lag the actual nose direction by
/// 10+ seconds (60° or more at a typical 6 °/s taxi turn rate). If the
/// renderer trusts that stale value, the aircraft visibly slides
/// sideways through the turn — its nose stays at the pre-turn direction
/// while its body progresses along the new direction.
///
/// We compare the feed heading against the track angle (the bearing
/// from the previous slot to this one — always "now"). Three regimes:
///   * `Δ < GND_FEED_TRACK_AGREE_DEG` (default 30°)
///     — feed and track agree: either the aircraft is going straight, or
///     the most recent EHS reading is fresh. Trust the feed value
///     (smooth, matches the transponder-reported nose).
///   * `GND_FEED_TRACK_AGREE_DEG ≤ Δ ≤ 180° − GND_FEED_TRACK_AGREE_DEG`
///     — feed has gone stale during a turn. Fall through to the
///     position-derived heading branch, which uses the current track.
///   * `Δ > 180° − GND_FEED_TRACK_AGREE_DEG` — track is roughly opposite
///     of the feed value: this is pushback. Trust the feed (nose stays
///     pointing at the gate while the body moves backwards).
///
/// 30° is wide enough to absorb a few seconds of EHS lag during a slow
/// taxi turn without flapping between feed and track on every degree of
/// gentle curvature, and narrow enough to catch the lag before the
/// sideways look becomes objectionable. Tunable in either direction
/// if real-world reports suggest a different balance.
constexpr double GND_FEED_TRACK_AGREE_DEG       = 30.0;

/// [kn] groundspeed at-or-above which the rendered nose direction is locked
/// to the direction of motion (the vector from `from` to `to` in the slot
/// interpolation), and any Bezier path between slots is suppressed in favour
/// of a straight-line interpolation.
///
/// Why this exists: at higher ground speeds (landing rollout, takeoff roll,
/// fast taxi) the rendered aircraft must visually track ALONG its line of
/// motion. The slot-side heading filters (stationary freeze, hysteresis,
/// pushback detect) work correctly here — but the per-leg renderer in
/// `LTAircraft::CalcAcPos` walks heading toward the NEXT slot's reported
/// heading via a Bezier whose end-tangent is `to.heading()`. When the next
/// slot is on a turn-off taxiway (heading 326°) and the current slot is at
/// end-of-runway (heading 020°), the Bezier arcs across the corner —
/// aircraft visually "slides off the runway" with its nose pointing 53°
/// off the direction of motion.
///
/// At gs ≥ 10 kn we therefore:
///   1. Skip Bezier and force linear interpolation between slots, which
///      walks heading toward `vec.angle` (the direct bearing from `from`
///      to `to` — i.e., the actual direction of motion).
///   2. Skip the half-way-through retarget to `to.heading()` so the
///      rendered heading stays locked to the motion vector for the
///      entire leg, only converging on the slot's reported heading once
///      the aircraft has decelerated below this threshold.
///
/// 10 kn matches `GND_USE_FEED_HEADING_MAX_KT` so the two thresholds are
/// the boundary between "trust the feed heading" (slow) and "trust the
/// motion vector" (fast). No middle ground.
constexpr double GND_TRACK_HEADING_MIN_KT       = 10.0;

/// [°] pitch hard-set on every frame while the aircraft is on the ground
/// (except during the take-off / flare phases, which manage pitch dynamically).
/// 0° (level) matches LiveTraffic's pre-existing convention (the touch-down
/// transition previously walked pitch to 0) and avoids the visible "tail-
/// dragger" look the previous 2° value produced on narrow-body airliners.
/// Hard-setting it (rather than inheriting from the data feed, which usually
/// has no useful pitch on the ground) still serves its other purpose:
/// preventing pitch drift caused by inter-position interpolation in the slot
/// pipeline.
constexpr double GND_PITCH_DEG                  = 0.0;

/// [°] roll hard-set on every frame while on the ground. Real aircraft never
/// bank while taxiing — they pivot flat — and the existing roll-from-turn-rate
/// computation can produce micro-banks from heading jitter that look wrong on
/// a parked aircraft. We zero it explicitly; the in-air banking logic stays
/// gated behind `!IsOnGnd()` so this only applies on the ground.
constexpr double GND_ROLL_DEG                   = 0.0;

/// [°] alignment threshold deciding whether motion resuming after a pushback
/// pause is "forward" (push complete, exit) or "still being pushed" (tug
/// resumed, stay in pushback). Compared against `|track − pbHeldNose|` where
/// `pbHeldNose` is the last computed nose direction during the push.
///
/// Below this angle: the resumed motion is aligned with the held nose →
/// aircraft is taxiing forward under its own power → EXIT to PB_NONE.
/// Above this angle: the resumed motion still points backwards relative
/// to the nose → the tug is continuing the push → re-enter PB_ACTIVE.
///
/// 90° splits the half-planes cleanly: anything moving forward of the
/// aircraft's beam is taxi, anything moving aft is push.
constexpr double PB_EXIT_FORWARD_DIFF_DEG       = 90.0;

/// [s] duration over which the rendered altitude is blended from terrain
/// level up to the interpolated value at lift-off.
///
/// Why this exists: while an aircraft is on the ground LiveTraffic clamps
/// `ppos.alt_m` to the terrain (so a parked or taxiing aircraft is exactly
/// at runway/taxiway height, regardless of what the feed says). The
/// moment the flight-model decides the aircraft has lifted off
/// (`bOnGrnd` flips from true to false, `phase` becomes `FPH_LIFT_OFF`),
/// that clamp stops applying. The next-rendered altitude becomes the raw
/// linear interpolation between the last on-ground slot and the next
/// airborne slot — which can be hundreds of feet above the runway
/// depending on how far apart those slots are in time. Without smoothing
/// the aircraft visibly teleports upwards in a single frame ("jumps into
/// the air on rotation").
///
/// We instead lerp from terrain altitude to the interpolated altitude
/// over `LIFTOFF_BLEND_TIME_S` using a smoothstep easing curve
/// f(t) = t² (3−2t). Smoothstep is the right choice because it is C¹-
/// continuous at both endpoints:
///   - at t=0, f'(0)=0, so the rendered altitude leaves the ground with
///     a vertical speed of zero — no perceived velocity jump;
///   - at t=1, f'(1)=0, so the *derivative* of the rendered altitude
///     matches the derivative of the raw interpolation exactly there
///     (`result'(1) = smoothstep'(1)·(interp−terrain) + smoothstep(1)·
///     interp'(1) = interp'(1)`), meaning the climb-rate seam at the
///     end of the blend is invisible.
///
/// 10 s gives a visibly gradual lift-off that tracks the natural shape
/// of a real climb-out (rotate → wheels-up → gear-up → flap-retraction
/// span comparable seconds). A shorter blend (the original 1.5 s) made
/// the aircraft appear to leap from runway level to several hundred
/// feet within one airframe-length of forward travel; 10 s reads as
/// "climbing away from the runway" instead of "popping into the sky".
constexpr double LIFTOFF_BLEND_TIME_S           = 10.0;


//MARK: Flight Model
constexpr double MDL_ALT_MIN =         -1500;   // [ft] minimum allowed altitude
constexpr double MDL_ALT_MAX =          60000;  // [ft] maximum allowed altitude
constexpr double MDL_CLOSE_TO_GND =     0.5;    // feet height considered "on ground"
constexpr double MDL_TO_LOOK_AHEAD  =    60.0;  // [s] to look ahead for take off prediction
constexpr float  MDL_EXT_CAMERA_PITCH  = -5;    // initial pitch
constexpr float  MDL_EXT_STEP_MOVE =      0.5f; // [m] to move with one command
constexpr float  MDL_EXT_FAST_MOVE =      5.0f; //               ...a 'fast' command
constexpr float  MDL_EXT_STEP_DEG =       1.0f; // [°] step turn with one command
constexpr float  MDL_EXT_FAST_DEG =       5.0f;
constexpr float  MDL_EXT_STEP_FACTOR =    1.025f; // step factor with one zoom command
constexpr float  MDL_EXT_FAST_FACTOR =    1.1f;
#define MDL_LABEL_COLOR         "LABEL_COLOR"
constexpr double MDL_REVERSERS_TIME = 2.0;  ///< [s] to open/close reversers
constexpr double MDL_SPOILERS_TIME  = 0.5;  ///< [s] to extend/retract spoilers
constexpr double MDL_TIRE_SLOW_TIME = 5.0;  ///< [s] time till tires stop rotating after take-off
constexpr double MDL_TIRE_MAX_RPM = 2000;   ///< [rpm] max tire rotation speed
constexpr double MDL_TIRE_CF_M      = 3.2;  ///< [m] tire circumfence (3.2m for a 40-inch tire)
constexpr double MDL_GEAR_DEFL_TIME = 0.5;  ///< [s] time for gear deflection (one direction...up down is twice this value)
constexpr double MDL_CAR_MAX_TAXI = 80.0;   ///< [kn] Maximum allowed taxi speed for ground vehicles (before they turn into planes)
constexpr double MDL_GLIDER_STOP_ROLL=7.0;  ///< [°] a stopped glider is tilted to rest on one of its wings

constexpr int COLOR_YELLOW      = 0xFFFF00;
constexpr int COLOR_RED         = 0xFF0000;
constexpr int COLOR_GREEN       = 0x00FF00;
constexpr int COLOR_BLUE        = 0x00F0F0;     // light blue

//MARK: Airports, Runways, Taxiways
constexpr double ART_EDGE_ANGLE_TOLERANCE=30.0; ///< [°] tolerance of searched heading to edge's angle to be considered a fit
constexpr double ART_EDGE_ANGLE_TOLERANCE_EXT=80.0; ///< [°] extended (second prio) tolerance of searched heading to edge's angle to be considered a fit
constexpr double ART_EDGE_ANGLE_EXT_DIST=5.0;   ///< [m] Second prio angle tolerance wins, if such a node is this much closer than an first priority angle match
constexpr double ART_RWY_TD_POINT_F = 0.10;     ///< [-] Touch-down point is this much into actual runway (so we don't touch down at its actual beginning)
constexpr double ART_RWY_MAX_HEAD_DIFF = 15.0;  ///< [°] maximum heading difference between flight and runway
constexpr double ART_RWY_MAX_DIST = 20.0 * M_per_NM; ///< [m] maximum distance to a runway when searching for one
constexpr double ART_RWY_MAX_VSI_F = 0.5;       ///< [-] descend rate: factor applied to VSI_FINAL to calc max VSI (which, as we are sinking and value are negative, is the shallowest approach allowed)
constexpr double ART_RWY_ALIGN_DIST = 500.0;    ///< [m] distance before touch down to be fully aligned with rwy
constexpr double ART_APPR_SPEED_F = 0.8;        ///< [-] ratio of FLAPS_DOWN_SPEED to use as max approach speed
constexpr double ART_FINAL_SPEED_F = 0.7;       ///< [-] ratio of FLAPS_DOWN_SPEED to use as max final speed
constexpr double ART_TAXI_SPEED_F  = 0.8;       ///< [-] ratio of MAX_TAXI_SPEED to use as taxi speed
constexpr double APT_MAX_TAXI_SEGM_TURN = 15.0; ///< [°] Maximum turn angle (compared to original edge's angle) for combining edges
constexpr double APT_MAX_SIMILAR_NODE_DIST_M = 2.0; ///< [m] Max distance for two taxi nodes to be considered "similar", so that only one of them is kept
constexpr double APT_STARTUP_VIA_DIST = 50.0;   ///< [m] distance of StartupLoc::viaLoc from startup location
constexpr double APT_STARTUP_MOVE_BACK = 10.0;  ///< [m] move back startup location so that it sits about in plane's center instead of at its head
constexpr double APT_JOIN_MAX_DIST_M = 15.0;    ///< [m] Max distance for an open node to be joined with another edge
constexpr double APT_JOIN_ANGLE_TOLERANCE=15.0; ///< [°] tolerance of angle for an open node to be joined with another edge
constexpr double APT_JOIN_ANGLE_TOLERANCE_EXT=45.0; ///< [°] extended (second prio) tolerance of angle for an open node to be joined with another edge
constexpr double APT_MAX_PATH_TURN=100.0;       ///< [°] Maximum turn allowed during shortest path calculation
constexpr double APT_PATH_MIN_SEGM_LEN=SIMILAR_POS_DIST*2;      ///< [m] Minimum segment length when taking over a shortest path. Shorter taxi segments are joined into one to avoid too many positions in the fd deque
constexpr double APT_RECT_ANGLE_TOLERANCE=10.0; ///< [°] Tolerance when trying to decide for rectangular angle

//MARK: Version Information
extern char LT_VERSION[];               // like "1.0"
extern char LT_VERSION_FULL[];          // like "1.0.181231" with last digits being build date
extern char HTTP_USER_AGENT[];          // like "LiveTraffic/1.0"
extern time_t LT_BETA_VER_LIMIT;        // BETA versions are limited
extern char LT_BETA_VER_LIMIT_TXT[];
#define BETA_LIMITED_VERSION    "BETA limited to %s"
#define BETA_LIMITED_EXPIRED    "BETA-Version limited to %s has EXPIRED -> SHUTTING DOWN! Get an up-to-date version from X-Plane.org."
constexpr int LT_NEW_VER_CHECK_TIME = 24;   ///< [h] between two checks for a new LT version

//MARK: Text Constants
#define LIVE_TRAFFIC            "LiveTraffic"
#define LIVE_TRAFFIC_XPMP2      "   LT"      ///< short form for logging by XPMP2, so that log entries are aligned
#define LT_FM_VERSION           "4.5.0"      ///< expected version of flight model file format
#define PLUGIN_SIGNATURE        "TwinFan.plugin.LiveTraffic"
#define PLUGIN_DESCRIPTION      "Create Multiplayer Aircraft based on live traffic."
constexpr const char* REMOTE_SIGNATURE      =  "TwinFan.plugin.XPMP2.Remote";
#define LT_DOWNLOAD_URL         "https://forums.x-plane.org/files/file/49749-livetraffic/"
#define OPSKY_EDIT_AC           "https://opensky-network.org/data/aircraft?icao24="
// Disabled until OpenSky offers the service again to maintain routes - #define OPSKY_EDIT_ROUTE        "https://opensky-network.org/add-route?callsign="
#define MSG_DISABLED            "Disabled"
#define MSG_STARTUP             "LiveTraffic %s starting up..."
#define MSG_WELCOME             "LiveTraffic %s successfully loaded!"
#define MSG_REINIT              "LiveTraffic is re-initializing itself"
#define MSG_DISABLE_MYSELF      "LiveTraffic disables itself due to unhandable exceptions"
#define MSG_LT_NEW_VER_AVAIL    "The new version %s of LiveTraffic is available at X-Plane.org!"
#define MSG_LT_UPDATED          "LiveTraffic has been updated to version %s"
#define MSG_TIMESTAMPS          "Current System time is %sZ, current simulated time is %s"
#define MSG_AI_LOAD_ACF         "Changing AI control: X-Plane is now loading AI Aircraft models..."
#define MSG_REQUESTING_LIVE_FD  "Requesting live flight data online..."
#define MSG_NUM_AC_INIT         "Initially created %d aircraft"
#define MSG_NUM_AC_ZERO         "No more aircraft displayed"
#define MSG_BUF_FILL_BEGIN      "Filling buffer: seeing "
#define MSG_BUF_FILL_COUNTDOWN  MSG_BUF_FILL_BEGIN "%d aircraft, displaying %d, still %ds to buffer"
#define MSG_REPOSITION_WND      "Resize and reposition message window to your liking."
#define MSG_REPOSITION_LN2      "Also see the effect of changing Font Scale and Opacity in the settings.\nWhen done click:"
#define MSG_FMOD_SOUND          "Audio Engine: FMOD Core API by Firelight Technologies Pty Ltd."
#define INFO_WEATHER_UPDATED    "Weather updated: QNH %.f hPa at %s (%.2f / %.2f)"
#define INFO_AC_ADDED           "Added aircraft %s, operator '%s', a/c model '%s', flight model [%s], bearing %.0f, distance %.1fnm, from channel %s"
#define INFO_AC_MDL_CHANGED     "Changed CSL model for aircraft %s, operator '%s': a/c model now '%s' (Flight model '%s')"
#define INFO_GND_VEHICLE_APT    "Vehicle %s: Decided for ground vehicle based on operator name '%s'"
#define INFO_GND_VEHICLE_CALL   "Vehicle %s: Decided for ground vehicle based on call sign '%s'"
#define INFO_AC_REMOVED         "Removed aircraft %s"
#define INFO_AC_ALL_REMOVED     "Removed all aircraft"
#define INFO_REQU_AI_RELEASE    "%s requested us to release TCAS / AI control. Switch off '" MENU_HAVE_TCAS "' if you want so."
#define INFO_REQU_AI_REMOTE     "XPMP2 Remote Client requested us to release TCAS / AI control, so we do."
#define INFO_GOT_AI_CONTROL     LIVE_TRAFFIC " has TCAS / AI control now"
#define INFO_RETRY_GET_AI       "Another plugin released AI control, will try again to get control..."
#define INFO_AC_HIDDEN          "A/c %s hidden"
#define INFO_AC_HIDDEN_AUTO     "A/c %s automatically hidden"
#define INFO_AC_SHOWN           "A/c %s visible"
#define INFO_AC_SHOWN_AUTO      "A/c %s automatically visible"
#define MSG_TOO_MANY_AC         "Reached limit of %d aircraft, will render nearest aircraft only."
#define MSG_CSL_PACKAGE_LOADED  "Successfully loaded CSL package %s"
#define MSG_MDL_FORCED          "Settings > Debug: Model matching forced to '%s'/'%s'/'%s'"
#define MSG_MDL_NOT_FORCED      "Settings > Debug: Model matching no longer forced"
#define WHITESPACE              " \t\f\v\r\n"
#define CSL_DEFAULT_ICAO_TYPE   "A320"
#define CSL_CAR_ICAO_TYPE       "ZZZC"      // fake code for a ground vehicle
#define STATIC_OBJECT_TYPE      "TWR"       ///< code often used for statuc objects
#define FM_MAP_SECTION          "Map"
#define FM_CAR_SECTION          "GroundVehicles"
#define FM_PARENT_SEPARATOR     ":"
#define CFG_CSL_SECTION         "[CSLPaths]"
#define CFG_FLARM_ACTY_SECTION  "[FlarmAcTypes]"
#define CFG_WNDPOS_MSG          "MessageWndPos"
#define CFG_WNDPOS_SUI          "SettingsWndPos"
#define CFG_WNDPOS_ACI          "ACInfoWndPos"
#define CFG_WNDPOS_ILW          "InfoListWndPos"
#define CFG_DEFAULT_AC_TYPE     "DEFAULT_AC_TYPE"
#define CFG_DEFAULT_CAR_TYPE    "DEFAULT_CAR_TYPE"
#define CFG_DEFAULT_AC_TYP_INFO "Default a/c type is '%s'"
#define CFG_DEFAULT_CAR_TYP_INFO "Default car type is '%s'"
#define CFG_SOUND_DEVICE        "Sound_Device"
#define CFG_SND_NO_DEVICE       "(no change)"
#define CFG_OPENSKY_CLIENT      "OpenSky_Client"
#define CFG_OPENSKY_SECRET      "OpenSky_Secret"
#define CFG_NVGR_REFRESH_TOKEN  "Navigraph_RefreshToken"
#define CFG_ADSBEX_API_KEY      "ADSBEX_API_KEY"
#define CFG_RT_LICENSE          "RealTraffic_License"
#define CFG_FSC_USER            "FSC_User"
#define CFG_FSC_PWD             "FSC_Pwd"
#define CFG_SI_DISPLAYNAME      "SI_DisplayName"

//MARK: Menu Items
#define MENU_INFO_LIST_WND      "Status / Information..."
#define MENU_AC_INFO_WND        "Aircraft Info..."
#define MENU_AC_INFO_WND_POPOUT "Aircraft Info... (Popped out)"
#define MENU_AC_INFO_WND_SHOWN  "Aircraft Info shown"
#define MENU_AC_INFO_WND_CLOSEALL "Close All Windows"
#define MENU_TOGGLE_AIRCRAFT    "Aircraft displayed"
#define MENU_TOGGLE_AC_NUM      "Aircraft displayed (%d shown)"
#define MENU_HAVE_TCAS          "TCAS controlled"
#define MENU_HAVE_TCAS_REQUSTD  "TCAS controlled (requested)"
#define MENU_TOGGLE_LABELS      "Labels shown"
#define MENU_TOGGLE_AC_AHEAD    "Hide Aircraft ahead"
#define MENU_SETTINGS_UI        "Settings..."
#define MENU_HELP               "Help"
#define MENU_HELP_DOCUMENTATION "Documentation"
#define MENU_HELP_FAQ           "FAQ"
#define MENU_HELP_MENU_ITEMS    "Menu Items"
#define MENU_HELP_INFO_LIST_WND "Status / Info Window"
#define MENU_HELP_AC_INFO_WND   "A/C Info Window"
#define MENU_HELP_SETTINGS      "Settings"
#define MENU_HELP_INSTALL_CSL   "Installaton of CSL Models"
#define MENU_HELP_SUPPORT_FORUM "Support Forum"
#define MENU_HELP_SUPPORT_HOWTO "Support HowTo"
#define MENU_HELP_DOWNLOAD      "Download LiveTraffic"
#define MENU_NEWVER             "New Version %s available!"
#ifdef DEBUG
#define MENU_RELOAD_PLUGINS     "Reload all Plugins (Caution!)"
#define MENU_REMOVE_ALL_BUT     "Remove all but selected a/c"
#endif

//MARK: Help URLs
#define HELP_URL                "https://twinfan.gitbook.io/livetraffic/"
#define HELP_FAQ                "reference/faq"
#define HELP_MENU_ITEMS         "using-lt/menu-items"
#define HELP_ILW                "using-lt/info-list-window"
#define HELP_ILW_AC_LIST        "using-lt/info-list-window/aircraft-list"
#define HELP_ILW_MESSAGES       "using-lt/info-list-window/messages"
#define HELP_ILW_STATUS         "using-lt/info-list-window/status-about"
#define HELP_ILW_SETTINGS       "using-lt/info-list-window/ui-settings"
#define HELP_AC_INFO_WND        "using-lt/aircraft-information-window"
#define HELP_INSTALL_CSL        "setup/installation/step-by-step#csl-model-installation"
#define HELP_SETTINGS           "setup/configuration#settings-ui"
#define HELP_SET_BASICS         "setup/configuration/settings-basics"
#define HELP_SET_INPUT_CH       "introduction/features/channels"
#define HELP_SET_CH_OPENSKY     "setup/installation/opensky"
#define HELP_SET_CH_ADSBHUB     "setup/installation/adsbhub"
#define HELP_SET_CH_ADSBEX      "setup/installation/ads-b-exchange"
#define HELP_SET_CH_AIRPLANES   "setup/installation/airplanes.live"
#define HELP_SET_CH_ADSBFI      "setup/installation/adsb.fi"
#define HELP_SET_CH_OPENGLIDER  "setup/installation/ogn"
#define HELP_SET_CH_REALTRAFFIC "setup/installation/realtraffic-connectivity"
#define HELP_SET_CH_NAVIGRAPH   "setup/installation/navigraph"
#define HELP_SET_CH_FSCHARTER   "setup/installation/fscharter"
#define HELP_SET_CH_SI          "setup/installation/sayintentions"
#define HELP_SET_CH_AUTOATC     "setup/installation/autoatc"
#define HELP_SET_OUTPUT_CH      "setup/installation/foreflight"     // currently the same as ForeFlight, which is the only output channel
#define HELP_SET_CH_FOREFLIGHT  "setup/installation/foreflight"
#define HELP_SET_ACLABELS       "setup/configuration/settings-a-c-labels"
#define HELP_SET_WEATHER        "setup/configuration/settings-weather"
#define HELP_SET_ADVANCED       "setup/configuration/settings-advanced"
#define HELP_SET_CSL            "setup/configuration/settings-csl"
#define HELP_SET_DEBUG          "setup/configuration/settings-debug"

#define URL_SUPPORT_FORUM       "https://forums.x-plane.org/forums/forum/457-livetraffic-support/"
#define URL_SUPPORT_HOWTO       "https://forums.x-plane.org/forums/topic/174691-support-attach-a-logtxt-file-and-provide-the-following-details/"

//MARK: File Paths
// these are under the plugins directory
#define PATH_FLIGHT_MODELS      "Resources/FlightModels.prf"
#define PATH_DOC8643_TXT        "Resources/Doc8643.txt"
#define PATH_MODEL_TYPECODE_TXT "Resources/model_typecode.txt"
#define PATH_RESOURCES          "Resources"
#define PATH_RESOURCES_CSL      "Resources/CSL"
#define PATH_RESOURCES_SCSL     "Resources/ShippedCSL"
// these are under X-Plane's root dir
#define PATH_DEBUG_RAW_FD       "LTRawFD.log"
#define PATH_DEBUG_EXPORT_FD    "Output/LTExportFD - %Y-%m-%d %H.%M.%S.csv"
#define PATH_RES_PLUGINS        "Resources/plugins"
#define PATH_CONFIG_FILE        "Output/preferences/LiveTraffic.prf"
// Standard path delimiter
constexpr const char* PATH_DELIMS = "/\\";      ///< potential path delimiters in all OS
#if IBM
#define PATH_DELIM '\\'                         ///< Windows path delimiter
#else
#define PATH_DELIM '/'                          ///< MacOS/Linux path delimiter
#endif

//MARK: Error Texsts
constexpr long HTTP_OK =            200;
constexpr long HTTP_MOVED =         302;        ///< redirect
constexpr long HTTP_BAD_REQUEST =   400;
constexpr long HTTP_UNAUTHORIZED =  401;
constexpr long HTTP_PAYMENT_REQU =  402;
constexpr long HTTP_FORBIDDEN =     403;
constexpr long HTTP_NOT_FOUND =     404;
constexpr long HTTP_METH_NOT_ALLWD =405;
constexpr long HTTP_TOO_MANY_REQU = 429;        ///< too many requests, e.g. OpenSky after request limit ran out
constexpr long HTTP_INTERNAL_ERR =  500;
constexpr long HTTP_BAD_GATEWAY =   502;        // typical cloudflare responses: Bad Gateway
constexpr long HTTP_NOT_AVAIL =     503;        //                               Service not available
constexpr long HTTP_GATEWAY_TIMEOUT=504;        //                               Gateway Timeout
constexpr long HTTP_TIMEOUT =       524;        //                               Connection Timeout
constexpr long HTTP_NO_JSON =       601;        ///< private definition: cannot be parsed as JSON
constexpr long HTTP_FLAG_SENDING =   -1;        ///< used only internal to logging: sending data
constexpr long HTTP_FLAG_UDP =       -2;        ///< used only internal to logging: received UDP data
constexpr int CH_MAC_ERR_CNT =      5;          // max number of tolerated errors, afterwards invalid channel
constexpr int SERR_LEN = 100;                   // size of buffer for IO error texts (strerror_s)
#define ERR_XPLANE_ONLY         "LiveTraffic works in X-Plane only, version 10 or higher"
#define ERR_INIT_XPMP           "Could not initialize XPMP2: %s"
#define ERR_LOAD_CSL            "Could not load CSL Package: %s"
#define ERR_XPMP_ADD_CSL        "Could not add additional CSL package from '%s': %s"
#define ERR_APPEND_MENU_ITEM    "Could not append a menu item"
#define ERR_CREATE_MENU         "Could not create menu %s"
#define ERR_CURL_INIT           "Could not initialize CURL: %s"
#define ERR_CURL_EASY_INIT      "Could not initialize easy CURL"
#define ERR_CURL_PERFORM        "%s: Could not get network data: %d - %s"
#define ERR_CURL_NOVERCHECK     "Could not browse X-Plane.org for version info: %d - %s"
#define ERR_CURL_HTTP_RESP      "%s: HTTP response is not OK but %ld for %s"
#define ERR_CURL_REVOKE_MSG     {"revocation","80092012","80092013"}  // appear in error text if querying revocation list fails
#define ERR_CURL_DISABLE_REV_QU "%s: Querying revocation list failed - have set CURLSSLOPT_NO_REVOKE and am trying again"
#define ERR_HTTP_NOT_OK         "HTTP response was not HTTP_OK"
#define ERR_FOUND_NO_VER_INFO   "Found no version info in response"
#define ERR_CH_INACTIVE1        "There are inactive (stopped) channels."
#define ERR_CH_NONE_ACTIVE1     "No channel for tracking data enabled!"
#define ERR_CH_NONE_ACTIVE      ERR_CH_NONE_ACTIVE1 " Check Basic Settings and enable channels."
#define ERR_CH_UNKNOWN_NAME     "(unknown channel)"
#define INFO_CH_RESTART         "%s: Channel restarted"
#define ERR_CH_INVALID          "%s: Channel invalid"
#define ERR_CH_MAX_ERR_INV      "%s: Channel invalid after too many errors"
#define ERR_NO_AC_TYPE          "Tracking data for '%s' (man '%s', mdl '%s') lacks ICAO a/c type code, can't derive type -> will be rendered with standard a/c %s"
#define ERR_NO_AC_TYPE_BUT_MDL  "Tracking data for '%s' (man '%s', mdl '%s') lacks ICAO a/c type code, but derived %s from mdl text"
#define ERR_SHARED_DATAREF      "Could not created shared dataRef for livetraffic/camera/..., 3rd party camera plugins will not be able to take over camera view automatically"
#define ERR_DATAREF_FIND        "Could not find DataRef/CmdRef: %s"
#define ERR_DATAREF_ACCESSOR    "Could not register accessor for DataRef: %s"
#define ERR_CREATE_COMMAND      "Could not create command %s"
#define ERR_DIR_CONTENT         "Could not retrieve directory content for %s"
#define ERR_JSON_PARSE          "Parsing flight data as JSON failed"
#define ERR_JSON_MAIN_OBJECT    "JSON: Getting main object failed"
#define ERR_JSON_ACLIST         "JSON: List of aircraft (%s) not found"
#define ERR_JSON_AC             "JSON: Could not get %lu. aircraft in '%s'"
#define ERR_NEW_OBJECT          "Could not create new object (memory?): %s"
#define ERR_LOCK_ERROR          "Could not acquire lock for '%s': %s"
#define ERR_MALLOC              "Could not (re)allocate %ld bytes of memory"
#define ERR_ASSERT              "ASSERT FAILED: %s"
#define ERR_AC_NO_POS           "No positional data available when creating aircraft %s"
#define ERR_AC_CALC_PPOS        "Could not calculate position when creating aircraft %s"
#define ERR_Y_PROBE             "Y Probe returned %d at %s"
#define ERR_POS_UNNORMAL        "A/c %s reached invalid pos: %s"
#define ERR_IGNORE_POS          "A/c %s: Ignoring data leading to sharp turn or invalid speed: %s"
#define ERR_INV_TRANP_ICAO      "Ignoring data for invalid transponder code '%s'"
#define ERR_TIME_NONLINEAR      "Time moved non-linear/jumped by %.1f seconds, will re-init aircraft."
#define ERR_TOP_LEVEL_EXCEPTION "Caught top-level exception! %s"
#define ERR_EXCEPTION_AC_CREATE "Exception occured while creating a/c %s of type %s: %s\nPosDeque before was:\n%s"
#define ERR_UNKN_EXCP_AC_CREATE "Unknown " ERR_EXCEPTION_AC_CREATE
#define ERR_CFG_FILE_OPEN_OUT   "Could not create config file '%s': %s"
#define ERR_CFG_FILE_WRITE      "Could not write into config file '%s': %s"
#define ERR_CFG_FILE_OPEN_IN    "Could not open '%s': %s"
#define ERR_CFG_FILE_VER        "Config file '%s' first line: Unsupported format or version: '%s'"
#define ERR_CFG_FILE_VER_UNEXP  "Config file '%s' first line: Unexpected version %s, expected %s...trying to continue"
#define ERR_CFG_FILE_IGNORE     "Ignoring unkown entry '%s' from config file '%s'"
#define ERR_CFG_FILE_WORDS      "Expected two words (key, value) separated by a space in config file '%s', line '%s': ignored"
#define ERR_CFG_FILE_READ       "Could not read from '%s': %s"
#define ERR_CFG_LINE_READ       "Could not read from file '%s', line %d: %s"
#define ERR_CFG_FILE_TOOMANY    "Too many warnings"
#define ERR_CFG_FILE_VALUE      "%s: Could not convert '%s' to a number: %s"
#define ERR_CFG_FORMAT          "Format mismatch in '%s', line %d: %s"
#define ERR_CFG_VAL_INVALID     "Value invalid in '%s', line %d: %s"
#define ERR_CFG_CSL_INVALID     "CSL Path config invalid in '%s': '%s'"
#define ERR_CFG_CSL_DISABLED    "CSL Path '%s' disabled, skipping"
#define ERR_CFG_CSL_EMPTY       "CSL Path '%s' does not exist or is empty, skipping"
#define ERR_CFG_CSL_NONE        "No valid CSL Paths configured, verify Settings > CSL!"
#define ERR_CFG_CSL_ZERO_MODELS "No CSL Model has been (successfully) loaded, LiveTraffic cannot activate!"
#define ERR_CFG_CSL_ONLY_CAR    "Only the follow-me car has been (successfully) loaded as CSL model. LiveTraffic can only draw cars!"
#define ERR_CFG_CSL_ONLY_ONE    "Only one CSL model has been (successfully) loaded. LiveTraffic can only draw %s (%s)!"
#define MSG_CFG_CSL_INSTALL     "For help see menu: Plugins > LiveTraffic > Help > " MENU_HELP_INSTALL_CSL
#define ERR_CFG_AC_DEFAULT      "A/c default ICAO type '%s' invalid, still using '%s' as default. Verify Settings > CSL!"
#define ERR_CFG_CAR_DEFAULT     "Car default ICAO type '%s' invalid, still using '%s' as default. Verify Settings > CSL!"
#define ERR_CFG_TYPE_INVALID    "%s, line %d: ICAO type designator '%s' unknown"
#define ERR_FM_NOT_AFTER_MAP    "Unknown section after [Map] section ignored"
#define ERR_FM_NOT_BEFORE_SEC   "Lines before first section ignored"
#define ERR_FM_UNKNOWN_NAME     "Unknown parameter in '%s', line %d: %s"
#define ERR_FM_UNKNOWN_SECTION  "Referring to unknown model section in '%s', line %d: %s"
#define ERR_FM_UNKNOWN_PARENT   "Parent section missing in '%s', line %d: %s"
#define ERR_FM_REGEX            "%s in '%s', line %d: %s"
#define ERR_FM_NOT_FOUND        "Found no flight model for ICAO %s/match-string %s: will use default"
#define ERR_TCP_LISTENACCEPT    "%s: Error opening the TCP port on %s:%s: %s"
#define ERR_SOCK_SEND_FAILED    "%s: Could not send position: send operation failed"
#define ERR_UDP_SOCKET_CREAT    "%s: Error creating UDP socket for %s: %s"
#define ERR_UDP_RCVR_RCVR       "%s: Error receiving UDP: %s"
constexpr int ERR_CFG_FILE_MAXWARN = 10;     // maximum number of warnings while reading config file, then: dead

//MARK: Debug Texts
#define DBG_MENU_CREATED        "Menu created"
#define DBG_WND_CREATED_UNTIL   "Created window, display until total running time %.2f, for text: %s"
#define DBG_WND_DESTROYED       "Window destroyed"
#define DBG_LT_MAIN_INIT        "LTMainInit initialized"
#define DBG_LT_MAIN_ENABLE      "LTMainEnable enabled"
#define DBG_MAP_DUP_INSERT      "Duplicate insert into LTAircraftMap with key %s"
#define DBG_SENDING_HTTP        "%s: Sending HTTP: %s"
#define DBG_RECEIVED_BYTES      "%s: Received %ld characters"
#define DBG_RAW_FD_START        "DEBUG Starting to log raw flight data to %s"
#define DBG_RAW_FD_STOP         "DEBUG Stopped logging raw flight data to %s"
#define DBG_EXPORT_FD_START     "Starting to export tracking data to %s"
#define DBG_EXPORT_FD_STOP      "Stopped exporting tracking data to %s"
#define DBG_RAW_FD_ERR_OPEN_OUT "DEBUG Could not open output file %s: %s"
#define DBG_FILTER_AC           "DEBUG Filtering for a/c '%s'"
#define DBG_FILTER_AC_REMOVED   "DEBUG Filtering for a/c REMOVED"
#define DBG_POS_DATA            "DEBUG POS DATA: %s"
#define DBG_KEEP_ABOVE          "DEBUG POS LIFTED TO 2.5deg GLIDESCOPE from %.0fft: %s"
#define DBG_NO_MORE_POS_DATA    "DEBUG NO MORE LIVE POS DATA: %s"
#define DBG_SKIP_NEW_POS_TS     "DEBUG SKIPPED NEW POS (ts too close): %s"
#define DBG_SKIP_NEW_POS_NOK    "DEBUG SKIPPED NEW POS (not OK next pos): %s"
#define DBG_ADDED_NEW_POS       "DEBUG ADDED   NEW POS: %s"
#define DBG_REMOVED_NOK_POS     "DEBUG REMOVED NOK POS: %s"
#define DBG_REMOVED_CLOSE_POS   "DEBUG REMOVED TOO-CLOSE STRAIGHT POS: %s"
#define DBG_INVENTED_STOP_POS   "DEBUG INVENTED STOP POS: %s"
#define DBG_INVENTED_TD_POS     "DEBUG INVENTED TOUCH-DOWN POS: %s"
#define DBG_REUSING_TD_POS      "DEBUG RE-USED TOUCH-DOWN POS: %s"
#define DBG_INVENTED_TO_POS     "DEBUG INVENTED TAKE-OFF POS: %s"
#define DBG_REUSING_TO_POS      "DEBUG RE-USED POS FOR TAKE-OFF: %s"
#define DBG_HOVER_POS_REMOVED   "DEBUG %s: Removed a hovering position: %s"
#define DBG_AC_SWITCH_POS       "DEBUG A/C SWITCH POS: %s"
#define DBG_AC_FLIGHT_PHASE     "DEBUG A/C FLIGHT PHASE CHANGED from %i %s to %i %s"
#define DBG_AC_CHANNEL_SWITCH   "DEBUG %s: SWITCHED CHANNEL from '%s' to '%s'"
#ifdef DEBUG
#define DBG_DEBUG_BUILD         "DEBUG BUILD with additional run-time checks and no optimizations"
#endif

#endif /* Constants_h */
