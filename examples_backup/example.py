#!/usr/bin/python3

import pycoxnet

s = pycoxnet.stack("null://")

print("repr " + repr(s))
print("str " + str(s))
print("getstack " + hex(s.getstack()))

if_index = s.if_nametoindex("vde0")
print("vde0 index: ", if_index)
try:
    print("bananas index: ", s.if_nametoindex("bananas"))
except Exception as e:
    print(str(e))


print(pycoxnet.AF_INET);
print(hex(pycoxnet.htonl(0xFF)))

addr = pycoxnet.inet_pton(pycoxnet.AF_INET, "10.0.0.42")
print(addr)
s.ipaddr_add(pycoxnet.AF_INET, addr, 24, if_index)

addr = pycoxnet.inet_pton(pycoxnet.AF_INET6, "::42");
print(addr)
s.ipaddr_add(pycoxnet.AF_INET6, addr, 64, if_index)


