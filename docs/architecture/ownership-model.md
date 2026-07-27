# Quarry Ownership Model

## Status

Draft

## Purpose

This document defines how Quarry devices are owned, claimed, assigned, transferred, suspended, and retired.

Ownership determines who is allowed to approve a device, assign it to a fleet, configure it, receive telemetry, issue commands, and authorize OTA updates.

---

# Design Goals

* Support single-user, small-fleet, and organization-based deployments.
* Keep device identity independent from ownership.
* Allow a device to exist before it is owned.
* Allow ownership transfer without changing device identity.
* Support fleet assignment as an operational grouping, not as the primary ownership mechanism.
* Preserve audit history across ownership changes.

---

# Core Concepts

## Device

A physical or virtual endpoint running the Quarry Agent.

A device has a stable deviceId.

A device may optionally have:

* `hardwareId`
* `assetId`

The device identity is defined in `device-identity.md`.

---

## Owner

The entity responsible for a device.

In Quarry v0.1, an owner may be:

* a user
* an organization

Ownership controls administrative authority over the device.

---

## Organization

A group of users that manages devices together.

An organization may contain:

* users
* fleets
* devices
* policies

For single-user deployments, Quarry may create a default personal organization.

---

## Fleet

A logical operational grouping of devices.

A fleet does not own devices directly.

A fleet is used for:

* telemetry views
* OTA targeting
* policy assignment
* reporting
* operational grouping

A device may belong to zero or more fleets.

---

## Asset

A business object represented by a device.

Examples:

* truck
* trailer
* container
* generator
* test bench
* development board

An asset may have a human-readable `assetId`.

A device may be attached to an asset, but the asset is not the device identity.

---

# Ownership States

## UNCLAIMED

The device is known to the cloud but has no owner.

Typical source:

* self-registration
* first boot
* lab discovery

Restrictions:

* no trusted telemetry
* no commands
* no OTA
* no fleet assignment

---

## CLAIMED

The device has an owner.

Characteristics:

* owner is assigned
* administrative authority is established
* device may proceed with provisioning

---

## ASSIGNED

The device is associated with one or more fleets or assets.

Characteristics:

* owner remains unchanged
* operational grouping is established
* policies may be applied

---

## TRANSFER_PENDING

Ownership transfer has been requested but not completed.

Typical reasons:

* asset sale
* device reassignment
* organization migration

Restrictions:

* destructive operations should be blocked
* certificate renewal may be blocked depending on policy

---

## SUSPENDED

Owner has temporarily disabled the device.

Characteristics:

* ownership remains unchanged
* device identity remains valid
* operational access is limited

---

## RETIRED

The device is permanently removed from active service.

Characteristics:

* owner relationship is retained for audit
* operational credentials are revoked
* telemetry history is preserved

---

# Ownership Rules

* A device SHALL have exactly one owner when in normal managed operation.
* A device MAY exist without an owner while in the unclaimed state.
* A device MAY belong to zero or more fleets.
* Fleet membership SHALL NOT imply ownership.
* Ownership transfer SHALL NOT change deviceId.
* Asset assignment SHALL NOT change deviceId.
* Retiring a device SHALL NOT delete historical ownership records.

---

# Claiming Flow

1. Device self-registers with Quarry Cloud.
2. Cloud creates an unclaimed device record.
3. User or organization administrator reviews the device.
4. Administrator claims the device.
5. Device becomes owned by the selected user or organization.
6. Device becomes eligible for provisioning.

Transition:

UNCLAIMED → CLAIMED

---

# Fleet Assignment Flow

1. Device is already claimed.
2. Administrator selects one or more fleets.
3. Cloud records fleet membership.
4. Fleet-level policies may be applied.

Transition:

CLAIMED → ASSIGNED

Fleet assignment may also happen after the device is already managed.

---

# Ownership Transfer Flow

1. Current owner initiates transfer.
2. Target owner accepts transfer.
3. Cloud records ownership change.
4. Policies and fleet memberships are reevaluated.
5. Ownership transfer may require reprovisioning depending on policy.
6. Reprovisioning may issue a new certificate, but certificate replacement shall
   not change deviceId.

Transition:

CLAIMED → TRANSFER_PENDING → CLAIMED

The deviceId remains unchanged during transfer.

---

# Suspension Flow

1. Owner or administrator suspends the device.
2. Device remains known and owned.
3. Cloud limits operational access.

Transition:

CLAIMED or ASSIGNED → SUSPENDED

A suspended device may later be restored.

---

# Retirement Flow

1. Owner retires the device.
2. Cloud revokes operational credentials.
3. Device is removed from active fleets.
4. Historical records are preserved.

Transition:

CLAIMED, ASSIGNED, or SUSPENDED → RETIRED

QUARANTINED is a device-lifecycle state, not an ownership state (see
`device-lifecycle.md`), so it is not enumerated as an ownership transition
source here; a device's lifecycle state does not restrict which ownership
states may transition to RETIRED.

---

# Authorization Implications

Ownership affects authorization.

Only the owner or an authorized organization member may:

* approve provisioning
* assign fleets
* update configuration
* issue commands
* authorize OTA
* suspend the device
* retire the device

Fleet membership may grant operational permissions, but it does not replace ownership.

---

# Relationship to Device Lifecycle

Ownership is part of the broader device lifecycle.

Typical sequence:

REGISTERED
→ UNCLAIMED
→ CLAIMED
→ PROVISIONED
→ MANAGED

A device shall not enter normal MANAGED operation until ownership has been established.

Ownership state and device lifecycle state are tracked independently. This
document's Ownership States govern administrative control (who may approve,
assign, configure, and retire a device); `device-lifecycle.md`'s device
states govern trust, connectivity, and security posture. Not every lifecycle
state has a corresponding ownership state — for example, QUARANTINED
(security-related isolation) is lifecycle-only and does not appear in the
Ownership States list or in any ownership transition above.

---

# Open Questions

* Should v0.1 support multiple organizations?
* Should devices support multiple simultaneous owners?
* Should fleet membership be single or multiple in v0.1?
* Should ownership transfer require reprovisioning?
* Should asset assignment be modeled separately from fleet assignment?
