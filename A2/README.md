# Mimicking distributed file transfer
Program to download a file using a simple protocol on top of TCP. Multiple clients on a LAN can download the file in chunks and reassemble it on their local machine.

## Usage
```
g++ client.cpp -o client
./client <server-ip>:<server-port> <other-client1-ip>:<other-client1-port> ...repeat last argument for other clients
```

## Protocol
The client requests a line by sending a ```SENDLINE\n``` message to the server. The server responds with a ```<line-number>\n<line>\n``` message containing a random line. The client then requests the next line. This continues till it receives all the lines.

## Error handling
The server responds with a ```-1\n``` message if the client send too many requests in a short time.
The client also handles cases in which peers might be down or not responding and tries to download the file from other peers if they have complete file which may happen if our client had gone down in between.
