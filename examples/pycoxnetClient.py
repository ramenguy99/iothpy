#! /usr/bin/python3
import sys
import pycoxnet
import re
import select
import fileinput

def client(fd):
    pollerObject = select.poll()
    pollerObject.register(fd, select.POLLIN);
    pollerObject.register(sys.stdin, select.POLLIN)
    
    while(True):
        events = pollerObject.poll(10000);
        for descriptor, event in events:
            if(descriptor == fd):
                message = pycoxnet.read(fd, 1024)
                if(message):
                    print(message)
                else:
                    break
            if(descriptor == sys.stdin):
                message = fileinput.input()[0].rstrip()
                if(message):
                    pycoxnet.write(fd, message)
                else:
                    break

    return

def main():
    if(len(sys.argv) != 6):
        name = sys.argv[0]
        print("Usage: {0} vde_net addr prefix port\ne,g: {1} vxvde://234.0.0.1 10.0.0.10 24 5000\n\n".format(name, name))
        return 1

    stack = pycoxnet.stack(sys.argv[1])

    prefix = int(re.search(r'\d+', sys.argv[3]).group())
    port = int(re.search(r'\d+', sys.argv[5]).group())

    ifindex = stack.if_nametoindex("vde0")

    try:
        addr = pycoxnet.inet_pton(pycoxnet.AF_INET, sys.argv[2])
    except OSError:
        print("invalid ip addr")
    
    try:
        stack.ipaddr_add(pycoxnet.AF_INET, addr, prefix, ifindex)
    except Exception as e:
        print(e)

    fd = stack.msocket(pycoxnet.AF_INET, pycoxnet.SOCK_STREAM, 0)
    if(fd < 0):
        return 1

    try:
        sin_addr = pycoxnet.inet_pton(pycoxnet.AF_INET, sys.argv[4])
    except OSError:
        print("invalid server addr")

    pycoxnet.connect(fd, pycoxnet.AF_INET, port, sin_addr)
    
    client(fd)
    pycoxnet.close(fd)
    


if __name__ == "__main__":
    main()