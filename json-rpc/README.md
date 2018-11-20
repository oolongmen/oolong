A simple JSON-RPC server

[JSON](http://json.org/) is a lightweight data-interchange format. It can represent numbers, strings, ordered sequences of values, and collections of name/value pairs.

[JSON-RPC](http://www.jsonrpc.org/specification) is a stateless, light-weight remote procedure call (RPC) protocol. Primarily this specification defines several data structures and the rules around their processing. It is transport agnostic in that the concepts can be used within the same process, over sockets, over HTTP, or in many various message passing environments. It uses JSON ([RFC 4627](http://www.ietf.org/rfc/rfc4627.txt)) as data format.

# Message Structure

All RPC messages must start with a 2-byte header represent the length of message payload (in network-oriented format).
Since header is only 2 bytes, the maximum length of payload is therefore limited to 65536.

Message: [ 2-Byte Length (big-endian) ] [ JSON-RPC (payload) ]
