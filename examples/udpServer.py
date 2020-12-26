#!/usr/bin/python

import sys
import pycoxnet
import time
import threading
import datetime

if(len(sys.argv) != 5):
    name = sys.argv[0]
    print("Usage: {0} vdeurl ip prefix port\ne,g: {1} vxvde://234.0.0.1 10.0.0.1 24 5000\n\n".format(name, name))
    exit(1)

stack  = pycoxnet.Stack("picox", sys.argv[1])

prefix = int(sys.argv[3])
port  = int(sys.argv[4])
ifindex = stack.if_nametoindex("vde0")
addr = pycoxnet.inet_pton(pycoxnet.AF_INET, sys.argv[2])

stack.ipaddr_add(pycoxnet.AF_INET, addr, prefix, ifindex)

sock = stack.socket(pycoxnet.AF_INET, pycoxnet.SOCK_DGRAM)

sock.bind(('', port))

while(True):
    tempVal, addr = sock.recvfrom(1024);
    print("Temp at is %s in %s"%(tempVal.decode(), addr))
    response = "Received at %s is %s"%(addr, datetime.datetime.now())
    sock.sendto(response.encode(), addr)
