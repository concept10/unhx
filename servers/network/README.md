# servers/network/

Network stack server — TCP/IP as a Mach server.

## Responsibility

Provide a POSIX socket API backed by a Mach-message based network stack.

## Implementation Plan

- [ ] Integrate lwIP or picoTCP as the network stack implementation
- [ ] Expose socket operations via Mach IPC
- [ ] BSD server bridge — translate `socket()`, `bind()`, `connect()` etc. to network server messages
- [ ] virtio-net driver integration (via device server)
- [ ] Ethernet + IPv4 + TCP + UDP minimum viable stack
