# Breadcrumbs Device Lifecycle

## Status

Draft

## Purpose

This document defines the lifecycle of a Breadcrumbs device from first boot through retirement.

The lifecycle model is used to define:

* Device identity
* Device ownership
* Device provisioning
* Certificate management
* Authorization
* Fleet assignment
* OTA eligibility

This document intentionally avoids protocol and implementation details, which are defined in separate specifications.

---

# Design Goals

* Minimize manufacturing-time provisioning requirements.
* Allow onboarding of generic POSIX devices.
* Use mutual TLS (mTLS) during normal operation.
* Support stronger identity models such as TPM, TEE, and HSM in future releases.
* Prevent untrusted devices from participating in fleet operations.
* Support both open-source and enterprise deployment models.

---

# Device States

## UNINITIALIZED

The device has no Breadcrumbs identity.

Characteristics:

* No device identifier
* No device certificate
* No owner
* No fleet assignment

Entry conditions:

* Fresh installation
* Factory reset
* Identity store erased

---

## BOOTSTRAPPED

The device has established a local identity.

Characteristics:

* Local key pair generated
* Device identifier generated
* No cloud trust established

Entry conditions:

* Successful local bootstrap

Exit conditions:

* Registration request sent

---

## REGISTERED

The cloud is aware of the device.

Characteristics:

* Device record exists in cloud
* Device public key known
* Device not yet trusted

Restrictions:

* No telemetry upload
* No OTA
* No command execution

Allowed operations:

* Registration status queries
* Certificate enrollment

---

## APPROVED

The device has been approved by an administrator or enrollment policy.

Characteristics:

* Ownership established
* Fleet assignment established
* Eligible for certificate issuance

---

## PROVISIONED

The device has received operational credentials.

Characteristics:

* Device certificate installed
* Configuration received
* Fleet membership assigned

Restrictions:

* Device must successfully establish mTLS before entering normal operation

---

## MANAGED

Normal operating state.

Characteristics:

* mTLS active
* Telemetry enabled
* Commands enabled
* OTA enabled

Allowed operations:

* Telemetry upload
* Configuration synchronization
* Command execution
* OTA updates
* Diagnostics

---

## SUSPENDED

Administrative suspension.

Characteristics:

* Identity preserved
* Certificate may remain valid
* Fleet participation restricted

Typical reasons:

* Billing issues
* Policy violations
* Temporary administrative action

---

## QUARANTINED

Security-related isolation.

Characteristics:

* Restricted connectivity
* No command execution
* No OTA deployment

Typical reasons:

* Compromised credentials
* Unexpected behavior
* Security investigation

---

## RETIRED

Permanent end-of-life state.

Characteristics:

* Credentials revoked
* Fleet membership removed
* Device record retained for audit purposes

Typical reasons:

* Device replacement
* Asset disposal
* Ownership transfer

---

# Lifecycle Diagram

UNINITIALIZED
→ BOOTSTRAPPED
→ REGISTERED
→ APPROVED
→ PROVISIONED
→ MANAGED

MANAGED
→ SUSPENDED
→ MANAGED

MANAGED
→ QUARANTINED
→ MANAGED

MANAGED
→ RETIRED

---

# Initial Registration Flow

## Step 1

The agent starts.

If no local identity exists:

* Generate device key pair.
* Generate device identifier.

Transition:

UNINITIALIZED → BOOTSTRAPPED

## Step 2

The agent establishes a TLS connection using server authentication only.

The device validates the cloud certificate.

The cloud does not yet authenticate the device.

## Step 3

The device sends a registration request containing:

* Device identifier
* Public key
* Device metadata
* Software version

Transition:

BOOTSTRAPPED → REGISTERED

## Step 4

The cloud evaluates enrollment policy.

Examples:

* Manual approval
* Enrollment token
* Organization policy
* Manufacturer trust

Transition:

REGISTERED → APPROVED

## Step 5

The cloud issues a device certificate.

The device installs the certificate.

Transition:

APPROVED → PROVISIONED

## Step 6

The device reconnects using mutual TLS.

Successful authentication establishes trusted operation.

Transition:

PROVISIONED → MANAGED

---

# Security Model

The lifecycle intentionally separates:

* Transport security
* Device identity
* Device authorization

## First Boot

Transport:

* TLS
* Server authentication only

Purpose:

* Secure registration

## Normal Operation

Transport:

* Mutual TLS

Purpose:

* Device authentication
* Fleet authorization
* Operational trust

A device is not considered trusted until it successfully enters the MANAGED state.

---

# Future Extensions

Future releases may support:

* TPM-backed identities
* OP-TEE integration
* HSM-backed keys
* Manufacturer certificates
* Hardware attestation
* Ownership transfer workflows
* Multi-tenant fleet assignment
