#!/usr/bin/python3

"""
Sends air traffic tracking data via UDP similar to how RealTraffic sends data,
so that LiveTraffic can receive canned data via the RealTraffic channel.

For usage info call
    python3 SendTraffic.py -h


MIT License

Copyright (c) 2022 B.Hoppe

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
"""

import os
import sys
import socket
import time
import argparse                     # handling command line arguments

_tsDiff = 0

# User's position as received from UDP 49002:
# latitude, longitude, altitude above MSL in meters, track in degress, ground speed in meters per second
class position(object):
    __slots__ = ['lat', 'lon', 'alt_m', 'track', 'gndSpd_m_per_s']
    def __init__(self):
        self.lat = float('NaN')
        self.lon = float('NaN')
        self.alt_m = float('NaN')
        self.track = float('NaN')
        self.gndSpd_m_per_s = float('NaN')

    def alt_ft(self) -> float:                      # convert altitude to feet
        return self.alt_m * 3.28084

    def gndSpd_kn(self) -> float:                   # convert speed to knots
        return self.gndSpd_m_per_s * 1.943844

    def printPos(self):
        print("User's position: %7.6f/%7.6f, altitude = %1.0f ft, track = %3.0f deg, gnd speed = %2.1f kn" \
            %(self.lat, self.lon, self.alt_ft(), self.track, self.gndSpd_kn()))

    def updateFrom(self,ln: str):                   # update values by reading them from an XGPS line as broadcasted on port 49002
        fields = ln.split(',')
        if len(fields) >= 3:                        # at least lat/lon
            self.lat = float(fields[1])
            self.lon = float(fields[2])
        if len(fields) >= 6:                        # also the others
            self.alt_m = float(fields[3])
            self.track = float(fields[4])
            self.gndSpd_m_per_s = float(fields[5])

# one global object with the user's position
userPos = position()

""" === Compute and wait for timestamp """
def compWaitTS(ts_s: str) -> str:
    global _tsDiff

    # current time and convert timestamp
    now = int(time.time())
    ts = float(ts_s)

    # First time called? -> compute initial timestamp difference
    if not _tsDiff:
        _tsDiff = now - ts - args.bufPeriod
        if args.verbose:
            print ("Timestamp difference: {}".format(_tsDiff))

    # What's the required timestamp to wait for and then return?
    ts += _tsDiff

    # if that's in the future then wait
    if (ts > now):
        if args.verbose:
            print ("Waiting for {} seconds...".format(ts-now), end='\r')
        time.sleep (ts-now)

    # Adjust returned timestamp value for historic timestamp
    ts -= args.historic

    return str(ts)

""" === Handle traffic data ==="""
def sendTrafficData(ln: str, doSend: int) -> int:
    # split into its fields
    fields = ln.split(',')

    # should have found at least 15 fields,
    # the first 15 are by and large very similar between AITFC and RTTFC,
    # #1 is the a/c id and #14 is the timestamp in both cases
    if len(fields) < 15:
        print ("Found {} fields, expected at least 15, in line {}".format(len(fields), ln))
        return 0

    # Test if a selected aircraft
    if not _ac or int(fields[1]) in _ac:
        # Update and wait for timestamp
        fields[14] = compWaitTS(fields[14])

        # Send the data
        if doSend:
            datagram = ','.join(fields)
            sock.sendto(datagram.encode('ascii'), (args.host, args.port))
            if args.verbose:
                print (datagram)
    return 1

""" === Handle weather data ==="""
def sendWeatherData(ln: str) -> int:
    sock.sendto(ln.encode('ascii'), (args.host, args.weatherPort))
    if args.verbose:
        print ("Weather: {}".format(ln))
    return 1

""" === Open listening port for user's position === """
def openUserPosPort():
    global sockPos
    sockPos = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sockPos.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    if sys.platform != "win32":
	    sockPos.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
    sockPos.setblocking(False)
    sockPos.bind(('', 49002))           # 49002 is the standard port for ForeFlight GPS data

""" === Read available position data from UDP === """
def updateUserPos():
    try:
        while 1:                        # read all available data from the UDP buffer
            ln = sockPos.recv(4096).decode()
            if ln.startswith('XGPS'):   # is this actually a user's position record?
                userPos.updateFrom(ln)
                if args.printUserPos:
                    userPos.printPos()  # output for debugging purposes

    except:
        return
    return

