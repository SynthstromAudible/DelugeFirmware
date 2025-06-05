---
title: How the SysEx protocol works
---

## Overview

The SysEx File System API provides remote file system access to the Deluge device through MIDI System Exclusive (SysEx) messages. The protocol uses JSON-encoded commands sent over MIDI SysEx to perform file operations like reading, writing, directory listing, and file management.

## Protocol Fundamentals

### Key Specifications

- **Transport**: MIDI System Exclusive (SysEx) messages
- **Data Format**: JSON
- **Session Management**: Session-based with message sequence numbers
- **File Handle System**: File descriptor-based operations
- **Maximum Open Files**: 4 concurrent files
- **Block Buffer Size**: 1024 bytes maximum per operation

### Message Format

Messages consist of a SysEx "packet" which has a header, a sequence number, a JSON body, an optional 0 separator and packed binary part.

All messages follow the standard SysEx format:

```
F0 00 21 7B 01 <command> <sequence_number> <json_data> F7
```

- `F0`: SysEx start
- `00 21 7B`: Synthstrom Audible (Deluge manufacturer ID)
- `01`: Product ID
- `<command>`: Command type (Json=0x06, JsonReply=0x07)
- `<sequence_number>`: Message sequence number for session tracking (1-127)
- `<json_data>`: JSON payload
- `F7`: SysEx end

### Data Encoding

Packed binary is used to transfer file content to or from the Deluge. So far only the read and write requests use it.

The packed binary part, if present, is encoded in 7 byte to 8 byte format. In 7 to 8 format, we send the information about a 7 byte-long block as 8 bytes, with the first byte in the group holding the sign bits of the following 7 bytes. Those following 7 bytes have their high-order bit masked-off, resulting in valid SysEx data.

Binary data in read/write operations uses 7-bit MIDI-safe encoding:

- Every 7 bytes of data are encoded as 8 bytes
- The first byte contains the high bits of the following 7 bytes
- This ensures all bytes are ≤ 0x7F (MIDI-safe)

### JSON Structure

The JSON body is always a JSON object with one key/value pair. The key is the request code and the value contains the parameters for the request. We did things this way so that the order-of-appearance of keys does not matter.

An example request:

```json
{ "open": { "path": "/SONGS/SONG004.XML", "write": 1 } }
```

And a response:

```json
{ "^open": { "fid": 2, "size": 0, "err": 0 } }
```

The caret in front of the response is used as a convention, but is not required. The way responses are matched up with requests is by using a sequence number that wraps from 1 to 127. The web side keeps track of sequence numbers and stores a callback function for each outgoing message to handle the reply.

The Deluge can initiate a message exchange by sending a request to the web-side using a sequence number of 0. This is not actually being used yet.

**Implementation Philosophy**: Maximize processing on the web side, minimize state on the Deluge. The device maintains only a small table of open file handles and directory positions.

**Block-based Processing**: Operations transfer data in small blocks. The client tracks offsets and reassembles complete files or directory listings.

## Session Management

### Assign Session

Assigns a session ID for client tracking and message correlation.

**Request:**

```json
{
  "session": {
    "tag": "string"
  }
}
```

**Parameters:**

- `tag` (string, optional): Client-specified tag for session identification

**Response:**

```json
{
  "^session": {
    "sid": 1,
    "tag": "string",
    "midBase": 8,
    "midMin": 9,
    "midMax": 15
  }
}
```

**Response Fields:**

- `sid`: Session ID (1-15)
- `tag`: Echo of request tag
- `midBase`: Base message ID for this session
- `midMin`: Minimum message ID for this session
- `midMax`: Maximum message ID for this session

## File Operations

_Basic file I/O operations using file descriptors (max 4 concurrent files)_

### Open File

Opens a file for reading or writing and returns a file descriptor.

**Request:**

```json
{
  "open": {
    "path": "/path/to/file.txt",
    "write": 0,
    "date": 0,
    "time": 0
  }
}
```

**Parameters:**

- `path` (string, required): Full path to the file
- `write` (integer, required): 0=read, 1=write (create new), 2=write (append)
- `date` (integer, optional): Date for directory creation (FAT format)
- `time` (integer, optional): Time for directory creation (FAT format)

**Response:**

```json
{
  "^open": {
    "fid": 1,
    "size": 1024,
    "err": 0
  }
}
```

**Response Fields:**

- `fid`: File descriptor ID (0 if error)
- `size`: File size in bytes
- `err`: Error code (FRESULT)

### Close File

Closes an open file descriptor.

**Request:**

```json
{
  "close": {
    "fid": 1
  }
}
```

