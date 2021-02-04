#!/usr/bin/python3

import iothpy

stack = iothpy.Stack("vdestack", "null://")
if_index = stack.if_nametoindex("vde0")

stack.linksetupdown(if_index, 1)
stack.ipaddr_add(iothpy.AF_INET, "10.0.0.1", 24, if_index)
stack.iproute_add(iothpy.AF_INET, "10.0.0.254")
stack.linksetaddr(if_index, "80:00:42:0e:e7:3a")

s = stack.socket(iothpy.AF_INET, iothpy.SOCK_STREAM);

addr = stack.linkgetaddr(if_index)
print(addr.hex(), len(addr))
stack.linksetmtu(if_index, 500)

newaddr = bytes.fromhex("8000420ee73a")
print(len(newaddr))

addr = stack.linkgetaddr(if_index)
print(addr.hex(), len(addr))

# print(iothpy.getdefaulttimeout())
# iothpy.setdefaulttimeout(2)
# print(iothpy.getdefaulttimeout())

# s = stack.socket(iothpy.AF_INET, iothpy.SOCK_STREAM);
# print("Socket is blocking when created: ", s.getblocking());
# s.setblocking(False);
# print("Socket is blocking after setting to non blocking: ", s.getblocking());
# s.setblocking(True);
# print("Socket is blocking after resetting to blocking: ", s.getblocking());


# Test that accept does not block
# s.setblocking(False)

# s.bind(('', 5000))
# s.listen(1)

# print(s.gettimeout())
#s.settimeout(3)
# print(s.gettimeout())

# try:
#     conn, addr = s.accept()
# except iothpy.timeout as e:
#     print("Accept would block: ", e)


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
# print(iothpy.AF_INET);
# print(hex(iothpy.htonl(0xFF)))
# 
# addr = iothpy.inet_pton(iothpy.AF_INET, "10.0.0.42")
# print(addr)
# s.ipaddr_add(iothpy.AF_INET, addr, 24, if_index)
# 
# addr = iothpy.inet_pton(iothpy.AF_INET6, "::42");
# print(addr)
# s.ipaddr_add(iothpy.AF_INET6, addr, 64, if_index)


