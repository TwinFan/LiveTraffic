#!/usr/bin/python3

"""
Sends air traffic tracking data via UDP similar to how RealTraffic sends data,
so that LiveTraffic can receive canned data via the RealTraffic channel.

For usage info call
    python3 SendTraffic.py -h


MIT License

Copyright (c) 2020 B.Hoppe

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

import sys
import socket
import time
import argparse                     # handling command line arguments

_tsDiff = 0

""" === Compute and wait for timestamp """
def compWaitTS(ts_s: str) -> str:
    global _tsDiff

    # current time and convert timestamp
    now = int(time.time())
    ts = int(ts_s)

    # First time called? -> compute initial timestamp difference
    if not _tsDiff:
        _tsDiff = now - ts - args.bufPeriod
        if args.verbose:
            print (f"Timestamp difference: {_tsDiff}")

    # What's the required timestamp to wait for and then return?
    ts += _tsDiff

    # if that's in the future then wait
    if (ts > now):
        if args.verbose:
            print (f"Waiting for {ts-now} seconds...", end='\r')
        time.sleep (ts-now)

    return str(ts)

""" === Handle traffic data ==="""
def sendTrafficData(ln: str) -> int:
    # split into its fields
    fields = ln.split(',')

    # should have found 16 fields!
    if len(fields) != 15:
        print (f"Found {len(fields)} fields, expected 16, in line {ln}")
        return 0

    # Test if a selected aircraft
    if not _ac or int(fields[1]) in _ac:
        # Update and wait for timestamp
        fields[14] = compWaitTS(fields[14])

        # Send the data
        datagram = ','.join(fields)
        sock.sendto(datagram.encode('ascii'), (args.host, args.port))

        if args.verbose:
            print (datagram)
    return 1

""" === Handle weather data ==="""
def sendWeatherData(ln: str) -> int:
    if args.verbose:
        print (line)
    return 1

""" === MAIN === """

# --- Handling command line argumens ---
parser = argparse.ArgumentParser(description='SendTraffic 0.1.0: Sends air traffic tracking data from a file out on a UDP port for LiveTraffic to receive it on the RealTraffic channel',fromfile_prefix_chars='@')
parser.add_argument('inFile', help='Tracking data file: records in CSV format holding air traffic data in RealTraffic\'s AITraffic format and weather data.\n<stdin> by default', nargs='?', type=argparse.FileType('r'), default=sys.stdin)
parser.add_argument('-a', '--aircraft', metavar='HEX_LIST', help='List of aircraft to read, others skipped. Add one or several transponder hex id codes, separate by comma.')
parser.add_argument('-d', '--aircraftDecimal', metavar='NUM_LIST', help='Same as -a, but specify decimal values (as used in the CSV file).')
parser.add_argument('-b', '--bufPeriod', metavar='NUM', help='Buffering period: Number of seconds the first record is pushed into the past so that LiveTraffic\'s buffer fills more quickly. Recommended to be slightly less than LiveTraffic\'s buffering period.', type=int, default=0)
parser.add_argument('--host', metavar='NAME_OR_IP', help='UDP target host or ip to send the data to, defaults to \'localhost\'', default='localhost')
parser.add_argument('--port', metavar='NUM', help='UDP port to send traffic data to, defaults to 49003', type=int, default=49003)
parser.add_argument('--weatherPort', metavar='NUM', help='UDP port to send weather data to, defaults to 49004', type=int, default=49004)
parser.add_argument('-v', '--verbose', help='Verbose output: Informs of each sent record', action='store_true')

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
    print (f"Selected aircraft: {_ac}")

# --- open the UDP socket ---
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

# --- open and loop the input file ---
_ts = 0
for line in args.inFile:
    # remove any whitespace at both ends
    line = line.strip()

    # Can be traffic or weather data
    if line.startswith('AITFC'):
        sendTrafficData(line)
    else:
        sendWeatherData(line)

# --- Cleanup ---
args.inFile.close;
sock.close;
