#!/usr/bin/python

import sys
import pycoxnet
import time
import threading
import datetime

if(len(sys.argv) != 2):
    name = sys.argv[0]
    print("Usage: {0} vdeurl\ne,g: {1} vxvde://234.0.0.1\n\n".format(name, name))
    exit(1)

stack  = pycoxnet.Stack("picox", sys.argv[1])
ifindex = stack.if_nametoindex("vde0")

stack.ipaddr_add(pycoxnet.AF_INET, "10.0.0.1", 24, ifindex)
sock = stack.socket(pycoxnet.AF_INET, pycoxnet.SOCK_DGRAM)
sock.bind(('', 5000))

while(True):
    tempVal, addr = sock.recvfrom(1024);
    print("Temp at is %s in %s"%(tempVal.decode(), addr))
    response = "Received at %s is %s"%(addr, datetime.datetime.now())
    sock.sendto(response.encode(), addr)