**Parameters:**

- `fid` (integer, required): File descriptor ID to close

**Response:**

```json
{
  "^close": {
    "fid": 1,
    "err": 0
  }
}
```

**Response Fields:**

- `fid`: Echo of file descriptor ID
- `err`: Error code (FRESULT)

### Read Block

Reads a block of data from an open file.

**Request:**

```json
{
  "read": {
    "fid": 1,
    "addr": 0,
    "size": 1024
  }
}
```

**Parameters:**

- `fid` (integer, required): File descriptor ID
- `addr` (integer, required): File offset to read from
- `size` (integer, optional): Number of bytes to read (max 1024)

**Response:**

```json
{
  "^read": {
    "fid": 1,
    "addr": 0,
    "size": 1024,
    "err": 0
  }
}
```

**Response Fields:**

- `fid`: Echo of file descriptor ID
- `addr`: Echo of file offset
- `size`: Actual bytes read
- `err`: Error code (FRESULT)

**Note:** Binary data follows the JSON header, encoded using 7-bit MIDI-safe encoding.

### Write Block

Writes a block of data to an open file.

**Request:**

```json
{
  "write": {
    "fid": 1,
    "addr": 0,
    "size": 1024
  }
}
```

**Parameters:**

- `fid` (integer, required): File descriptor ID
- `addr` (integer, required): File offset to write to
- `size` (integer, required): Number of bytes to write (max 1024)

**Note:** Binary data follows the JSON header, encoded using 7-bit MIDI-safe encoding.

**Response:**

```json
{
  "^write": {
    "fid": 1,
    "addr": 0,
    "size": 1024,
    "err": 0
  }
}
```

**Response Fields:**

- `fid`: Echo of file descriptor ID
- `addr`: Echo of file offset
- `size`: Actual bytes written
- `err`: Error code (FRESULT)

## Directory Operations

_Browse and manage directories (max 25 entries per request)_

### Get Directory Entries

Lists files and directories in a specified path.

**Request:**

```json
{
  "dir": {
    "path": "/path/to/directory",
    "offset": 0,
    "lines": 20
  }
}
```

**Parameters:**

- `path` (string, optional): Directory path (default: "/")
- `offset` (integer, optional): Starting entry offset (default: 0)
- `lines` (integer, optional): Number of entries to return (max 25, default: 20)

**Response:**

```json
{
  "^dir": {
    "list": [
      {
        "name": "filename.txt",
        "size": 1024,
        "date": 21156,
        "time": 34816,
        "attr": 32
      }
    ],
    "err": 0
  }
}
```

**Response Fields:**

- `list`: Array of directory entries
  - `name`: File/directory name
  - `size`: File size in bytes
  - `date`: Last modified date (FAT format)
  - `time`: Last modified time (FAT format)
  - `attr`: File attributes (0x10=dir, 0x20=archive, 0x01=readonly, 0x02=hidden, 0x04=system)
- `err`: Error code (FRESULT)

### Create Directory

Creates a new directory.

**Request:**

```json
{
  "mkdir": {
    "path": "/path/to/new/directory",
    "date": 0,
    "time": 0
  }
}
```

**Parameters:**

- `path` (string, required): Directory path to create
- `date` (integer, optional): Creation date (FAT format)
- `time` (integer, optional): Creation time (FAT format)

**Response:**

```json
{
  "^mkdir": {
    "path": "/path/to/new/directory",
    "err": 0
  }
}
```

**Response Fields:**

- `path`: Echo of directory path
- `err`: Error code (FRESULT)

## File Management

_Advanced file operations: delete, rename, copy, move, timestamps_

### Delete File

Deletes a file or directory.

**Request:**

```json
{
  "delete": {
    "path": "/path/to/file.txt"
  }
}
```

**Parameters:**

- `path` (string, required): Path to file/directory to delete

**Response:**

```json
{
  "^delete": {
    "err": 0
  }
}
```

**Response Fields:**

- `err`: Error code (FRESULT)

### Rename/Move

Renames or moves a file/directory.

**Request:**

```json
{
  "rename": {
    "from": "/old/path/file.txt",
    "to": "/new/path/file.txt"
  }
}
```

**Parameters:**

- `from` (string, required): Source path
- `to` (string, required): Destination path

**Response:**

```json
{
  "^rename": {
    "from": "/old/path/file.txt",
    "to": "/new/path/file.txt",
    "err": 0
  }
}
```

**Response Fields:**

- `from`: Echo of source path
- `to`: Echo of destination path
- `err`: Error code (FRESULT)

### Copy File

