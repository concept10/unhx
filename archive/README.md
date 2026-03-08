# archive/

Historical source references and research archive.

This directory contains or references all historical sources that NEOMACH draws
from. Everything here is reference material — no new code is written here.

## Contents

| Directory     | Source | What We Take |
|--------------|--------|--------------|
| `cmu-mach/`  | CMU Mach 3.0 | Core IPC, VM, task/thread model reference |
| `osf-mk/`    | OSF MK6/MK7 | NORMA distributed IPC, SMP, RT scheduling |
| `utah-oskit/`| Utah OSKit + Lites | OS component framework, BSD server on Mach |
| `next-docs/` | NeXTSTEP/OPENSTEP documentation | API reference, DPS, framework architecture |

## External References (not mirrored here)

| Source | URL | Notes |
|--------|-----|-------|
| GNU Mach | https://git.savannah.gnu.org/git/hurd/gnumach.git | Active, Linux driver compat layer |
| GNU HURD | https://git.savannah.gnu.org/git/hurd/hurd.git | Server architecture, translators |
| XNU | https://github.com/apple-oss-distributions/xnu | Modern osfmk/, SMP, ARM64 |
| MkLinux | Apple/OSF archive | Linux-on-Mach architecture reference |

## Documentation Obligation

Per design principle 7.5: every source, its license, and its lineage must be
documented before writing implementation code that draws from it.
