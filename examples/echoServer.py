#!/usr/bin/python

import sys
import pycoxnet
import time
import threading

if(len(sys.argv) != 2):
    name = sys.argv[0]
    print("Usage: {0} vdeurl\ne,g: {1} vxvde://234.0.0.1\n\n".format(name, name))
    exit(1)

stack  = pycoxnet.Stack("picox", sys.argv[1])

ifindex = stack.if_nametoindex("vde0")

stack.linksetupdown(ifindex, 1)
stack.ipaddr_add(pycoxnet.AF_INET, "10.0.0.1", 24, ifindex)
stack.iproute_add(pycoxnet.AF_INET, "10.0.0.254")

sock = stack.socket(pycoxnet.AF_INET, pycoxnet.SOCK_STREAM)

sock.bind(('', 5000))

sock.listen(1)

def handle(conn, addr):
    while True:
        data = conn.recv(1024)
        if not data:
            print("Connection closed by", addr)
            break
        print("Got:", data.decode(), "from", addr)
        conn.send(data)

while True:
    conn, addr = sock.accept()
    print("New connection by {}".format((conn, addr)))
    t = threading.Thread(target = handle, args=(conn, addr), daemon=True)
    t.start()

