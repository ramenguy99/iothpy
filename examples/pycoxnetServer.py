#! /usr/bin/python3
import sys
import pycoxnet
import re

def main():
    if(len(sys.argv) != 5):
        name = sys.argv[0]
        print("Usage: {0} vde_net addr prefix port\ne,g: {1} vxvde://234.0.0.1 10.0.0.10 24 5000\n\n".format(name, name))
        return 1

    stack = pycoxnet.stack(sys.argv[1])

    prefix = int(re.search(r'\d+', sys.argv[3]).group())
    port = int(re.search(r'\d+', sys.argv[4]).group())

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

    # servaddr = {"sin_family" : pycoxnet.AF_INET, "sin_port": pycoxnet.htons(port), "s_addr": pycoxnet.htonl(pycoxnet.INADDR_ANY)}

    # print(pycoxnet.bind(fd, servaddr))


if __name__ == "__main__":
    main()