#!/usr/bin/python3

import sys
import iothpy
import time
import select
import random

def getTemp():
    temp = random.uniform(60.0, 62.0)
    return temp

if(len(sys.argv) != 2):
    name = sys.argv[0]
    print("Usage: {0} vdeurl\ne,g: {1} vxvde://234.0.0.1\n\n".format(name, name))
    exit(1)

stack  = iothpy.Stack("vdestack", sys.argv[1])
ifindex = stack.if_nametoindex("vde0")

stack.ipaddr_add(iothpy.AF_INET, "10.0.0.2", 24, ifindex)
sock = stack.socket(iothpy.AF_INET, iothpy.SOCK_DGRAM)

temp = getTemp()
tempString = "%.2f"%temp

sock.sendto(tempString.encode(), ("10.0.0.1", 5000))
response = sock.recv(1024)

print(response.decode())