Copies a file to a new location.

**Request:**

```json
{
  "copy": {
    "from": "/source/file.txt",
    "to": "/destination/file.txt",
    "date": 0,
    "time": 0
  }
}
```

**Parameters:**

- `from` (string, required): Source file path
- `to` (string, required): Destination file path
- `date` (integer, optional): Timestamp for destination file (FAT format)
- `time` (integer, optional): Timestamp for destination file (FAT format)

**Response:**

```json
{
  "^copy": {
    "from": "/source/file.txt",
    "to": "/destination/file.txt",
    "err": 0
  }
}
```

**Response Fields:**

- `from`: Echo of source path
- `to`: Echo of destination path
- `err`: Error code (FRESULT)

### Move File

Moves a file to a new location (rename or copy+delete for cross-filesystem moves).

**Request:**

```json
{
  "move": {
    "from": "/source/file.txt",
    "to": "/destination/file.txt",
    "date": 0,
    "time": 0
  }
}
```

**Parameters:**

- `from` (string, required): Source file path
- `to` (string, required): Destination file path
- `date` (integer, optional): Timestamp for destination file (FAT format)
- `time` (integer, optional): Timestamp for destination file (FAT format)

**Response:**

```json
{
  "^move": {
    "from": "/source/file.txt",
    "to": "/destination/file.txt",
    "err": 0
  }
}
```

**Response Fields:**

- `from`: Echo of source path
- `to`: Echo of destination path
- `err`: Error code (FRESULT)

### Update Timestamp

Updates the timestamp of a file or directory.

**Request:**

```json
{
  "utime": {
    "path": "/path/to/file.txt",
    "date": 21156,
    "time": 34816
  }
}
```

**Parameters:**

- `path` (string, required): Path to file/directory
- `date` (integer, required): New date (FAT format)
- `time` (integer, required): New time (FAT format)

**Response:**

```json
{
  "^utime": {
    "err": 0
  }
}
```

**Response Fields:**

- `err`: Error code (FRESULT)

## Utility Operations

_Simple connectivity and status operations_

### Ping

Simple connectivity test.

**Request:**

```json
{
  "ping": {}
}
```

**Response:**

```json
{
  "^ping": {}
}
```

## Implementation Details

_Technical specifications, error codes, and system constraints_

### Operations Summary

So far, the file protocol has:

- **session** - Assign session ID
- **open** - Open file for reading/writing
- **close** - Close file descriptor
- **dir** - Get directory entries
- **read** - Read file block
- **write** - Write file block
- **delete** - Delete file/directory
- **mkdir** - Create directory
- **rename** - Rename/move file/directory
- **copy** - Copy file
- **move** - Move file (with fallback to copy+delete)
- **utime** - Update file timestamp
- **ping** - Connectivity test

### High-Level Routines

The file routines include:

- **readFile** - Complete file reading
- **writeFile** - Complete file writing
- **recursiveDelete** - Delete directory tree
- **downloadOneItem** - Download single file/directory
- **recursiveDownload** - Download directory tree
- **getDirInfo** - Get directory information

The file routines work with Uint8Array objects. The assumption is that we have an enormous amount of free memory on the web side. Downloading uses the web File System API and can handle nested directories.

### Error Codes

Error codes follow the FatFs FRESULT enumeration:

- `0`: FR_OK - Success
- `1`: FR_DISK_ERR - Disk error
- `2`: FR_INT_ERR - Internal error
- `3`: FR_NOT_READY - Drive not ready
- `4`: FR_NO_FILE - File not found
- `5`: FR_NO_PATH - Path not found
- `6`: FR_INVALID_NAME - Invalid filename
- `7`: FR_DENIED - Access denied
- `8`: FR_EXIST - File exists
- `9`: FR_INVALID_OBJECT - Invalid object
- `10`: FR_WRITE_PROTECTED - Write protected
- `11`: FR_INVALID_DRIVE - Invalid drive
- `12`: FR_NOT_ENABLED - Volume not enabled
- `13`: FR_NO_FILESYSTEM - No filesystem
- `14`: FR_MKFS_ABORTED - Format aborted
- `15`: FR_TIMEOUT - Timeout
- `16`: FR_LOCKED - File locked
- `17`: FR_NOT_ENOUGH_CORE - Out of memory
- `18`: FR_TOO_MANY_OPEN_FILES - Too many open files

### System Limitations

- Maximum 4 concurrent open files
- Maximum 1024 bytes per read/write operation
- Maximum 25 directory entries per request
- Session IDs limited to 1-15
- Message sequence numbers limited to 1-7 per session
