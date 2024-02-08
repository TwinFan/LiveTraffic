#!/usr/bin/env python3

# RTAPI_example.py v1.2 7/11/2023 balt@inside.net
# Illustrates data retrieval and broadcasting via UDP

import requests
import json
import time
import sys
import signal
import psutil
from datetime import datetime
from argparse import ArgumentParser
import threading
from socket import SO_REUSEADDR, SOCK_STREAM, socket, SOL_SOCKET, AF_INET, SOCK_DGRAM, IPPROTO_UDP, SO_BROADCAST

def sighandler(signum, frame):
  if signum == signal.SIGINT:
    print("Ctrl-C captured. Exiting.")
    sys.exit(0)

def UDPbcast(ip, bcast, port, data):
  sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)  # UDP
  sock.setsockopt(SOL_SOCKET, SO_BROADCAST, 1)
  sock.bind((ip,0))
  sock.sendto(data, (bcast, port))
  sock.close()

if __name__ == '__main__':

  parser = ArgumentParser(description='RealTraffic API Demo')

  # Add optional argument, with given default values if user gives no arg
  parser.add_argument('-p', '--port', default=10747, type=int, help='Server port')
  parser.add_argument('-s', '--host', default="127.0.0.1", type=str, help='Server host')
  parser.add_argument('-l', '--license', required=True, help='Your RealTraffic license, e.g. AABBCC-1234-AABBCC-123456')
  args = parser.parse_args()

  license = args.license

  # application specific settings
  software = "RTAPI_demo"

  # API constants
  auth_url = "https://rtw.flyrealtraffic.com/v3/auth"
  traffic_url = "https://rtw.flyrealtraffic.com/v3/traffic"
  weather_url = "https://rtw.flyrealtraffic.com/v3/weather"
  header = { "Accept-encoding": "gzip" }
  license_types = { 1: "Standard", 2: "Professional" }
  special_keys = [ "full_count", "dataepoch", "source", "rrl", "status" ]

  # capture signals such as ctrl-c in the loop
  signal.signal(signal.SIGINT, sighandler)

  ###############################################################
  # enumerate all network interfaces and get their IPs
  # broadcasting to 255.255.255.255 is bad practice, need to find the correct bcast addr for
  # the local subnet on each interface only
  bcast_addrs = []
  ip_addrs = []
  ifs = psutil.net_if_addrs()
  for key in ifs.keys():
    for intf in ifs[key]:
      if intf.broadcast != None:
        bcast_addrs.append(intf.broadcast)
        ip_addrs.append(intf.address)

  print("Will broadcast to:", bcast_addrs)

  ###############################################################
  # authenticate
  payload = { "license": "%s" % license, "software": "%s" % software }
  json_data = requests.post(auth_url, payload, headers=header).json()
  if json_data["status"] != 200:
    print(json_data["message"])
    sys.exit(0)

  ###############################################################
  # retrieve our GUID to use for data access as well as the license details
  GUID = json_data["GUID"]
  license_type = json_data["type"]
  expiry = datetime.fromtimestamp(json_data["expiry"])
  sleep_seconds = int(json_data["rrl"]) / 1000

  print("Successfully authenticated. %s license valid until %s UTC" % (license_types[license_type], expiry.strftime("%Y-%m-%d %H:%M:%S")))
  print("Sleeping %ds to avoid request rate violation..." % sleep_seconds)
  time.sleep(sleep_seconds)

  # set up the payload for the traffic queries
  # Example area: Heidiland
  traffic_payload = { "GUID": "%s" % GUID,
             "querytype": "locationtraffic",
             "top": 52.4,                   # Around EDLE
             "bottom": 50.4,
             "left": 5.9,
             "right": 7.9,
             "toffset": 0 }

  weather_payload = { "GUID": "%s" % GUID,
             "querytype": "locwx",
             "lat": 51.4069,                # EDLE
             "lon": 6.9391,
             "alt": 0,
             "airports": "UNKN",            # Don't need airport METAR
             "toffset": 0 }

  ###############################################################
  # keep fetching traffic forever (or until ctrl-c is pressed)
  while True:
    # fetch weather

    response = requests.post(weather_url, weather_payload, headers=header)
    print(response.text)
    try:
      json_data = response.json()
    except Exception as e:
      print(e)
      print(response.text)
      # something borked. abort.
      raise

    if json_data["status"] != 200:
      print(json_data["message"])
      time.sleep(2)
      continue

    print(json_data["data"])

    # fetch traffic
    response = requests.post(traffic_url, traffic_payload, headers=header)
    try:
      json_data = response.json()
    except Exception as e:
      print(e)
      print(response.text)
      # something borked. abort.
      raise

    if json_data["status"] != 200:
      print(json_data["message"])
      time.sleep(2)
      continue

    flights = []
    for key in json_data:
      if key not in special_keys:
        for ip, bcast in zip(ip_addrs, bcast_addrs):
          UDPbcast(ip, bcast, 49005, str.encode(json.dumps(json_data[key])))
        # and extract a few data points for show and tell
        flights.append("%08s %08s %04s %05s %03s %04s %04s" % (json_data[key][13], json_data[key][16], json_data[key][8], json_data[key][4], json_data[key][5], json_data[key][11], json_data[key][12]))

    print("------------------------------------------------------")
    print("Current time: %s UTC" % datetime.utcnow().strftime("%Y-%m-%d %H:%M:%S"))
    print("Traffic time: %s UTC" % datetime.utcfromtimestamp(json_data["dataepoch"]))
    print("Traffic source: %s" % json_data["source"])
    print("Request rate limit: %sms" % json_data["rrl"])
    print("Total flights in the system: %s" % json_data["full_count"])
    print("")
    print("Callsign   Flight Type   Alt Gsp Orig Dest")
    for f in sorted(flights):
      print(f)

    sleep_seconds = int(json_data["rrl"]) / 1000
    print("Sleeping %d seconds..." % sleep_seconds)

    time.sleep(sleep_seconds)



