#!/usr/bin/python

import sys
import pycoxnet
import time
import threading

if(len(sys.argv) != 5):
    name = sys.argv[0]
    print("Usage: {0} vdeurl ip prefix port\ne,g: {1} vxvde://234.0.0.1 10.0.0.1 24 5000\n\n".format(name, name))
    exit(1)

stack  = pycoxnet.stack("vdestack", sys.argv[1])

prefix = int(sys.argv[3])
port  = int(sys.argv[4])
addr = pycoxnet.inet_pton(pycoxnet.AF_INET, sys.argv[2])
gw_addr = pycoxnet.inet_pton(pycoxnet.AF_INET, "10.0.0.254")
ifindex = stack.if_nametoindex("vde0")

stack.linksetupdown(ifindex, 1)
stack.ipaddr_add(pycoxnet.AF_INET, addr, prefix, ifindex)
stack.iproute_add(pycoxnet.AF_INET, None, 0, gw_addr)

sock = stack.socket(pycoxnet.AF_INET, pycoxnet.SOCK_STREAM)

sock.bind(('', port))

sock.listen(1)

def handle(conn, addr):
    while True:
        data = conn.recv(1024)
        if not data:
            print("Connection closed by", addr)
            break
        print("Got:", data.decode(), "from", sender)
        conn.send(data)

while True:
    conn, addr = sock.accept()
    print("New connection by {}".format((conn, addr)))
    t = threading.Thread(target = handle, args=(conn, addr), daemon=True)
    t.start()

