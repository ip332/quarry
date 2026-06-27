# Breadcrumbs Security Architecture

## Status

Draft

## Purpose

This document defines the security architecture of the Breadcrumbs platform.

Security is treated as a cross-cutting concern and is intentionally separated from:

* Device Identity
* Ownership
* Provisioning
* Protocol
* OTA

Each of those documents references this architecture rather than defining independent security mechanisms.

---

# Security Goals

* Confidential communication
* Strong device authentication
* Clear separation between identity, authentication, and authorization
* Flexible deployment models
* Cryptographic agility
* Minimal manufacturing requirements
* Support for hardware-backed trust in future releases

---

# Security Layers

Breadcrumbs separates security into four independent layers.

## Layer 1 – Transport Security

### Purpose

Protect communication against eavesdropping and modification.

### Mechanism

TLS 1.3

### Requirements

* Server authentication is required for every connection.
* Modern cipher suites only.
* TLS protects the communication channel but does not establish device trust.

---

## Layer 2 – Device Authentication

### Purpose

Authenticate the device.

### Mechanism

Mutual TLS (mTLS)

The canonical identity of a Breadcrumbs device is the immutable deviceId.

The device certificate binds the device's cryptographic key pair to the deviceId.

The certificate is used to authenticate the device during operational communication.

---

## Layer 3 – Authorization

### Purpose

Determine what an authenticated device is allowed to do.

Authorization decisions are based on:

* ownership
* organization
* fleet membership
* assigned policies

Authentication and authorization are intentionally independent.

Future versions may introduce short-lived authorization tokens for cloud APIs.

---

## Layer 4 – Application Security

### Purpose

Protect application-level operations.

Examples include:

* OTA artifacts
* configuration
* commands
* diagnostics

Protection mechanisms may include:

* digital signatures
* integrity validation
* replay protection
* message authentication

---

# Trust Decisions

Breadcrumbs intentionally separates:

* transport security
* authentication
* authorization
* operational trust

The following statements are fundamental architectural principles:

* A secure TLS connection does not imply device trust.
* Successful device authentication does not imply authorization.
* Authorization does not imply ownership.
* Operational trust is established only after successful provisioning and mutual TLS authentication.

---

# Device Trust Lifecycle

## Initial Registration

Connection:

TLS

Authentication:

Server authentication only

Purpose:

Secure bootstrap and registration.

The device is not yet trusted.

---

## Managed Device

Connection:

Mutual TLS

Authentication:

Server and device

Purpose:

Trusted operational communication.

Only managed devices may:

* upload trusted telemetry
* receive commands
* participate in OTA campaigns
* receive policy updates

---

# Cryptographic Material

Each Breadcrumbs device owns:

* immutable UUIDv7 deviceId
* private key
* public key
* device certificate

The private key is generated and stored locally on the device.

The private key SHALL NOT be transmitted outside the device.

Future implementations may protect the private key using:

* TPM
* Secure Element
* OP-TEE
* HSM

---

# Certificate Authority

Initial implementation:

Breadcrumbs Certificate Authority.

Future implementations may support:

* Enterprise CA
* Customer CA
* Manufacturer CA

---

# Certificate Lifecycle

## Certificate Issuance

Certificates are issued during provisioning.

The issued certificate binds the device's public key to its immutable deviceId.

---

## Certificate Lifetime

Device certificates are intended to be long-lived.

Certificates are not renewed periodically.

Certificate replacement may occur when:

* the device is reprovisioned
* the Certificate Authority changes
* cryptographic algorithms are upgraded
* ownership policies require replacement
* certificate compromise is suspected

Certificate replacement SHALL NOT change the deviceId.

---

## Certificate Revocation

Certificates shall be revoked when:

* certificate compromise is detected
* the device is retired
* the Certificate Authority is compromised

A compromised certificate SHALL NOT authorize issuance of its own replacement.

If a certificate is compromised:

1. The certificate is revoked.
2. The device enters the QUARANTINED state.
3. The device must repeat the registration and provisioning process.
4. A new key pair and certificate may be generated.
5. The deviceId remains unchanged.

---

# Trust Anchors

The Breadcrumbs Agent maintains:

* trusted root certificates
* intermediate certificates

Future versions may support additional certificate revocation mechanisms.

---

# Key Generation

Initial implementation:

Software-generated key pair.

Future implementations may support:

* TPM-backed keys
* OP-TEE
* Secure Elements
* Hardware Security Modules

The Breadcrumbs architecture does not require hardware-backed cryptography.

---

# Threat Model

Breadcrumbs is designed to protect against:

* passive network monitoring
* active man-in-the-middle attacks
* replay attacks
* stolen certificates
* unauthorized device cloning
* rogue devices
* unauthorized cloud access

Out of scope:

* physical hardware compromise
* side-channel attacks
* nation-state adversaries

---

# Security Principles

* Device identity is independent from hardware identity.
* Identity is not authentication.
* Authentication is not authorization.
* Device certificates authenticate devices.
* The immutable deviceId identifies devices.
* Hardware identifiers assist enrollment but do not establish trust.
* Trust is established through provisioning.
* Trust is maintained through mutual TLS.
* Every privileged operation shall be authorized.
* Every security decision shall be auditable.

---

# Relationship to Other Documents

Related architecture documents:

* `device-identity.md` defines deviceId and optional hardware and asset identifiers.
* `provisioning-model.md` defines registration, approval, and operational provisioning.
* `ownership-model.md` defines administrative authority and authorization context.
* `device-lifecycle.md` defines security-relevant lifecycle states.

---

# Future Enhancements

Future releases may support:

* TPM-backed identities
* hardware attestation
* post-quantum cryptography
* alternative trust anchors
* multiple Certificate Authorities
* authorization tokens
* confidential computing
* secure boot integration
* certificate pinning
