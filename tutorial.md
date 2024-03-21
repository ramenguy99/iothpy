# iothpy

## Python module for ioth, iothconf and iothdns support

This module provides the creation and configuration of Internet of Threads stack and socket, with an additional support for clients who need to query DNS servers, using iothdns API.

### classes
To configure the stack

#### Stack

##### Creation
To create a stack object, the constructor is:
```py
iothpy.Stack(stack_name, vdeurl = None, config_dns = None)
```
where:
* stack_name (required): it is the name of ioth stack to be used (e.g. vdestack, picox, ...), or a string following newstackc configuration syntax.
* vdeurl(optional): can be a string or a list of string representing the vde urls. The stack will have an interface for each vde.
* config_dns(optional): path or string for the configuration of iothdns descriptor, following /etc/resolv.conf syntax. Default value is"/etc/resolv.conf".

Note: if stack_name isn't a configuration and vdeurl is None, then the stack will be initializated with no interface.

##### Configuration
To configure the stack you can use these functions:
    `linksetupdown`, `linkgetaddr`, `linksetaddr`, `iplink_add`, `iplink_del`, `linksetmtu`, `ipaddr_add`, `ipaddr_del`,`iproute_add`, `iproute_del`.

Or you can use a single method: `iothconfig(config)`

To update the dns, you can use `iothdns_update(config)`

##### Other methods
```py
Stack.getaddrinfo(host, port, family=0, type=0, proto=0)
```
It translate the *host/port* argument in a sequence of 5-tuple containing all the info for create a socket connected to service.
This method have the same signature of builtin `socket.getaddrinfo()` but uses iothdns.


```py
Stack.socket(self, family=-1, type=-1, proto=-1, fileno=None)
```
Create and return a new socket on this stack
This method take the same parameters as the builtin `socket.socket()` function

#### Socket


### Installation

### Example