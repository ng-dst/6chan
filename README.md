# Lab 6: socket-based messaging board

## 6chan - Welcome back. Again.

6chan is an anonymous messaging board (like a group chat / forum) with file sharing. \
To ensure users' privacy, all user and message data is processed in RAM, so it's all destroyed once server is shut down.

## Usage

### Program startup

Run `server`:
* `server.exe` 
* `server.exe [port]` 
* `server.exe [host] [port]`

Default is `127.0.0.1:5000`. Server writes logs to _stderr_, which can be piped to file.

Run `client`:
* `client.exe`
* `client.exe [port]`
* `client.exe [host] [port]`

Client connects to _host:port_ and establishes session. On connect, message history syncs automatically. 

### Available commands

* `/file` - upload file
* `/dl <id>` - download file or message by `#id`
* `/sync <id>` - sync manually, starting after `#id` _(unused, unless network errors occur)_
* `/q` - quit

## Compile definitions

* `-D DEBUG` (`./CMakeLists.txt`) to build debug version: extended logging, slower polling rates
* `-D USE_COLOR` (`./client/CMakeLists.txt`) to colorize console text _(recommended)_

## Server architecture

List of server controllers:
* `startServer()`
  - Initialize _socket(), bind(), listen()_
  - Initialize global _Client List_ and _Message History_
  - Call `startAllControllers()`
  - Clean up global lists
  

* `startAllControllers()`
  - Initialize Critical Section for _Message History_
  - Create thread for `clientMgmtController()`
  - Wait for _stdin_
  - Close socket and set _cv_stop_ flag
  - Wait for _clientMgmtController()_


* `clientMgmtController()`
  - Call _accept()_ in loop
  - For each client socket, create `messageController()` thread
  - If socket is closed, wait for all controller threads
  

* `messageController()`
  - Receive client data in loop
  - Call `parseMessageFromClient()` to form a _Message_ from raw buffer
  - Process message based on message type:
    * _Sync_: send new messages (if any) to client
    * _Download_: find file in _Message History_, call `sendFileToClient()` 
    * _File_, _Message_: add data to _Message History_

## Client architecture

List of client routines and services:

* `runClient()`
  - Initialize _socket(), connect()_
  - Call `startAllServices()`


* `startAllServices()`
  - Initialize Critical Sections for _send()_ and _recv()_
  - Create event for client stop
  - Create threads for services:
    * `syncService()`
    * `recvService()`
    * `sendService()`
  - Wait for event
  - Close socket
  - Wait for remaining threads


* `syncService()`
  - Send `/sync <last_msg_id>` command in loop
  - Sleep for polling delay
  - Uses lock for _send()_


* `recvService()`
  - Receive until `\0` in loop
  - Print each message in terminal
  - Update `last_msg_id` based on incoming message id
  - Uses lock for _recv()_


* `sendService()`
  - Process user input in loop
  - Parse commands:
    * `/file` - call `clientUploadFile()`, uses lock for _send()_
    * `/dl <id>` - call `clientDownloadFile()`, uses locks for _send()_ and _recv()_
    * `/q` - set event, set stop flag, and return
  - If input is not a command, send message. uses lock for _send()_


## Bufferized receive: recvuntil(), recvlen()

Both client and server use the following _recv()_ wrappers:
* `recvuntil(char delim)`: Allocate buffer and receive until `delim` character encounters
* `recvlen(int len)`:  Allocate buffer and receive exactly `len` bytes

Uses thread-local (for server) or shared (for client) static buffer.
Buffer state persists between calls.

`buf` = persistent buffer, shared by _recvuntil()_ and _recvlen()_\
`end` = current index of last byte in buffer \
`size` = current allocated buffer size

### Algorithm
Here is pseudocode for recvuntil(). Function recvlen() has similar algorithm.
```
      if buf == NULL:
          allocate buf
          size = BASE_LEN

      while True:
          if end >= len:   (i.e. buffer has enough data)
              allocate 'return buffer', copy first `len` bytes
              pop first `len` bytes from buffer, shift remaining data
              shrink buffer, decrease size
              ptr = 'return buffer'
              return len
          else:
              n = recv( &buf[end] <- (size-end) bytes )
              end = end + n
              if end > MAX_SIZE:
                  clear buffer, return 1
              if end >= size:
                  extend buffer, increase size
```

### Illustration:

```
     0             end       end+n       size          
buf  ===============-----------|-----------
            recv -> ============
```
######
```
     0                         end       size          
buf  ===========================-----------
                        recv -> ==================
```
######
```
     0                         end                                 size
buf  ======================================--------------------------
                                   recv -> =======
```
######
```
     0                                          end    pos         size
buf  =============================================------|------------
                                          recv -> ======d=======
```
######
```
     0                                                 pos    end  size
buf  ===================================================d=======-----
     v    v    v    v    v    v    v    v    v    v    v
ret  ===================================================
```
######
```
     0     end                                                     size
buf  ========--------------------------------------------------------
```
######
```
     0     end                      size
buf  ========-------------------------
```

## Improvements

This lab uses `recvuntil('\0')` for messages and commands, which means data is received dynamically. Though it works fine, it's not the best approach, as we don't know message length beforehand, and message type is parsed from `/commands`.

Better **implement your own _TV / TLV_ protocol** for messages (use tags from `Message` struct) and receive values with `recvlen()`.