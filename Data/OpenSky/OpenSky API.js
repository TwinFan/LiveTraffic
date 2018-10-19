/*
 * General Remarks:
 * Data types for fields:
 *   - serial(s): integer
 *   - day: Date,
 *   - begin, end: Date
 *   - icao24: String
 * If not specified, time values have to be given as Date
 */

function getCookie(name) {
    var value = "; " + document.cookie;
    var parts = value.split("; " + name + "=");
    if (parts.length == 2) return parts.pop().split(";").shift();
}

jQuery.ajaxPrefilter(function (options, originalOptions, jqXHR) {
    var m = options['type'].toLowerCase()
    if (m === "post" || m === "put" || m === "delete" || m === "options") {
        jqXHR.setRequestHeader('X-XSRF-TOKEN',  getCookie("XSRF-TOKEN"));
    }
});

/**
 * @classdesc OpenSky REST API - JavaScript Reference Implementation
 * @class module:com_opensky~OpenSkyApi
 * @author Markus Fuchs [fuchs@sero-systems.de]
 */
var OpenSkyApi = (function() {
    /*******************************************************************************
     * Init Object
     ******************************************************************************/
            // without trailing '/', i.e. root is ''
    baseUri = "//"+window.location.host+"/api";

    debug = false;

    /*******************************************************************************
     * Public Functions
     ******************************************************************************/
    /**
     * This datastructure saves the rangedata for one sensor: <br> <br>
     *{ <br>
     *     serial, <br>
     *     area (in m²), <br>
     *     minDistance: [angle, distance (in meters)], <br>
     *     maxDistance: [angle, distance (in meters)], <br>
     *     sensorPosition: [latitude, longitude], <br>
     *     ranges: [[angle, latitude, longitude]] <br>
     * } <br>
     * @typedef {Object} module:com_opensky~DATASensorRange
     */

    /**
     * This datastructure saves all
     * {@link module:com_opensky~DATASensorRange|DATASensorRange}
     * for all sensors for one day in an array: <br> <br>
     * [{@link module:com_opensky~DATASensorRange|DATASensorRange}] <br>
     * @typedef {Array} module:com_opensky~DATARangesForDay
     */

    /**
     * An associative array whose key is the day and the values are collections of
     * SensorRanges for all sensor that have collected position messages at the
     * given day. The key is an integer that encodes the date in the format
     * yyyyMMdd, e.g.  20150930 for Sep 30 2015. There is at most one SensorRange
     * for each sensor serial number at the given day. If there is no sensor range
     * for a day in between dayBegin and dayEnd it is simply left out. <br>
     * <br>
     * {day: {@link module:com_opensky~DATARangesForDay|DATARangesForDay}} <br>
     * @typedef {Object} module:com_opensky~DATARangesForDays
     */

    /**
     * Retrieve sensor ranges for an interval of several days for the whole
     * network. Filtering by certain sensors is optional.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Date} begin
     * results will include ranges for [dayBegin, dayEnd], inculding those dates!
     * It is expected as Date()
     *
     * @param {Date} end
     * results will include ranges for [dayBegin, dayEnd], inculding those dates!
     * It is expected as Date()
     *
     * @param {ArrayOfString} serials
     * an array of sensor serial numbers to retrieve the ranges for.  If the array
     * is empty or null, ranges for all possible sensors are retrieved. (optional)
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATARangesForDays}
     *
     */
    var getRangesInterval = function(callback, begin, end, serials,
            onfail) {
        console.warn("Deprecation warning: getRangesInterval(callback, begin, end, serials, onfail) is deprecated and should not be used anymore!");
        var params = {
            begin: _date2Day(begin),
            end: _date2Day(end)
        };
        if (serials != null) {
            params.serials = serials;
        }
        _getData("range/interval", params, callback, onfail);
    };

    /**
     * Retrieve sensor ranges for several days for the whole network. Filtering
     * by certain sensors is optional.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {ArrayOfDate} days
     * an arrays of several days to retrieve the ranges for. The days must be of
     * type Date.
     *
     * @param {ArrayOfString} serials
     * an array of sensor serial numbers to retrieve the ranges for. Must
     * include at least one sensor serial number.
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATARangesForDays}
     */
    var getRangesDays = function(callback, days, serials, onfail) {
        for (var i = 0; i < days.length; i++) {
            days[i] = _date2Day(days[i]);
        }
        var params = {
            days: days,
            serials: serials
        };
        _getData("range/days", params, callback, onfail);
    };

    /**
     * This datastructure provides the network's coverage <br> <br>
     * [ <br>
     *  [ <br>
     *    latitude, <br>
     *    longitude, <br>
     *    min altitude (in meters) <br>
     *  ] <br>
     * ]
     * @typedef {Object} module:com_opensky~DATACoverage
     */

    /**
     * Retrieve the network's coverage data for a given day.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Date} day
     * A day to retrieve data for. The days must be of type Date. If no day is
     * given, the most recent network coverage is served.
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATACoverage}
     */
    var getCoverage = function(callback, day, onfail) {
        params = {};
        if (day != null) {
            params.day = (day.getTime() / 1000).toFixed(0);
            params.day = params.day - params.day % 86400;
        }
        _getData("range/coverage", params, callback, onfail);
    };

    /**
     * Retrieve days where network coverage is available
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {ArrayOfString} an array of unix time stamps
     */
    var getCoverageDays = function(callback, day, onfail) {
        _getData("range/coverageDays", {}, callback, onfail);
    };
    /**
     * { <br>
     *     time (for which data is returned), <br>
     *     states: <br>
     *         [ <br>
     *            [ <br>
     *             icao24,              //0 <br>
     *             callsign,            //1 <br>
     *             originCountry,       //2 <br>
     *             lastPositionUpdate,  //3 <br>
     *             lastVelocityUpdate,  //4 <br>
     *             longitude,           //5 <br>
     *             latitude,            //6 <br>
     *             altitude,            //7 <br>
     *             isOnGround,          //8 <br>
     *             velocity,            //9 <br>
     *             heading,             //10 <br>
     *             verticalRate,        //11 <br>
     *             [sensors],           //12 <br>
     *             geometric_altitude (number), //13 <br>
     *             squawk (string),             //14 <br>
     *             alert (boolean),             //15 <br>
     *             spi (boolean),               //16 <br>
     *             position_source (integer 0: adsb, 1: asterix, 2: mlat)  //17 <br>
     *            ] <br>
     *         ] <br>
     * } <br>
     * @typedef {Object} module:com_opensky~DATAFlightMapStates
     */

    /**
     * Retrieve state vectors at a given time. If there are no states at the
     * given time, the FlightMapData's list of states is null or empty.
     * The states are ordered by callsign in descending order. The list of sensors
     * is only present for most recent state vectors _or_ if the filter for serial
     * has been supplied. Otherwise it is null.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Date} time
     * time to retrieve state vectors for as Date()
     *
     * @param {string} icao24
     * an array of icao24 IDs to filter for. Pass an empty array or null if you
     * don't want to filter for particular aircraft.
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @param {String} serial
     * sensor serial number for which states should be retrieved (optional)
     *
     * @param {Boolean} extended
     * Boolean,  if the extended stats are supposed to be retrieved. The extended stats hold, for example, the category
     * description.
     *
     * @returns {module:com_opensky~DATAFlightMapStates}
     */
    var getFlightMapStates = function(callback, time, icao24, onfail, serial, extended) {
        extended = typeof extended === 'undefined' ? true : extended;
        var params = {};
        params.extended = extended;
        if (time != null) {
            params.time = (time.getTime() / 1000).toFixed(0);
        }
        if (icao24 != null)
            params.icao24 = icao24;
        if (serial != null)
            params.serial = serial;

        _getData("states/all", params, callback, onfail);
    };

   /**
     * Same as getFlightMapStates() but filters for own sensors (all if you are admin)
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Date} time
     * time to retrieve state vectors for as Date()
     *
     * @param {string} icao24
     * an array of icao24 IDs to filter for. Pass an empty array or null if you
     * don't want to filter for particular aircraft.
     *
     * @param {ArrayOfString} serials
     * an array of sensor serial numbers to retrieve states for. The result will
     * only contain aircraft that have been seen by one of the given sensors at
     * that time. It is only a sub set of the sensors you own (except for admins).
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @param {Boolean} extended
     * Boolean,  if the extended stats are supposed to be retrieved. The extended stats hold, for example, the category
     * description.
     *
     * @returns {module:com_opensky~DATAFlightMapStates}
     */
    var getMyStates = function(callback, time, icao24, serials,
            onfail, extended) {
        extended = typeof extended === 'undefined' ? true : extended;
        var params = {};
        if (time != null) {
            params.time = (time.getTime() / 1000).toFixed(0);
            params.extended = extended;
        }
        if (icao24 != null)
            params.icao24 = icao24;
        if (serials != null)
            params.serials = serials;
        _getData("states/own", params, callback, onfail);
    };

    /**
     *{ <br>
     *   latest (latest time included in the result set), <br>
     *   earliest (time there is data to retrieve), <br>
     *   series: { <br>
     *      &lt;serial&gt;: [[time, messageRate (msgs/s)]] <br>
     *   } <br>
     * } <br>
     * @typedef {Object} module:com_opensky~DATAMessageStats
     */

    /**
     * Get message stats for all sensors or a specific one within a time interval
     * [begin, end].
     * If both begin and end are missing, the last hour of message rates is
     * retrieved
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Date} begin
     * start of time interval to retrieve message rates for as Date(). If it is
     * missing it is set to one hour before the function call.
     *
     * @param {Date} end
     * end of time interval to retrieve message rates for as Date(). If missing,
     * it is set to the current time.
     *
     * @param {ArrayOfString} serials
     * an array of sensor serial numbers to retrieve states for. The result will
     * only contain aircraft that have been seen by one of the given sensors at
     * that time. It is only a sub set of the sensors you own (except for admins).
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATAMessageStats}
     *
     */
    var getMessageStats = function(callback, begin, end,
            serials, onfail) {
        var params = {};
        if (begin != null) {
            params.begin = (begin.getTime() / 1000).toFixed(0);
        }
        if (end != null) {
            params.end = (end.getTime() / 1000).toFixed(0);
        }
        if (serials != null) {
            params.serials = serials;
        }
        _getData("stats/msgRates", params, callback, onfail);
    };

    /**
     *{ <br>
     *   latest (latest time included in the result set), <br>
     *   earliest (time there is data to retrieve), <br>
     *   series: { <br>
     *      &lt;serial&gt;: [[time, #aircraft]] <br>
     *   } <br>
     * } <br>
     * @typedef {Object} module:com_opensky~DATAAircraftCounts
     */

    /**
     * Get aircraft counts per minute for all sensors or a specific one within a
     * time interval [begin, end].
     * If both begin and end are missing, the last hour of aircraft counts is
     * retrieved
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Date} begin
     * start of time interval to retrieve message rates for as Date(). If it is
     * missing it is set to one hour before the function call.
     *
     * @param {Date} end
     * end of time interval to retrieve message rates for as Date(). If missing,
     * it is set to the current time.
     *
     * @param {ArrayOfString} serials
     * an array of sensor serial numbers to retrieve states for. The result will
     * only contain aircraft that have been seen by one of the given sensors at
     * that time. It is only a sub set of the sensors you own (except for admins).
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATAAircraftCounts} Time is always the beginning of a minute.
     */
    var getAircraftCounts = function(callback, begin, end,
            serials, onfail) {
        var params = {};
        if (begin != null) {
            params.begin = (begin.getTime() / 1000).toFixed(0);
        }
        if (end != null) {
            params.end = (end.getTime() / 1000).toFixed(0);
        }
        if (serials != null) {
            params.serials = serials;
        }
        _getData("stats/aircraftCounts", params, callback, onfail);
    }

    /**
     * Get various stats as visible in the OpenSky front page
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {boolean} extended
     * retrieve extended stats (default false)
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {Object}
     * an associative array of key-value pairs for stats
     */
     var getPublicStats = function(callback, extended, onfail) {
         var params = {};
         if (extended != null) {
             params.extended = extended;
         }
        _getData("stats/publicStats", params, callback, onfail);
    };


    /**
     * Get message count for sensor.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Number} serial
     * a serial number to retrieve stats for. If this parameter is null or '0',
     * accumulated stats for the whole network are retrieved.
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {Object}
     * an associative array of key-value pairs for stats. Keys are message type
     * labels, values are their counts.
     */
     var getMsgCounts = function(callback, serial, onfail) {
         var params = {};
         if (serial != null) {
             params.serial = serial;
         }
        _getData("stats/msgCounts", params, callback, onfail);
    };

    /**
     * Get message type counts.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Date} day
     * A day to retrieve data for. The days must be of type Date. If no day is
     * given, the most recent stats are returned
     *
     * @param {Number} serial
     * a serial number to retrieve stats for. If this parameter is null or '0',
     * accumulated stats for the whole network are retrieved.
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {Object}
     * an associative array of key-value pairs for stats. Keys are message type
     * labels, values are their counts.
     */
     var getMsgTypeStats = function(callback, day, serial, onfail) {
         var params = {};
         if (day != null) {
             params.day = (day.valueOf()/1000);
         }
         if (serial != null) {
             params.serial = serial;
         }
        _getData("stats/msgtypes", params, callback, onfail);
    };

    /**
     * An object containing data for one sensor <br>
     *{ <br>
     *              active : Boolean, <br>
     *              added: Date, <br>
     *              address: String, <br>
     *              anonymized: Boolean, <br>
     *              anonymousPosition: Boolean (can be null), <br>
     *              approved: Boolean, <br>
     *              checked: Boolean, <br>
     *              clientMode: Boolean,  <br>
     *              deleted: Boolean, <br>
     *              deletedDate: Date (can be null), <br>
     *              hostname: String, <br>
     *              id: Integer, <br>
     *              lastConnectionEvent: Timestamp, <br>
     *              notes: String, <br>
     *              online: Boolean, <br>
     *              operator: String, <br>
     *              port: Integer, <br>
     *              position: {}, <br>
     *              serial: Integer, <br>
     *              type: String, <br>
     *              uid: String <br>
     *              } <br>
     * @typedef {Array} module:com_opensky~DATASensor
     */

    /**
     * An array of sensor objects: <br> <br>
     * [ <br>
     *     {@link module:com_opensky~DATASensor|DATASensor}
     * ] <br>
     * @typedef {Array} module:com_opensky~DATASensors
     */

    /**
     * Get sensors with online/offline status. Based on the current user, sensor
     * positions/addresses/operators might be anonymized.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful. If callback is 'null' the function returns a jQuery.Deferred.
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATASensors} The array might also contain
     * un-approved and inactive sensors!
     */
     var getSensors = function(callback, onfail) {
        if (callback != null) {
            _getData("sensor/list", {}, callback, onfail);
            return null;
        } else {
            return _getDataDeferred("sensor/list", {});
        }
    };


    /**
     * Get stats for a single sensor for a selection of days.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Array} days Array of Date objects
     *
     * @param {String} serial Serial of sensor for which states are supposed to
     *  get retrieved
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {Object}
     */
    var getSensorStats = function(callback, days, serial, onfail) {
        //transform Date() objects into timestamp string
        for (var i = 0; i < days.length; i++) {
            days[i] = (days[i].getTime() / 1000).toFixed(0);
        }
        params = {days: days,
        serial: serial};
        _getData("sensor/stats", params, callback, onfail);
    };

    /**
     * [sum, max (per min), average (per min)]<br>
     * @typedef {Array} module:com_opensky~DATASensorDetailsMessageCount
     */

    /**
     * [sum, max (per min), average (per min)]<br>
     * @typedef {Array} module:com_opensky~DATASensorDetailsAircraftCount
     */

    /**
     * An array of sensor stats <br>
     * [ <br>
     *      {@link module:com_opensky~DATASensorDetailsMessageCount|DATASensorDetailsMessageCount}, <br>
     *      {@link module:com_opensky~DATASensorDetailsAircraftCount|DATASensorDetailsAircraftCount}, <br>
     *      availability,         (in percent) <br>
     *      [area, msg sum, msg average, availability, aircraft sum], (toplist position) <br>
     *      sensor serial, <br>
     *      user id <br>
     *   ]
     * @typedef {Array} module:com_opensky~DATASensorStats
     */

    /**
     * An array of sensor stats. Each entry of the most outer array contains the
     * stats for one day. The array is sorted by the days that have been asked for.
     * The earliest day is at the first position. <br>
     * [ <br>
     *     { <br>
     *         ranges : {@link module:com_opensky~DATARangesForDay|DATARangesForDay}<br>
     *         stats: { <br>
     *              &lt;serial&gt;: {@link module:com_opensky~DATASensorStats|DATASensorStats} <br>
     *          } <br>
     *     } <br>
     * ] <br>
     * @typedef {Array} module:com_opensky~DATAMySensorDetails
     */

    /**
     * Retrieve accumulated sensor statistics for a user's sensor for a  set of
     * days. If the user is an administrator, he will get all receive stats    for
     * all sensors. The set of sensors to filter is inferred from the
     * authentication credentials (i.e. cookie values or basic auth params)
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {ArrayOfDate} days
     * an arrays of several days to retrieve the stats for. The days must be of
     * type Date.
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATAMySensorDetails}
     *
     */
    var getMySensorDetails = function(callback, days, onfail) {
        console.warn("Deprecation warning: getMySensorDetails(callback, days, onfail) is deprecated and should not be used anymore!");
        for (var i = 0; i < days.length; i++) {
            days[i] = (days[i].getTime() / 1000).toFixed(0);
        }
        params = {days: days};
        _getData("sensor/myStats", params, callback, onfail);
    };


    /**
     * An array of sensor stats. Each entry of the most outer array contains the
     * stats for one day. The array is sorted by the days that have been asked for.
     * The earliest day is at the first position.<br>
     * [{ <br>
     *   time: int, <br>
     *   ranges: [], <br>
     *   stats: { <br>
     *              &lt;serial&gt;: {@link module:com_opensky~DATASensorStats|DATASensorStats} <br>
     *          } <br>
     * }] <br>
     * @typedef {Array} module:com_opensky~DATASensorDetails
     */

    /**
     * Retrieve accumulated sensor statistics for a for a  set of
     * days. Filtering by sensor serial is optional.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {ArrayOfDate} days
     * an arrays of several days to retrieve the stats for. The days must be of
     * type Date.
     *
     * @param {ArrayOfString} serials
     * an array of sensor serials to retrieve stats for. If null or empty, stats
     * for all sensors are retrieved.
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATASensorDetails}
     */
     var getSensorDetails = function(callback, days, serials, onfail) {
        console.warn("Deprecation warning: getSensorDetails(callback, days, serials, onfail) is deprecated and should not be used anymore!");
        for (var i = 0; i < days.length; i++) {
            days[i] = (days[i].getTime() / 1000).toFixed(0);
        }
        params = {days: days};
        if (serials != null) {
            params.serials = serials;
        }
        _getData("sensor/statsDaily", params, callback, onfail);
    };



    /**
     * Retrieve accumulated sensor statistics for a for a particular day
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Date} day
     * day to retrieve the stats for (as Date)
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATASensorStats}
     */
     var getDailyToplists = function(callback, day, onfail) {
        _getData("sensor/toplist", {date: _date2Day(day)}, callback, onfail);
     };

    /**
     * Retrieve accumulated sensor statistics for a for a particular month
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Date} month
     * month to retrieve the stats for (as Date)
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATASensorStats}
     */
     var getMonthlyToplists = function(callback, month, onfail) {
        _getData("sensor/toplist", {date: _date2Month(month)}, callback, onfail);
     };

    /**
     * Retrieve accumulated sensor statistics for a for a particular year
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {Date} day
     * year to retrieve the stats for (as Date)
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATASensorStats}
     */
     var getYearlyToplists = function(callback, year, onfail) {
        _getData("sensor/toplist", {date: _date2Year(year)}, callback, onfail);
     };


    /**
     * An object containing data for one sensor application <br>
     *{ <br>
     *              id: Long, <br>
     *              position: {<br>
     *                  longitude: double,<br>
     *                  latitude: double<br>
     *              },<br>
     *              location: {<br>
     *                  city: String,<br>
     *                  country: String<br>
     *              },<br>
     *              uid: String,<br>
     *              dhcp: Boolean,<br>
     *              networkConfig: { //null if dhcp==true<br>
     *                  ip4: String,<br>
     *                  subnetMask: String,<br>
     *                  defaultGw: String,<br>
     *                  dns: String<br>
     *              },<br>
     *              wifi: Boolean,<br>
     *              wifiSecurity: { //null if wifi = false<br>
     *                  ssid: String,<br>
     *                  type: int,<br>
     *                  psk: String,<br>
     *                  comment: String<br>
     *              },<br>
     *              shippingAddress: {<br>
     *                  address: String,<br>
     *                  city: String,<br>
     *                  zipCode: String,<br>
     *                  country: String,<br>
     *                  insititution: String,<br>
     *                  name: String,<br>
     *                  address2: String,<br>
     *                  state: String<br>
     *              },<br>
     *              sensorType: String,<br>
     *              donationGoal: Double,<br>
     *              currentAmount: Double,<br>
     *              state: String, //(PENDING, ACTIVE, COMPLETED)<br>
     *              lastStateUpdate: DateTime,<br>
     *              created: DateTime,<br>
     *              lastUpdate: DateTime,<br>
     *              comment: String, //max 500 chars<br>
     *              shipped: DateTime<br>
     *              } <br>
     * @typedef {object} module:com_opensky~DATASensorApplication
     */


    /**
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {Array} Array of module:com_opensky~DATASensorApplication
     */
    var getApplications = function(callback, onfail) {
        _getData("sensorDonation/list", {}, callback, onfail);
    };


    /**
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as
     * parameter if the request is successful
     *
     * @param {string} id
     * the id of the sensor application that is supposed to be retrieved
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {module:com_opensky~DATASensorApplication}
     */
    var getApplication = function(callback, id, onfail) {
        if (typeof id !== 'undefined' && id !== null) {
            _getData("sensorDonation/application/"+id, {}, callback, onfail);
        } else {
            console.log("Failed to load SponsoringApplication: No id provided!");
        }
    };


    /**
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {module:com_opensky~DATASensorApplication} data
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as
     * parameter if the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     */
    var newApplication = function(data, callback, onfail) {
        _createData("sensorDonation/application", data, callback, onfail);
    };


    /**
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {module:com_opensky~DATASensorApplication} data
     *
     * @param {int} id
     * the id of the application that is supposed to get updated
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as
     * parameter if the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     */
    var updateApplication = function(data, id, callback, onfail) {
        if (typeof id !== 'undefined' && id !== null) {
            _updateData("sensorDonation/application/"+id, data, callback, onfail);
        } else {
            console.log("Failed to update SponsoringApplication: No id provided!");
        }
    };


    /**
     * An object containing data for one custom donation. Custom donations are
     * added by admins to support a sensor application without using PayPal.<br>
     *{ <br>
     *          id: Long, <br>
     *          applicationId: Long,<br>
     *          amount: Double<br>
     *     } <br>
     * @typedef {object} module:com_opensky~DATADonation
     */

    /**
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {module:com_opensky~DATADonation} data
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as
     * parameter if the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     */
    var newCustomDonation = function(data, callback, onfail) {
        _createData("donation/addCustom", data, callback, onfail);
    };

    /**
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {int} id
     * the id of the application that is supposed to get deleted
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     */
    var deleteApplication = function(id, callback, onfail) {
        if (typeof id !== 'undefined' && id !== null) {
            _deleteData("sensorDonation/application/"+id, null, callback, onfail);
        } else {
            console.log("Failed to delete SponsoringApplication: No id provided!");
        }
    };

    /**
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object} TODO
     */
    var getSensorPrices = function(callback, onfail) {
        _getData("sensorPrice/list", {}, callback, onfail);
    };


    /**
     * Retrieve flight track for a live flight.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {ArrayOfString} icao
     * The icao of the plane for which the flight track should be retrieved
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
     var getTrack = function(callback, icao, onfail, time) {
         var params = {};
        if (icao != null) {
            params.icao24 = icao;
        }
        if (time != null) {
            params.time = time;
        }
        _getData("tracks/", params, callback, onfail);
    };

    /**
     * Retrieve all alerts.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object} with properties icao24, callsign, detected, ended,
     *                                   squawk
     */
    var getAlerts = function(callback, onfail) {
        _getData("alerts/all", {}, callback, onfail);
    };

    /**
     * Retrieve Sensor Metrics
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param interval {Number} interval in seconds (from now backwards in time) to retrieve values
     *                          for. Default is 1800 (30min)
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object} with properties callsign, path, position, acceleration, callsign
     *                   each containing an object with key-value pairs (sensor serial, value)
     */
    var getSensorMetrics = function(interval, callback, onfail) {
        var params = {};
        if (interval != null) params.interval = interval;

        if (callback != null) {
            _getData("bad-sensors/metrics", params, callback, onfail);
            return null;
        } else {
            return _getDataDeferred("bad-sensors/metrics", params);
        }
    };

    /**
     * Retrieve list of blocked aircraft in speed layer
     * @returns {Array} of object with properties serial and blockedUntil
     */
    var getDroplist = function(callback, onfail) {
        if (callback != null) {
            _getData("bad-sensors/drop", {}, callback, onfail);
            return null;
        } else {
            return _getDataDeferred("bad-sensors/drop", {});
        }
    };

    /**
     * Add receiver to list of blocked aircraft
     *
     * @param serial {Number} sensor serial number
     *
     * @param until {Date} Time until receiver should be blocked
     * @returns {null}
     */
    var addDroplist = function(serial, until, callback, onfail) {
        _createData("bad-sensors/drop", {serial: serial, until: (until.getTime() / 1000).toFixed(0)}, callback, onfail);
    };

    /**
     * Remove sensor from list of blocked receivers
     *
     * @param serial {Number} sensor serial number
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @returns {null}
     */
    var removeDroplist = function(serial, callback, onfail) {
        _deleteData("bad-sensors/drop", {serial: serial}, callback, onfail);
    };

    /**
     * Retrieve a list of aircraft.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {int} n
     * Number of results per page
     *
     * @param {int} p
     * The current page index
     *
     * @param {String} q
     * A search query
     *
     * @param {String} sc
     * Sorting column (e.g. "icao24")
     *
     * @param {String} sd
     * Sorting direction (ASC or DESC)
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getAircraft = function(n, p, q, sc, sd, callback, onfail) {
         var params = {};
        if (n != null) {
            params.n = n;
        }
        if (p != null) {
            params.p = p;
        }
        if (q != null) {
            params.q = q;
        }
        if (sc != null) {
            params.sc = sc;
        }
        if (sd != null) {
            params.sd = sd;
        }
        _getData("metadata/aircraft/list", params, callback, onfail);
    };

    /**
     * Retrieve a single aircraft by icao24.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getSingleAircraft = function(icao24, callback, onfail) {
         var params = {};
        if (icao24 != null) {
            _getData("metadata/aircraft/icao/"+icao24, null, callback, onfail);
        }
    };

    /**
     * Add metadata for a single aircraft.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {Object} data
     *
     * @param {function} callback
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     */
    var addSingleAircraft = function(data, callback, onfail) {
        if (data != null) {
            _createData("metadata/aircraft/user/", data, callback, onfail);
        }
    };


    /**
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {module:com_opensky~DATASensorApplication} data
     *
     * @param {int} uid
     * the uid of the user for which the manager should be updated
     *
     * @param {int} managerUid
     * the uid of the manager that should be assigned to the user
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as
     * parameter if the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     */
    var assignManager = function(uid, managerUid, callback, onfail) {
        if (uid && managerUid) {
            _updateData("user/m/"+uid, managerUid, callback, onfail);
        } else {
            console.log("Failed to update the user: No uid or managerUid provided!");
        }
    };

    /**
     * Add a user to one or multiple groups
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {String} uid
     *
     * @param {Array} groups An array of strings of group names
     *
     * @param {function} callback
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     */
    var addToGroup = function(uid, groups, callback, onfail) {
        if (uid && groups && groups.length) {
            if (callback != null) {
                _updateData("user/g/"+uid, groups, callback, onfail);
                return null;
            } else {
                return _updateDataDeferred("user/g/"+uid, groups);
            }
        }

    };

    /**
     * Delete a user from one or multiple groups
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {String} uid
     *
     * @param {Array} groups An array of strings of group names
     *
     * @param {function} callback
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     */
    var removeFromGroup = function(uid, groups, callback, onfail) {
        if (uid && groups && groups.length) {
            _deleteData("user/g/"+uid, groups, callback, onfail);
        }
    };

    /**
     * Retrieve the list of users
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getUsers = function(callback, onfail) {
        if (callback != null) {
            _getData("user/list/", null, callback, onfail);
            return null;
        } else {
            return _getDataDeferred("user/list/", {});
        }
    };

    /**
     * Retrieve aircraft database statistics.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getAircraftDatabaseStatistics = function(callback, onfail) {
        if (callback != null) {
            _getData("metadata/aircraft/stats", {}, callback, onfail);
            return null;
        } else {
            return _getDataDeferred("metadata/aircraft/stats", {});
        }
    };

    /**
     * Retrieve a list of airports in a specific are bounded by maximum latitude, minimum latitude, maximum longitude
     * and minimum longitude.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {double} lamin
     *
     * @param {double} lamax
     *
     * @param {double} lomin
     *
     * @param {double} lomax
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getAirportsRegion = function(lamin, lamax, lomin, lomax, type, callback, onfail) {
        var params = {};

        params["lamin"] = lamin;
        params["lamax"] = lamax;
        params["lomin"] = lomin;
        params["lomax"] = lomax;
        if (type) {
            params["type"] = type;
        }
        if (lamin != null) {
            _getData("airports/region", params, callback, onfail);
        }
    };

    var airportDataCache = {};

    /**
     * Retrieve data for a single airport by the icao of the airport.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {double} icao
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getSingleAirport = function(icao, callback, onfail, noCache) {
        if (
            !noCache
            && airportDataCache[icao]
            && airportDataCache[icao].data) {
            callback(airportDataCache[icao].data);
        }

        if (!airportDataCache[icao]) {
            airportDataCache[icao] = {callbacks: [], count: 0};
            airportDataCache[icao].callbacks.push(callback);
            airportDataCache[icao].count++;
        } else {
            airportDataCache[icao].callbacks.push(callback);
            airportDataCache[icao].count++;
            return;
        }


        if (icao != null) {
            _getData(
                "airports/?icao="+icao,
                null,
                function(data){
                    airportDataCache[icao].data = data;
                    airportDataCache[icao].callbacks.forEach( function (callback) {
                        callback(data);
                    });
                    airportDataCache[icao].callbacks = [];
                },
                onfail
            );
        } else {
            console.warn("api.js: icao null was passed to getSingleAirport()")
        }
    };

    /**
     * Retrieve flights for a time interval. If no flights are found for the given time period, HTTP stats
     * `404 - Not found` is returned with an empty response body.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {Date} begin
     *
     * @param {Date} end
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getFlightsInterval = function(begin, end, callback, onfail) {
        var params = {
            begin: Math.floor(begin/1000),
            end: Math.floor(end/1000)
        };
        _getData("flights/all", params, callback, onfail);
    };

    /**
     * Retrieve flights (arrivals and departures) for a single aircraft by the icao of the airport.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {double} icao
     *
     * @param {Date} begin
     *
     * @param {Date} end
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getFlightsAircraft = function(icao, begin, end, count, offset, callback, onfail) {
        if (icao != null) {
            var params = {
                icao24: icao,
                begin: Math.floor(begin/1000),
                end: Math.floor(end/1000),
                limit: count,
                offset: offset
            };
            _getData("flights/aircraft", params, callback, onfail);
        } else {
            console.warn("api.js: icao null was passed to getFlightsAircraft()")
        }
    };

    /**
     * Retrieve flights (only arrivals) for a single airport by the icao of the airport.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {double} icao
     *
     * @param {Date} begin
     *
     * @param {Date} end
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getFlightsAirportArrivalsInterval = function(icao, begin, end, callback, onfail) {
        if (icao != null) {
            var params = {
                airport: icao,
                begin: Math.floor(begin/1000),
                end: Math.floor(end/1000)
            };
            _getData("flights/arrival", params, callback, onfail);
        } else {
            console.warn("api.js: icao null was passed to getFlightsAirportArrivalsInterval()")
        }
    };

    /**
     * Retrieve flights (only departures) for a single airport by the icao of the airport.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {double} icao
     *
     * @param {Date} begin
     *
     * @param {Date} end
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getFlightsAirportDeparturesInterval = function(icao, begin, end, callback, onfail) {
        if (icao != null) {
            var params = {
                airport: icao,
                begin: Math.floor(begin/1000),
                end: Math.floor(end/1000)
            };
            _getData("flights/departure", params, callback, onfail);
        } else {
            console.warn("api.js: icao null was passed to getFlightsAirportDeparturesInterval()")
        }
    };

    /**
     * Retrieve flights (arrivals and departures) for a single airport by the icao of the airport.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {double} icao
     *
     * @param {Date} begin
     *
     * @param {Date} end
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getFlightsAirport = function(icao, begin, end, count, offset, callback, onfail) {
        if (icao != null) {
            var params = {
                airport: icao,
                begin: Math.floor(begin/1000),
                end: Math.floor(end/1000),
                limit: count,
                offset: offset
            };
            _getData("flights/airport", params, callback, onfail);
        } else {
            console.warn("api.js: icao null was passed to getFlightsAirport()")
        }
    };

    /**
     * Retrieve arrivals for a single airport by the icao of the airport.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {double} icao
     *
     * @param {Date} begin
     *
     * @param {Date} end
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getFlightsAirportArrival = function(icao, begin, end, count, offset, callback, onfail) {
        if (icao != null) {
            var params = {
                airport: icao,
                begin: Math.floor(begin/1000),
                end: Math.floor(end/1000),
                limit: count,
                offset: offset
            };
            _getData("flights/arrival", params, callback, onfail);
        } else {
            console.warn("api.js: icao null was passed to getFlightsAirport()")
        }
    };

    /**
     * Retrieve depatures for a single airport by the icao of the airport.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {double} icao
     *
     * @param {Date} begin
     *
     * @param {Date} end
     *
     * @param {function} callback
     * a callback function that is executed with the returned value as parameter if
     * the request is successful
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     *
     * @returns {object}
     */
    var getFlightsAirportDeparture = function(icao, begin, end, count, offset, callback, onfail) {
        if (icao != null) {
            var params = {
                airport: icao,
                begin: Math.floor(begin/1000),
                end: Math.floor(end/1000),
                limit: count,
                offset: offset
            };
            _getData("flights/departure", params, callback, onfail);
        } else {
            console.warn("api.js: icao null was passed to getFlightsAirport()")
        }
    };

    /**
     * Add a historical data access application.
     *
     * @memberof module:com_opensky~OpenSkyApi#
     *
     * @param {Object} data
     *
     * @param {function} callback
     *
     * @param {function} onfail
     * a function that is called with an error message as argument in case of a
     * failure. (optional)
     */
    var addHistoricalDataApplication = function(data, callback, onfail) {
        if (data != null) {
            _createData("forms/history", data, callback, onfail);
        }
    };

    /*******************************************************************************
     * Helper functions
     ******************************************************************************/
     var log = function(msg) {
        if (debug)
            console.log(msg);
    };

    var _getData = function(uri, attr, callback, onfail) {
        _getDataDeferred(uri, attr).fail(
            function(jqxhr, textStatus, errorThrown) {
                    if (onfail != null) {
                        onfail(errorThrown, jqxhr);
                    } else {
                        console.log("Failed to load API data: " + errorThrown);
                    }
                }
        ).done(
            function(data) {
                callback(data);
            }
        );
    };

    var _getDataDeferred = function(uri, attr) {
        log("Requesting " + baseUri + "/" + uri);
        log("Parameters: " + attr);
        jQuery.ajaxSettings.traditional = true;
        return jQuery.get(
            baseUri + "/" + uri,
            attr
        );
    };

    var _createData = function(uri, data, callback, onfail) {
        jQuery.ajax({
            type: "POST",
            url: baseUri + "/" + uri,
            processData: false,
            contentType: 'application/json',
            data: JSON.stringify(data),
            success: function(data, textStatus, jqXHR) {
                if (callback) {
                    callback(data);
                }
            }

        }).fail(function( jqXHR, textStatus, errorThrown ) {
            if (onfail) {
                onfail(errorThrown);
            } else {
                console.log("Failed to post data to API: " + errorThrown);
            }
        });

    };

    var _updateData = function(uri, data, callback, onfail) {
        _updateDataDeferred(uri, data).fail(
            function(jqxhr, textStatus, errorThrown) {
                    if (onfail != null) {
                        onfail(errorThrown);
                    } else {
                        console.log("Failed to update API data: " + errorThrown);
                    }
                }
        ).done(
            function(data) {
                callback(data);
            }
        );
    };

    var _updateDataDeferred = function(uri, data) {
        return jQuery.ajax({
            type: "PUT",
            url: baseUri + "/" + uri,
            processData: false,
            contentType: 'application/json',
            data: JSON.stringify(data)
        });
    };

    var _deleteData = function(uri, data, callback, onfail) {

        jQuery.ajax({
            type: "DELETE",
            url: baseUri + "/" + uri,
            processData: false,
            contentType: 'application/json',
            data: (data != null) ? JSON.stringify(data) : data,
            success: function(data, textStatus, jqXHR) {
                if (callback) {
                    callback(data);
                }
            }

        }).fail(function( jqXHR, textStatus, errorThrown ) {
            if (onfail) {
                onfail(errorThrown);
            } else {
                console.log("Failed to delete data on API: " + errorThrown);
            }
        });

    };

    var _date2Day = function(date) {
        return (date.getUTCFullYear() * 100 + (date.getUTCMonth() + 1)) * 100
                + date.getUTCDate();
    };
    var _date2Month = function(date) {
        return (date.getUTCFullYear() * 100 + (date.getUTCMonth() + 1));
    };
    var _date2Year = function(date) {
        return date.getUTCFullYear();
    };

     var _day2Date = function(day) {
        var s = String(day);
        var d = new Date();
        d.setUTCYear(s.substr(0, 4));
        d.setUTCMonth(Number(s.substr(4, 2)) - 1);
        d.setUTCDate(s.substr(6, 2));
        return d;
    };

    var _setBaseUri = function(newBaseUri) {
        baseUri = newBaseUri;
    };

     // 'public methods'
     return {
         baseUri: baseUri,
         day2Date: _day2Date,
         date2Day: _date2Day,
         getRangesInterval: getRangesInterval,
         getRangesDays: getRangesDays,
         getCoverage: getCoverage,
         getCoverageDays: getCoverageDays,
         getFlightMapStates: getFlightMapStates,
         getMyStates: getMyStates,
         getMessageStats: getMessageStats,
         getAircraftCounts: getAircraftCounts,
         getMsgCounts: getMsgCounts,
         getMsgTypeStats: getMsgTypeStats,
         getPublicStats: getPublicStats,
         getSensors: getSensors,
         getSensorStats: getSensorStats,
         getMySensorDetails: getMySensorDetails,
         getSensorDetails: getSensorDetails,
         getDailyToplists: getDailyToplists,
         getMonthlyToplists: getMonthlyToplists,
         getYearlyToplists: getYearlyToplists,
         setBaseUri: _setBaseUri,
         getApplications: getApplications,
         getApplication: getApplication,
         newApplication: newApplication,
         updateApplication: updateApplication,
         getSensorPrices: getSensorPrices,
         deleteApplication: deleteApplication,
         newCustomDonation: newCustomDonation,
         getTrack: getTrack,
         getAlerts: getAlerts,
         getAircraft: getAircraft,
         getSingleAircraft: getSingleAircraft,
         addSingleAircraft: addSingleAircraft,
         getSensorMetrics: getSensorMetrics,
         getDroplist: getDroplist,
         addDroplist: addDroplist,
         removeDroplist: removeDroplist,
         assignManager: assignManager,
         addToGroup: addToGroup,
         removeFromGroup: removeFromGroup,
         getUsers: getUsers,
         getAircraftDatabaseStatistics: getAircraftDatabaseStatistics,
         getAirportsRegion: getAirportsRegion,
         getSingleAirport: getSingleAirport,
         getFlightsInterval: getFlightsInterval,
         getFlightsAircraft: getFlightsAircraft,
         getFlightsAiportArrivalsInterval: getFlightsAirportArrivalsInterval,
         getFlightsAirportDeparturesInterval: getFlightsAirportDeparturesInterval,
         getFlightsAirport: getFlightsAirport,
         getFlightsAirportArrival: getFlightsAirportArrival,
         getFlightsAirportDeparture: getFlightsAirportDeparture,
         addHistoricalDataApplication: addHistoricalDataApplication
     };

})();

// deprecated object for backwards compatibility
var RestApi = function() {
    return OpenSkyApi;
};
