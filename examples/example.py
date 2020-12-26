#!/usr/bin/python3

import pycoxnet

stack = pycoxnet.Stack("vdestack", "null://")
if_index = stack.if_nametoindex("vde0")

addr = pycoxnet.inet_pton(pycoxnet.AF_INET, "10.0.0.1")
gw_addr = pycoxnet.inet_pton(pycoxnet.AF_INET, "10.0.0.254")

stack.linksetupdown(if_index, 1)
stack.ipaddr_add(pycoxnet.AF_INET, addr, 24, if_index)
stack.iproute_add(pycoxnet.AF_INET, None, 0, gw_addr)


print(pycoxnet.getdefaulttimeout())
pycoxnet.setdefaulttimeout(2)
print(pycoxnet.getdefaulttimeout())

s = stack.socket(pycoxnet.AF_INET, pycoxnet.SOCK_STREAM);
# print("Socket is blocking when created: ", s.getblocking());
# s.setblocking(False);
# print("Socket is blocking after setting to non blocking: ", s.getblocking());
# s.setblocking(True);
# print("Socket is blocking after resetting to blocking: ", s.getblocking());


# Test that accept does not block
# s.setblocking(False)

s.bind(('', 5000))
s.listen(1)

print(s.gettimeout())
#s.settimeout(3)
print(s.gettimeout())

try:
    conn, addr = s.accept()
except pycoxnet.timeout as e:
    print("Accept would block: ", e)


# print("repr " + repr(s))
# print("str " + str(s))
# print("getstack " + hex(s.getstack()))
# 
# if_index = s.if_nametoindex("vde0")
# print("vde0 index: ", if_index)
# try:
#     print("bananas index: ", s.if_nametoindex("bananas"))
# except Exception as e:
#     print(str(e))
# 
# 
# print(pycoxnet.AF_INET);
# print(hex(pycoxnet.htonl(0xFF)))
# 
# addr = pycoxnet.inet_pton(pycoxnet.AF_INET, "10.0.0.42")
# print(addr)
# s.ipaddr_add(pycoxnet.AF_INET, addr, 24, if_index)
# 
# addr = pycoxnet.inet_pton(pycoxnet.AF_INET6, "::42");
# print(addr)
# s.ipaddr_add(pycoxnet.AF_INET6, addr, 64, if_index)


