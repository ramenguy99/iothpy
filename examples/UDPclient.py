import sys
import pycoxnet
import time
import select
import random

def getTemp():
    temp = random.uniform(60.0, 62.0)
    return temp

if(len(sys.argv) != 6):
    name = sys.argv[0]
    print("Usage: {0} vdeurl client_ip prefix server_ip port\ne,g: {1} vxvde://234.0.0.1 10.0.0.2 24 10.0.0.1 5000\n\n".format(name, name))
    exit(1)

stack  = pycoxnet.stack("picox", sys.argv[1])

prefix = int(sys.argv[3])
port  = int(sys.argv[5])
ifindex = stack.if_nametoindex("vde0")
addr = pycoxnet.inet_pton(pycoxnet.AF_INET, sys.argv[2])

stack.ipaddr_add(pycoxnet.AF_INET, addr, prefix, ifindex)

sock = stack.socket(pycoxnet.AF_INET, pycoxnet.SOCK_DGRAM)#udp

temp = getTemp()
tempString = "%.2f"%temp

sock.sendto(tempString.encode(), (sys.argv[4], port))

response = sock.recv(1024)

print(response.decode())