""" === MAIN === """

# --- Handling command line argumens ---
parser = argparse.ArgumentParser(description='SendTraffic 1.1.0: Sends air traffic tracking data from a file out on a UDP port for LiveTraffic to receive it on the RealTraffic channel. '
    'In LiveTraffic, activate the "RealTraffic" channel to receive the data and have it displayed as moving planes. '
    'From LiveTraffic, you can also export tracking data in a matching format using the Debug options "Export Tracking Data" and/or "Export User Aircraft". '
    'The latter allows you to fly yourself and have your aircraft\'s movements written as tracking data. '
    'Data is written to \'Output/LTExportFD - <timestamp>.csv\'.',fromfile_prefix_chars='@')
parser.add_argument('inFile', help='Tracking data file: records in CSV format holding air traffic data in RealTraffic\'s AITraffic format and weather data.\n<stdin> by default', nargs='?', type=argparse.FileType('r'), default=sys.stdin)
parser.add_argument('-a', '--aircraft', metavar='HEX_LIST', help='List of aircraft to read and send, others skipped. Add one or several transponder hex id codes, separate by comma.')
parser.add_argument('-d', '--aircraftDecimal', metavar='NUM_LIST', help='Same as -a, but specify decimal values (as used in the CSV file).')
parser.add_argument('-b', '--bufPeriod', metavar='NUM', help='Buffering period: Number of seconds the first record is pushed into the past so that LiveTraffic\'s buffer fills more quickly. '
    'Recommended to be slightly less than _half of_ LiveTraffic\'s buffering period. (More than half of buf period triggers historic data processing.)', type=int, default=0)
parser.add_argument('--historic', metavar='NUM', help='Send historic data, ie. reduce included timestamp by this many seconds', type=int, default=0)
parser.add_argument('-l', '--loop', help='Endless loop: restart from the beginning when reaching end of file. Will work best if data contains loop with last position(s) being roughly equal to first position(s).', action='store_true')
parser.add_argument('--host', metavar='NAME_OR_IP', help='UDP target host or ip to send the data to, defaults to \'localhost\'', default='localhost')
parser.add_argument('--port', metavar='NUM', help='UDP port to send traffic data to, defaults to 49005', type=int, default=49005)
parser.add_argument('--weatherPort', metavar='NUM', help='UDP port to send weather data to, defaults to 49004', type=int, default=49004)
parser.add_argument('-v', '--verbose', help='Verbose output: Informs of each sent record', action='store_true')
parser.add_argument('-u', '--getUserPos', help='Developer demo: Get user aircraft\'s position by reading from UDP port 49002; activate ForeFlight output in LiveTraffic, or "Broadcast to all third-party apps" in X-Plane\'s Network settings.', action='store_true')
parser.add_argument('-up', '--printUserPos', help='Requires -u, then it prints each received user aircraft position to stdout.', action='store_true')

args = parser.parse_args()

# --- list of selected aircraft ---
_ac=[]

# convert hex ids to decimal numbers and add them to _ac
if args.aircraft is not None:
    _ac += [int(h,16) for h in args.aircraft.split(',')]

# add the decimal-defined a/c, too
if args.aircraftDecimal is not None:
    _ac += [int(n,0) for n in args.aircraftDecimal.split(',')]

# print list
if _ac and args.verbose:
    print ("Selected aircraft: {}".format(_ac))

# --- open the UDP socket ---
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
if args.getUserPos:
    openUserPosPort()

# Outer loop helps with endless looping
_sendLn = 1
while 1:
    _tsDiff = 0
    # --- open and loop the input file ---
    for line in args.inFile:
        # Update user's position if available
        if args.getUserPos:
            updateUserPos()

        # remove any whitespace at both ends
        line = line.strip()

        # Can be traffic or weather data
        if line.startswith('AITFC') or line.startswith('RTTFC') :
            sendTrafficData(line, _sendLn)
            _sendLn = 1                 # send all following lines
        else:
            sendWeatherData(line)

    # Endless loop?
    if (not args.loop): break           # no, end replay
    _sendLn = 0                         # don't send first position again
    args.bufPeriod = 0                  # no buffering as we keep sending continuously
    args.inFile.seek(0, os.SEEK_SET)    # restart file from beginning

# --- Cleanup ---
args.inFile.close
sock.close
if args.getUserPos:
    sockPos.close