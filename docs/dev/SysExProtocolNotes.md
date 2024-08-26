##### How the SysEx protocol works

Messages consist of a SysEx "packet" which has a header, a sequence number, a JSON body, an optional 0 separator and packed binary part.

Packed binary is used to transfer file content to or from the Deluge.  So far only the read and write requests use it.

The packed binary part, if present, is encoded in 7 byte to 8 byte format. In 7 to 8 format, we send the information about a 7 byte-long block as 8 bytes, with the first byte in the group holding the sign bits of the following 7 bytes. Those following 7 bytes have their high-order bit masked-off, resulting in valid SysEx data.

The JSON body is always a JSON object with one key/value pair. The key is the request code and the value contains the parameters for the request. We did things this way so that the order-of-appearance of keys does not matter.

An example request:

{"open": {"path": "/SONGS/SONG004.XML", "write": 1}}

And a response:

{"^open": {"fid": 2, "size": 0, "err": 0}}

The caret in front of the response is used as a convention, but is not required. The way responses are matched up with requests is by using a sequence number that wraps from 1 to 127. The web side keeps track of sequence numbers and stores a callback function for each outgoing message to handle the reply.

The Deluge can initiate a message exchange by sending a request to the web-side using a sequence number of 0. This is not actually being used yet.

The code handling the basic request/response stuff is in JsonReplyHandler.js. The file transfer protocols are in FileRoutines.js. The general principal used is "do the maximum possible on the web side" and "be as stateless as possible on the Deluge side. The only state so far is a small table that relates file IDs to a particular open file information blocks. We do keep track of where we are in fulfilling directory data requests, but if we ask out of order, its just a cache miss.

Directory requests and read/write are examples of requests that only handle a small block of data at a time. The web side keeps track of the offsets into the file or directory involved.

So far, the file protocol has:
	open
	close
	dir
	read
	write
	delete
	mkdir
	rename
	ping

The file routines include:

	readFile
	writeFile
	recursiveDelete
	downloadOneItem
	recursiveDownload
	getDirInfo

The file routines work with Uint8Array objects. The assumption is that we have an enormous amount of free memory on the web side. Downloading uses the web File System API and can handle nested directories.
