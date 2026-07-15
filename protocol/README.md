# Protocol

This directory is reserved for future transport or protocol integrations.

Binary Record Format byte mechanics currently live in `runtime/`. The runtime
support emits and structurally parses record bytes only; it does not define
transport framing, networking, storage protocols, checksums, compression, or
encryption.
