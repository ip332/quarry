# Quarry Device Identity

## Status

Draft

## Purpose

This document defines the identity model used by Quarry.

The identity model provides a stable, globally unique identifier for every managed device while supporting optional hardware- and business-specific identifiers.

This document intentionally separates device identity from authentication and authorization mechanisms, which are described in the Security Architecture.

---

# Design Goals

* Every device shall have a globally unique identifier.
* Device identity shall remain stable throughout the device lifetime.
* Device identity shall be independent of the underlying hardware whenever possible.
* Hardware-specific identifiers should improve enrollment and inventory management but should not define operational identity.
* Business identifiers should remain independent from technical identifiers.

---

# Identity Attributes

Every Quarry device may expose three identifiers.

| Attribute  | Required | Mutable | Purpose                               |
| ---------- | -------- | ------- | ------------------------------------- |
| deviceId   | Yes      | No      | Primary Quarry device identifier |
| hardwareId | No       | Rarely  | Platform or hardware identifier       |
| assetId    | No       | Yes     | Human-readable business identifier    |

---

# deviceId

The deviceId is the canonical identity of a Quarry device.

Properties:

* Required
* Globally unique
* Immutable
* UUIDv7
* Generated during first bootstrap
* Persisted locally
* Used as the primary identifier in the cloud

Example:

```
01978a4d-5d8a-7a32-8f12-b8e1db8f4a33
```

The deviceId SHALL NOT change during the lifetime of the device.

---

# hardwareId

The hardwareId is an optional identifier supplied by the application or platform.

Examples include:

* TPM identity
* CPU serial number
* MCU unique identifier
* Board serial number
* Vendor-specific hardware identifier

Examples:

```
stm32:003B004B3437511635363536

raspberrypi:10000000abcd1234

tpm:2.0:81C5...
```

The Quarry Agent SHOULD request a hardware identifier from the application or platform during bootstrap.

If unavailable, the hardwareId shall remain empty.

The cloud SHALL NOT use the hardwareId as the primary identity of the device.

Typical uses:

* Inventory
* Enrollment assistance
* Device recovery
* Duplicate detection

---

# assetId

The assetId is an optional business identifier assigned by the operator.

Examples:

```
Truck-42

WeatherStation-01

Trailer-0158
```

The assetId:

* may change
* is not globally unique
* is intended for human operators

Typical uses:

* Fleet management
* Search
* Reporting
* Business integration

---

# Registration Payload

Example registration request:

```json
{
  "deviceId": "01978a4d-5d8a-7a32-8f12-b8e1db8f4a33",
  "hardwareId": "raspberrypi:10000000abcd1234",
  "assetId": "WeatherStation-01",
  "publicKey": "...",
  "softwareVersion": "0.1.0"
}
```

---

# Identity Lifecycle

The deviceId is generated and persisted during local bootstrap.

Registration submits the deviceId, optional identifiers, and public key to
Quarry Cloud.

Detailed bootstrap behavior is defined in `bootstrap.md`.

The registration and provisioning flow is defined in `provisioning-model.md`.

---

# Design Rules

* deviceId SHALL be the canonical Quarry identity.
* hardwareId SHALL be optional.
* assetId SHALL be optional.
* deviceId SHALL NOT change after bootstrap.
* hardwareId MAY change if hardware changes.
* assetId MAY change during normal operation.

Operational trust is established using the device certificate bound to the deviceId rather than any hardware identifier.

---

# Relationship to Other Documents

Related architecture documents:

* `bootstrap.md` defines local device identity initialization.
* `provisioning-model.md` defines registration and provisioning.
* `security-architecture.md` defines certificate binding and operational trust.

---

# Future Extensions

Future versions may support:

* Multiple hardware identifiers
* TPM-backed identities
* Secure Element integration
* Device aliases
* Identity federation
* Ownership transfer
* Hardware attestation
