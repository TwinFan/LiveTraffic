/// @file       LTNavigraph.h
/// @brief      Navigraph/Flightradar24: Requests and processes live tracking data
/// @see        https://navigraph.com/blog/navigraph-flightradar24
/// @see        https://developers.navigraph.com/docs/authentication/device-authorization
/// @details    Implements NvgrFR24Connection:\n
///             - Handles the OAuth authentication protocol
///             - Provides a proper REST-conform URL\n
///             - Interprets the response and passes the tracking data on to LTFlightData.\n
/// @author     Birger Hoppe
/// @copyright  (c) 2026 Birger Hoppe
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

#ifndef LTNavigrpaph_h
#define LTNavigrpaph_h

#include "LTChannel.h"

// MARK: Navigraph Constants
#define NVGR_CHECK_NAME         "Flightradar24 Tracker"
#define NVGR_CHECK_URL          "https://www.flightradar24.com/%.3f,%.3f"
#define NVGR_CHECK_POPUP        "Check Flightradar's coverage"

// Request Device Authorization
// See https://developers.navigraph.com/docs/authentication/device-authorization
#define NVGR_AUTH_URL           "https://identity.api.navigraph.com/connect/deviceauthorization"
#define NVGR_AUTH_BODY          "client_id=%s&client_secret=%s&code_challenge=%s&code_challenge_method=S256"
// Device Authorization Reply
#define NVGR_AUTH_DEV_CODE      "device_code"
#define NVGR_AUTH_VERIFY_URI    "verification_uri_complete"
#define NVGR_AUTH_INTERVAL      "interval"
constexpr size_t NVGR_AUTH_INTERVAL_DEFAULT = 5;
// Poll Token
#define NVGR_TOKEN_URL          "https://identity.api.navigraph.com/connect/token"
#define NVGR_TOKEN_POLL_BODY    "grant_type=urn:ietf:params:oauth:grant-type:device_code&device_code=%s&code_verifier=%s&client_id=%s&client_secret=%s&scope=openid traffic:real:read offline_access"
// Token Response
#define NVGR_TOKEN_ACCESS       "access_token"
#define NVGR_TOKEN_EXPIRES      "expires_in"
#define NVGR_TOKEN_TYPE         "token_type"
#define NVGR_TOKEN_REFRESH      "refresh_token"
// Token Refresh
#define NVGR_TOKEN_REFRESH_BODY "grant_type=refresh_token&client_id=%s&client_secret=%s&refresh_token=%s"

#define NVGR_AUTH_HEADER        "Authorization: %s %s"     // add token_type, access_token
constexpr long NVGR_AUTH_EXP_DEFAULT = 3600;       ///< default expiration in case we don't find expiration field

#define NVGR_NAME               "Navigraph/Flightradar24"
#define NVGR_TRAFFIC_URL        "https://api.navigraph.com/v1/real-traffic/positions/nearby?lat=%.3f&lon=%.3f&radiusKm=%d"
#define NVGR_SLUG_FMT           "https://www.flightradar24.com/%06lx"

constexpr int NVGR_MIN_REFRESH_INTVL = 20;              ///< Navigraph imposes a minimum refresh interval of 20s

//
// MARK: Navigraph
//
class NvgrFR24Connection : public LTFlightDataChannel
{
protected:
    enum State {
        NVGR_STATE_NONE = 0,                    ///< no/initial/unknown status
        NVGR_STATE_GETTING_TOKEN,               ///< requesting current token
        NVGR_STATE_GET_PLANES,                  ///< normal operations: fetch planes
    } eState = NVGR_STATE_NONE;
    struct curl_slist* pHdrForm = nullptr;      ///< HTTP Header (needed during fetching a token)
    struct curl_slist* pHdrToken = nullptr;     ///< HTTP Header containing the bearer token
public:
    NvgrFR24Connection ();
    void ResetStatus ();                        ///< used to force fetching a new token, e.g. after change of credentials
    std::string GetURL (const positionTy& pos) override;
    void ComputeBody (const positionTy& pos) override;      ///< only needed for token request, will then form token request body
    bool ProcessFetchedData () override;
    std::string GetStatusText () const override;  ///< return a human-readable staus
//    // shall data of this channel be subject to LTFlightData::DataSmoothing?
//    bool DoDataSmoothing (double& gndRange, double& airbRange) const override
//    { gndRange = NVGR_SMOOTH_GROUND; airbRange = NVGR_SMOOTH_AIRBORNE; return true; }
    
