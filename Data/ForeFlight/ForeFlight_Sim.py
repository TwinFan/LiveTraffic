#!/usr/bin/python3
# Miniature mock-up of interface to ForeFlight
# - Every 5s broadcasts the message {"App":"ForeFlight","GDL90":{"port":4000, "sim":true}} on port 63093
# - Listens on UDP Port 49002 and outputs whatever it receives

import sys
import socket
import time

#--- Output Socket (Broadcast)
sockBcst = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sockBcst.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

#--- Listen Socket
sockListen = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP)
sockListen.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
if sys.platform != "win32":
	sockListen.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)
sockListen.settimeout(1.0)
sockListen.bind(('', 49002))

#---wait for and print data
tsNextBcst = 0.0
while 1:
    # Every (about) 5s we broadcast our availability to the world
    if (time.monotonic() > tsNextBcst):
        sockBcst.sendto(b'{"App":"ForeFlight","GDL90":{"port":4000, "sim":true}}', ('<broadcast>', 63093))
        tsNextBcst = time.monotonic() + 5.0
    # Wait for data, timeout is defined about (1s) and would throw an exception
    try:
        data, addr = sockListen.recvfrom(1024)
        print(time.strftime('%H:%M:%S'), addr)
        print(data.decode("utf-8"))
    except OSError:
         pass
