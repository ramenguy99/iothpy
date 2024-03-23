import iothpy

#create and configure stack
stack = iothpy.Stack("vdestack","slirp://")
stack.ioth_config("auto")

# host and port to connect to
host ="www.google.com"
port = 80

addrinfos = stack.getaddrinfo(host, port)

print(f"All addresses info of {host}, port {port}:\n\n{addrinfos}]\n")

family, type, proto, _, sockaddr = addrinfos[0]
print(f"Address 0 info, used as test:\n{addrinfos[0]}.\nSo the address to connect the socket is:\n{sockaddr}\n")

# create socket from the stack
socket = stack.socket(family, type, proto)

socket.connect(sockaddr)

print("Connection established to ", socket.getpeername())