# myNFS


## Project Overview

**myNFS** is a lightweight implementation of a Network File System using RPC (Remote Procedure Call).
It allows remote file and directory operations on a server, accessible from a simple interactive client.

## Key Features
<img width="1440" height="945" alt="image" src="https://github.com/user-attachments/assets/d7c7c659-bf9f-4995-b09b-7fd4b643cffc" />

- **RPC-based communication**
- **File & directory operations** 
- Interactive **edit/read** support
- `help` command with suggestions for unknown commands

### Dependencies

- **LibTIRPC**: The project uses `libtirpc` to implement the transport-independent RPC.
  - To install: `sudo apt-get install libtirpc-dev`
- **GCC** + **make**

### Compilation

To compile the NFS client:

```bash
gcc -g -o nfs_client nfs_client.c -ltirpc
```
To compile the NFS server:

```bash
gcc -g -o nfs_server nfs_server.c -ltirpc

```
Or:
```bash
make -f Makefile.nfs
```

### Running the Project

Start the NFS server:

```bash

./nfs_server
```

Run the NFS client to connect to the server:

```bash
./nfs_client
```