#!/usr/bin/python3

import sys
import socket

if len(sys.argv) < 3:
    print ("Usage: udp_send.py <TargetHost> <Port> [msg]")
    exit(1)

UDP_IP = sys.argv[1] # "localhost"
UDP_PORT = int(sys.argv[2]) # 49003
MESSAGE = "Hello, World!"
if len(sys.argv) >= 4:
    MESSAGE = sys.argv[3]

print ("UDP target IP:", UDP_IP)
print ("UDP target port:", UDP_PORT)
print ("message:", MESSAGE)

sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM, socket.IPPROTO_UDP) # UDP

# User wants broadcast? We can do that!
if UDP_IP == '255.255.255.255':
    sock.setsockopt(socket.SOL_SOCKET, socket.SO_BROADCAST, 1)

sock.sendto(MESSAGE.encode('utf-8'), (UDP_IP, UDP_PORT))
