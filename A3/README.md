# Reliable and congestion friendly file transfer
Program to download a file using a TCP-like protocol for reliable and congestion friendly file transfer on top of UDP.

The server implements a leaky bucket filter and may also ignore some requests randomly to simulate packet loss.

## Usage
```
g++ UDPClient.cpp md5.cpp -o client
./client <server-ip> <server-port>
```

## Protocol
- The first request a client should send to the server is to know how many bytes to receive. ```SendSize\n\n```
- The server responds with a ```Size:<no. of bytes>\n\n``` message.
- The client then requests the file in chunks. ```Offset:<offset>\nNumBytes:<size>\n\n```
- After receiving the file, the client sends a md5 hash of the file to the server to verify the integrity of the file.
