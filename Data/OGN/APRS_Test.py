#!/usr/bin/python3

import sys
import socket

if len(sys.argv) < 4:
    print ("3 parameters required: lat lon dist[km]")
    exit()

lat = sys.argv[1]
lon = sys.argv[2]
dist = sys.argv[3]

sock = socket.create_connection(('aprs.glidernet.org', 14580)) # TCP
sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)

print ("Socket connected")

# logon to OGN APRS network
USER = "LiveTrffc"               # Set to your username for sending data
PASSCODE = -1                   # Passcode = -1 is readonly, set to your passcode for useranme to send data
login = 'user %s pass %s vers Py_Test 0.0.1 filter r/%s/%s/%s -p/oimqstunw\r\n'  % (USER, PASSCODE, lat, lon, dist)
print (login)
sock.send(login.encode('ascii'))
print ("Login sent")
i = 0

try:

    while(1):
        r = sock.recv(1024)
        i += 1
        if (len(r) > 0):
            print ("{0}. {1}".format(i, r.decode('ascii').rstrip("\r\n")))

except OSError as err:
    print ("OS Error: {0}".format(err))

except:
    # eat the exception (like Ctrl-C interrupt)
    print ("")

finally:
    sock.close()
    print ("Socket closed")