    static bool IsBuiltIn();                    ///< Is Navigraph support built in, i.e. do we have a proper client secret/id?
    
protected:
    void Main () override;          ///< virtual thread main function

    bool InitCurl () override;
    /// Tries to interpret pBuf as JSON and looks for "error" or similar
    std::string TryExtractErrorMsg (const JSON_Object* pMain);
    
    // Device Authorization Process
public:
    /// Status of the device authentication process
    enum DevAuthState {
        NVGR_AUTH_NONE = 0,                     ///< No device authorization underway
        NVGR_AUTH_FETCHING,                     ///< Fetching codes from the authorization servers
        NVGR_AUTH_WAITING,                      ///< Waiting for authorization / polling the auth server
        NVGR_AUTH_ERROR,                        ///< Received an error, something needs to change -> GetStatusTxt
        NVGR_AUTH_TIMEOUT,                      ///< Authorization timed out...user needs to try again
        NVGR_AUTH_SUCCESS,                      ///< Successfully authorized, received token
        NVGR_AUTH_CANCEL,                       ///< indicates to the thread to stop immediately
    };
    /// What to show to the user just now?
    enum DevAuthUI {
        NVGR_AUTH_UI_NOTHING = 0,               ///< Don't show anything, maybe because not built in
        NVGR_AUTH_UI_AUTH,                      ///< Show the Authorize button (no Refresh Token yet)
        NVGR_AUTH_UI_REAUTH,                    ///< Show the Re-Authroize button (have a Refresh Token, but can always re-authorize)
        NVGR_AUTH_UI_WAIT,                      ///< Waiting for a server response
        NVGR_AUTH_UI_VERIFY_URI,                ///< Show the verificatio URI, asking the user to perform the authorization
        NVGR_AUTH_UI_DONE,                      ///< Process is done, for result see AuthState
    };
protected:
    static DevAuthState eAuthState;             ///< Current state of Device Authorization
    static DevAuthUI eAuthUI;                   ///< What to show to the user?
    static std::string sErrMsg;                 ///< last error message, empty if OK
    static std::string sAuthVerifyURI;          ///< Verification URI, to be passed on to the user
    static std::thread thrAuth;                 ///< the authroization communication thread
    static std::string tokenAccess;             ///< the temporary access token
    ///< when will the token expire? (XP network time)
    static std::chrono::time_point<std::chrono::steady_clock> tTokenExpiration;

public:
    static void AuthInit();                     ///< Some initialization at startup time
    static bool AuthStartProcess ();            ///< Triggers the process (if not NVGR_AUTH_FETCHING/WAITING)
    static void AuthCancelProcess ();           ///< If process is underway, cancel it and wait for it to end

    static DevAuthState AuthGetState ();        ///< Get state of auth process
    static DevAuthUI AuthGetUI ();              ///< What to show the user just now?
    static std::string AuthGetVerifyURI ();     ///< Return the verification URI, if the authorization process received and needs one
protected:
    ///< Sets the new state, lock-conrolled, and save: only overwrite eOld with eNew
    static bool AuthSetState (DevAuthState eOld, DevAuthState eNew);
    static void AuthMain ();                    ///< Thread main function running the auth process
};

/// Enabled this module
bool NavigraphStart ();

/// Stop this module, makes sure the auth thread shuts down
void NavigraphStop ();

#endif /* LTNavigrpaph_h */
