#!/usr/bin/python

import sys
import socket
import time

#---socket creation
connexion = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
connexion.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
connexion.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEPORT, 1)

#---Bind
try:
    connexion.bind(('', int(sys.argv[1])))
except socket.error:
    print "connexion failed"
    connexion.close()
    sys.exit()

#---wait for and print data
while 1:
    data, addr = connexion.recvfrom(1024)
#    print "messages : ",addr , data
    print time.time(), ":", data
