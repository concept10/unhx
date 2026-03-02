# servers/auth/

Auth server — capability-based authentication via Mach port rights.

## Responsibility

A port right is authority. The auth server manages identity tokens and
capability delegation following HURD's auth model.

## Implementation Plan

- [ ] Identity token creation and verification
- [ ] Port right delegation model for authentication
- [ ] `getauth` / `setauth` protocol
- [ ] Integration with BSD server credential model
- [ ] Group membership and privilege escalation protocol

## References

- GNU HURD `auth/` server
- EROS capability model (background reading)
