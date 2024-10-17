# How to use ?

# host as client
- your project needs the following files: dhcp_client.c/.h and dhcp_common.c/.h
- set the value of the CLIENT_CAN_BE_SERVER define to 0 in dhcp_common.h
- call dhcp_client_init() to start the client. The LWIP stack must be first initialized

## host as server
- your project needs the following files: dhcp_client.c/.h and dhcp_common.c/.h
- call dhcp_server_init() to start the server. The LWIP stack must be first initialized

## host as client and maybe as server
- your project needs all the files of this library
- set the value of the CLIENT_CAN_BE_SERVER define to 0 in dhcp_common.h
- call dhcp_role_resolver() to start the host with autonomous DHCP role resolution