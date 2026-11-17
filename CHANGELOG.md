# Changelog

## 0.1.2

- Added a fast profile for a positively identified Switch Lite.
- Kept the conservative delayed profile for other or unidentified models.
- Rechecked controller identity, PD contract, and dock indicators before sending a role-switch request.
- A request is sent at most once per connection; ambiguous state fails closed.

## 0.1.1

- Fixed dock detection for an ordinary charger whose DP status contained only the INSERT bit.
- Added regression coverage for the other DP status bits.

## 0.1.0

- Initial optional sysmodule release with configurable timing and logging.
