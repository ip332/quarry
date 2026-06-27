# Bootstrap

Bootstrap is the local-only initialization step that creates the device's local
Breadcrumbs identity material.

This document is the home for device-local startup behavior that must be
portable across Linux, RTOS, and bare-metal implementations without changing the
provisioning flow.

Bootstrap has no cloud dependencies.

No cloud trust is established during bootstrap.

No network request is required to complete bootstrap.

## Responsibilities

Bootstrap is responsible for:

* generating a UUIDv7 deviceId when none exists
* persisting the deviceId locally
* loading optional hardwareId
* loading optional assetId
* generating the device key pair
* persisting local identity material
* preparing a registration request

The registration request is prepared as local output only. It is not sent during
bootstrap.

## Steps

* Generate a UUIDv7 deviceId if none exists.
* Persist the deviceId locally.
* Load optional hardwareId.
* Load optional assetId.
* Generate device key pair.
* Persist identity material locally.
* Prepare the registration request.

## Output

* deviceId.
* public/private key pair.
* registration request.

## Failure Behavior

Bootstrap succeeds only after the required local identity material has been
generated and persisted.

If bootstrap fails, the device remains uninitialized.

Bootstrap failure shall not create a cloud device record, establish ownership, or
grant operational privileges.

## Boundary

Bootstrap ends when the device has local identity material and a prepared
registration request.

Sending the registration request is part of provisioning, not bootstrap.

---

# Relationship to Other Documents

Related architecture documents:

* `device-identity.md` defines deviceId, hardwareId, and assetId.
* `provisioning-model.md` defines when the prepared registration request is sent.
* `device-lifecycle.md` defines the UNINITIALIZED and BOOTSTRAPPED states.
