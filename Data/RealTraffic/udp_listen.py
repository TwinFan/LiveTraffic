#!/usr/bin/python

import sys
import socket
#---socket creation
connexion = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)

#---Bind
try:
    connexion.bind(('', int(sys.argv[1])))
except socket.error:
    print "connexion failed"
    connexion.close()
    sys.exit()

#---testing
while 1:
    data, addr = connexion.recvfrom(1024)
    print "messages : ",addr , data
