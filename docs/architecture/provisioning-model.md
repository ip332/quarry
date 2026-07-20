# Quarry Provisioning Model

## Status

Draft

## Purpose

This document defines how a Quarry device becomes a trusted managed device.

Provisioning establishes operational trust between the Quarry Agent and Quarry Cloud.

Provisioning is independent of manufacturing and is performed after the device has network connectivity.

---

# Design Goals

* No mandatory factory provisioning
* Minimal first-boot experience
* Strong operational security
* Support both hobby and enterprise deployments
* Support future TPM/HSM integration
* Allow automated enrollment where appropriate

---

# Provisioning Overview

Provisioning consists of four phases:

1. Local bootstrap
2. Registration
3. Approval
4. Operational Provisioning

After provisioning completes, the device operates using mutual TLS.

---

# Local Bootstrap Boundary

Local bootstrap is performed entirely on the device and has no cloud
dependencies. Its behavior is defined in `bootstrap.md`.

Provisioning begins after bootstrap has produced:

* deviceId
* public/private key pair
* registration request

---

# Registration

The device establishes a TLS connection using server authentication only.

The cloud authenticates itself.

The device is not yet authenticated.

The agent submits:

* deviceId
* public key
* hardwareId (optional)
* assetId (optional)
* software version
* platform information

The cloud creates a device in the REGISTERED state.

---

# Approval

The cloud evaluates enrollment policy.

Possible approval mechanisms:

* Manual administrator approval
* Enrollment token
* Organization policy
* Manufacturer policy (future)

Successful approval establishes ownership.

The device enters the APPROVED state.

---

# Operational Provisioning

After approval, the cloud provisions the device.

Provisioned data may include:

* Device certificate
* Certificate chain
* Trust anchors
* Initial configuration
* Fleet membership
* Policy set
* API endpoints
* OTA configuration

The agent stores this information securely.

---

# Transition to Managed Operation

The agent reconnects using mutual TLS.

Successful mTLS authentication transitions the device into the MANAGED state.

Only managed devices may:

* Upload trusted telemetry
* Receive commands
* Participate in OTA campaigns
* Receive policy updates
* Join fleet operations

---

# Provisioning Modes

## Mode A — Open Source

Designed for individual developers.

Approval:

Manual

Identity:

Generated key pair

Authentication:

TLS → mTLS

---

## Mode B — Enrollment Token

Administrator creates a temporary enrollment token.

Registration requires both:

* valid token
* generated key pair

Suitable for small fleets.

---

## Mode C — Enterprise

Automatic enrollment based on organization policy.

Future enhancement.

---

## Mode D — Hardware Root of Trust

Future enhancement.

Examples:

* TPM
* OP-TEE
* Secure Element
* HSM

Private keys never leave hardware.

---

# Re-Provisioning

Re-provisioning may occur when:

* configuration changes
* certificate renewal
* ownership transfer
* fleet reassignment
* policy updates

Re-provisioning shall not change the deviceId.

---

# Failure Handling

Registration failures:

* Retry with exponential backoff.

Approval failures:

* Remain in REGISTERED state.

Provisioning failures:

* Retry provisioning.

mTLS failures:

* Return to PROVISIONED state until operational trust is restored.

---

# Security Rules

* First registration SHALL use TLS with server authentication only.
* Operational communication SHALL require mutual TLS.
* Device certificates SHALL be bound to the deviceId.
* Provisioning SHALL NOT change the deviceId.
* Devices SHALL NOT receive operational privileges before successful provisioning.

---

# Relationship to Other Documents

Related architecture documents:

* `bootstrap.md` defines local bootstrap and registration request preparation.
* `device-identity.md` defines deviceId and optional device identifiers.
* `ownership-model.md` defines ownership approval and transfer implications.
* `security-architecture.md` defines TLS, mTLS, certificates, and trust rules.

---

# Future Extensions

Future versions may support:

* Zero-touch provisioning
* Manufacturer-issued certificates
* Remote attestation
* Hardware-backed key generation
* Certificate rotation
* Multi-cloud provisioning
* Offline provisioning packages
