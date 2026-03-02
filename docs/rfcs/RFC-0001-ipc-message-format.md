# RFC-0001: IPC Message Format

- **Status**: Accepted
- **Author**: UNHU Project
- **Date**: 2026-03-01

## Summary

This RFC defines the binary format of Mach IPC messages in UNHU, covering
the message header, inline data, and (future) out-of-line descriptors. The
format follows CMU Mach 3.0 conventions to maintain compatibility with MIG
(Mach Interface Generator) and existing Mach documentation.

## Motivation

Every inter-process interaction in UNHU uses Mach messages. A well-defined,
stable message format is essential for:

1. Server interoperability (BSD, VFS, device servers must agree on wire format)
2. MIG compatibility (auto-generated stubs depend on header layout)
3. Debugging (tools need to parse messages for tracing)
4. Performance (layout affects copy costs and cache behavior)

## Message Header

Every message begins with a `mach_msg_header_t`:

```c
typedef struct {
    mach_msg_bits_t      msgh_bits;         /* right bits + flags       */
    mach_msg_size_t      msgh_size;         /* total message size       */
    mach_port_name_t     msgh_remote_port;  /* destination port name    */
    mach_port_name_t     msgh_local_port;   /* reply port name          */
    mach_msg_size_t      msgh_reserved;     /* reserved (padding)       */
    mach_msg_id_t        msgh_id;           /* operation identifier     */
} mach_msg_header_t;
```

### Field Details

**msgh_bits** (32-bit): Encodes the types of port rights being sent:
- Bits 0–7: remote port disposition (MACH_MSG_TYPE_COPY_SEND, etc.)
- Bits 8–15: local port disposition
- Bit 31: MACH_MSGH_BITS_COMPLEX (message contains descriptors)

**msgh_size** (32-bit): Total size of the message in bytes, including the
header and all inline data. Must be ≥ `sizeof(mach_msg_header_t)` (24 bytes).

**msgh_remote_port**: Port name in the sender's name space identifying the
destination port. The kernel translates this to a kernel port pointer.

**msgh_local_port**: Port name in the sender's name space identifying the
reply port. The kernel inserts a send right to this port into the receiver's
name space, allowing the receiver to reply.

**msgh_reserved**: Must be zero. Ensures 8-byte alignment of the body.

**msgh_id**: Operation identifier. By convention, servers use this to
dispatch incoming messages to handler functions. MIG assigns sequential IDs
starting from a base number per subsystem.

## Message Body (Inline Data)

Inline data follows immediately after the header:

```
┌──────────────────────────────────┐  offset 0
│  mach_msg_header_t  (24 bytes)   │
├──────────────────────────────────┤  offset 24
│  Inline body data                │
│  (variable length, up to         │
│   msgh_size - sizeof(header))    │
└──────────────────────────────────┘
```

Inline data is copied by the kernel on send and receive. The maximum inline
size in Phase 1 is 1000 bytes (1024 total - 24 header).

## Complex Messages (Phase 2)

When `msgh_bits` has `MACH_MSGH_BITS_COMPLEX` set, the body begins with a
descriptor count followed by typed descriptors:

```c
typedef struct {
    mach_msg_size_t  msgh_descriptor_count;
} mach_msg_body_t;

/* Out-of-line memory descriptor */
typedef struct {
    void              *address;       /* VM address in sender's space  */
    mach_msg_size_t    size;          /* region size in bytes          */
    boolean_t          deallocate;    /* deallocate from sender?       */
    mach_msg_type_t    type;          /* OOL_DESCRIPTOR                */
} mach_msg_ool_descriptor_t;

/* Port descriptor (right transfer) */
typedef struct {
    mach_port_name_t   name;          /* port name in sender's space   */
    mach_msg_size_t    pad;
    mach_msg_type_t    disposition;   /* COPY_SEND, MOVE_SEND, etc.    */
    mach_msg_type_t    type;          /* PORT_DESCRIPTOR               */
} mach_msg_port_descriptor_t;
```

## Size Limits

| Parameter | Phase 1 | Phase 2 (Planned) |
|-----------|---------|-------------------|
| Max inline message | 1024 bytes | 1024 bytes |
| Max OOL transfer | N/A | Limited by VM |
| Max descriptors | N/A | 256 |
| Queue depth | 16 messages | Configurable per-port |

## Alignment

- The header is naturally aligned (all 32-bit fields).
- Inline body data starts at a 4-byte aligned offset (byte 24).
- Complex message descriptors must be 8-byte aligned.

## Type Definitions

```c
typedef uint32_t  mach_msg_bits_t;
typedef uint32_t  mach_msg_size_t;
typedef int32_t   mach_msg_id_t;
typedef uint32_t  mach_port_name_t;
typedef int32_t   kern_return_t;
typedef uint32_t  mach_msg_return_t;
```

## Error Codes

Send errors:
- `MACH_SEND_MSG_TOO_SMALL` — message smaller than header
- `MACH_SEND_TOO_LARGE` — message exceeds max inline size
- `MACH_SEND_NO_BUFFER` — destination queue is full
- `MACH_SEND_INVALID_DEST` — no send right to destination

Receive errors:
- `MACH_RCV_TOO_LARGE` — no message available (Phase 1) / buffer too small
- `MACH_RCV_INVALID_NAME` — port name not found in receiver's space

## Compatibility

This format is intentionally compatible with the CMU Mach 3.0 and XNU message
layouts. Existing MIG definitions and Mach documentation should apply directly.

## References

- Accetta et al., "Mach: A New Kernel Foundation for UNIX Development" (1986)
- CMU Mach 3.0 Kernel Interface Reference, Chapter 5: IPC
- OSF Mach Kernel Principles, Chapter 4: Interprocess Communication
- XNU `osfmk/mach/message.h`
