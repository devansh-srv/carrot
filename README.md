# Carrot

Carrot is a Redis-like key-value database that supports both basic KV operations and sorted sets. It implements a subset of Redis commands with a focus on simplicity and efficiency.

## Prerequisites

Before building Carrot, ensure you have the following installed:
- CMake (version 3.0 or higher)
- Make
- GCC or Clang compiler
- Unix-like operating system

## Building from Source

1. Clone the repository:
```bash
git clone git@github.com:devansh-srv/carrot.git
cd carrot
```

2. Give execution permission to the build script:
```bash
chmod +x build.sh
```

3. Build the project:
```bash
./build.sh
```

This will initialize CMake and build both the server and client binaries.

## Running the Server

Start the server by specifying a port number:
```bash
cd build
./bin <port>
```

Example:
```bash
cd build
./bin 6379
```

## Using the Client

The client can be used to send commands to the server:
```bash
cd build
./client <hostname> <port> <command>
```

Example:
```bash
./client localhost 6379 get mykey
```

## Supported Commands

### Basic Key-Value Operations

1. **GET** - Retrieve a value by key
```bash
./client localhost 6379 get mykey
```

2. **SET** - Set a value for a key
```bash
./client localhost 6379 set mykey myvalue
```

3. **DEL** - Delete a key-value pair
```bash
./client localhost 6379 del mykey
```

4. **KEYS** - List all stored keys
```bash
./client localhost 6379 keys
```

### Sorted Set Operations

1. **ZADD** - Add a scored element to a sorted set
```bash
./client localhost 6379 zadd myset 1.5 element1
```

2. **ZREM** - Remove an element from a sorted set
```bash
./client localhost 6379 zrem myset element1
```

3. **ZSCORE** - Get the score of an element
```bash
./client localhost 6379 zscore myset element1
```

4. **ZRANK** - Get the rank of an element
```bash
./client localhost 6379 zrank myset element1
```

5. **ZQUERY** - Query elements in a sorted set
```bash
./client localhost 6379 zquery myset 1.0 "" 0 10
```
Parameters:
- `key`: The sorted set key
- `score`: Minimum score to start from
- `name`: Minimum element name to start from
- `offset`: Number of elements to skip
- `limit`: Maximum number of elements to return


## Technical Details

- Written in C
- Uses non-blocking I/O
- Implements an event loop using poll()
- Supports concurrent client connections
- Uses FNV hash for key hashing
- Implements AVL trees for sorted sets

## **Acknowledgement**
- Thank you [Vansh Jangir](https://github.com/vanshjangir) and [Amritanshu Darbari](https://github.com/MinuteHanD) for coming up with this amazing name.
- Thank you to [Erik Demaine](https://github.com/edemaine) for this amazing lecture on data structure augmentation that helped me in understanding AVL trees and range queries.
- [MITOCW Lecture](https://youtu.be/xVka6z1hu-I?si=tACOW2nsMhOAcdJ8)
- Also big thanks to [0xAX](https://github.com/0xAX) for the amazing repo that taught me about intrinsic data structures
- [linux-insiders](https://github.com/0xAX/linux-insides)

## Contributing

Feel free to submit issues and pull requests.
