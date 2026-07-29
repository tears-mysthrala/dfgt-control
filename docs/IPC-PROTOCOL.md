# DFGT Control local IPC protocol

## Trust boundary

Protocol v4 uses the per-user local named pipes
`\\.\pipe\DfgtFfbBroker.v2` and
`\\.\pipe\DfgtFfbBroker.control.v2`. Remote clients are rejected and the pipe
ACL grants access only to SYSTEM, Administrators and the broker user's SID.
`FILE_FLAG_FIRST_PIPE_INSTANCE` prevents a second local server instance.

This protects against accidental or cross-user injection. It does not claim
isolation from malware already running with the same user token.

The provider and agent additionally verify:

- the server PID reported by the named pipe;
- that its token belongs to the same user;
- that its image is `DfgtFfbBroker.exe`;
- that it resides beside the x64 binaries, allowing the provider's known
  sibling `x86` → `x64` layout.

The broker records the client PID and Windows session and watches the process
handle. Control clients must be in the broker's interactive session.

## Wire header

Every frame begins with this 48-byte little-endian header. It is serialized
field-by-field; C++ object layout and padding are never transmitted.

| Offset | Size | Field |
|---:|---:|---|
| 0 | 4 | Magic (`DFGT` or `DFGC`) |
| 4 | 2 | Protocol version |
| 6 | 2 | Header size, exactly 48 |
| 8 | 4 | Total frame size |
| 12 | 4 | Payload length |
| 16 | 4 | Command, zero in responses |
| 20 | 4 | Monotonic sequence |
| 24 | 8 | Capability bits |
| 32 | 8 | Session nonce |
| 40 | 8 | Build ID |

FFB request frames are exactly 1,104 bytes and responses 68 bytes. Control
requests are exactly 1,228 bytes and responses 240 bytes. UTF-16 strings have
fixed declared bounds and must contain a terminator when the command consumes
them.

## Handshake and replay handling

The first FFB `Open` and first control `Ping` carry nonce zero. The broker
returns a cryptographically random 64-bit nonce. Every later request and
response must echo it. Build ID and all required capability bits must match.

Sequences are strictly increasing per connection. Repeated or regressing
sequences, incorrect nonces, unknown commands, wrong versions, incorrect
lengths and unsupported capabilities fail closed and close the session.

The mandatory capability bits currently identify:

- broker safety-state reporting;
- isolated physical slots;
- renderer epoch health.

An incompatibility therefore produces a handshake failure instead of allowing
an old provider and new broker to interpret each other's payload.

## Bounded transport

All pipe reads, writes and connects use overlapped I/O, deadlines and
`CancelIoEx`. A partial frame cannot retain the single pipe instance: the
broker evicts it after the bounded read window. The
`broker-framing-integration` test starts a no-force broker, sends a one-byte
frame and proves that a valid second client can connect.

## Validation evidence

`protocol-vectors` checks exact golden header bytes, sizes, round trips and
corrupted version/length rejection in both x86 and x64 builds.
`broker-framing-integration` exercises the real broker without a HID path and
cannot produce physical force.
