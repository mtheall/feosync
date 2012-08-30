# FeOSync

This is a sync daemon/client for [FeOS](https://github.com/fincs/FeOS "FeOS").

## Prerequisites

- [FeOS](https://github.com/fincs/FeOS "FeOS")
- [zlib](http://zlib.net "zlib")
- [OpenSSL](http://www.openssl.org "OpenSSL")

## Installation

### Windows

Support for Windows is included via MinGW. If you already have FeOS and its
prerequisites installed, you will have MinGW.

#### Installing OpenSSL for MinGW

Open the MinGW shell by going to Start -> Programs -> devkitPro -> MSys
In the prompt, use the following commands:

    mingw-get install msys-libopenssl
    cp -r /c/MinGW/msys/1.0/include/openssl /mingw/include
    cp -r /c/MinGW/msys/1.0/lib/openssl /mingw/lib
    cp /c/MinGW/msys/1.0/bin/msys-crypto-1.0.0.dll /mingw/bin
    cp /c/MinGW/msys/1.0/bin/msys-ssl-1.0.0.dll /mingw/bin

#### Installing zlib for MinGW

A similar approach to installing OpenSSL does not work. The zlib you get from
`mingw-get` will crash. Instead, I had to download and build from source.
Download from [zlib.net](http://zlib.net "zlib.net"), then perform the
following commands:

    tar -xzf zlib-1.2.7.tar.gz
    cd zlib-1.2.7
    make -f win32/Makefile.gcc
    cp zconf.h zlib.h /mingw/include
    cp zlib1.dll /mingw/bin
    cp libz.a libz.dll.a /mingw/lib

#### Installing FeOSync

In the feosync directory, perform the following commands:

    make
    make install

### Normal Operating Systems

#### Installing zlib and OpenSSL

Getting zlib and OpenSSL are fairly straightforward depending on your Operating
System. In the worst case, you will need to download and build from source
using the standard `./configure && make && make install`.

#### Installing FeOSync

In the feosync directory, perform the following commands:

    make
    make install

## Usage

FeOSync consists of two programs:

- A daemon that runs in FeOS
- A client that runs on your PC

### Using the daemon

The FeOSync daemon simply has two commands:

    feosync start
    feosync stop

Obviously, the first starts the daemon, and the second stops it. If no argument
is provided, then it is identical to `feosync start`. This will spawn the
FeOSync daemon, which will happily run in the background while you enjoy other
applications. It is designed to have minimal impact on foreground applications.

`feosync stop` simply tells the daemon to quit. It will finish any currently
running sync job before exiting.

### Using the client

The FeOSync client has only one command:

    feosync <directory>

The client will switch to the provided directory, then it will connect to the
daemon and begin the sync process. It will clone the provided directory to the
root of the storage device where FeOS resides. Therefore, it is best to provide
$FEOSDEST as the directory for updating FeOS. For copying other data, such as
music or programs, the directory chosen must have a layout that you want to
put onto the storage card.

### The sync process

Synchronization occurs in a very straightforward manner. The daemon sits idly,
broadcasting itself so that the client can discover it. It also listens for
incoming connections. The client will listen for the broadcasts, and then
connect to the daemon when it receives one.

First, the client will send a list of directories to the daemon, which will
create the directories if they do not exist. Then, the client will send
filenames to the daemon to check for data mismatches. Both the client and
daemon will checksum the file. If the file does not exist on the daemon side,
or if the md5sum does not match, then the client will send the file
(compressed with zlib to minimize network traffic), and the daemon will
decompress it onto the storage medium. Once all of the files have been updated,
the client will disconnect from the daemon, and the daemon will resume
broadcasting and listening for connections.
