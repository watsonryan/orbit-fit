<!-- @brief Attribution and immutability rules for independent NASA and eph2tle fixtures. @author watson -->
# Independent reference fixtures

Curated by watson on 2026-10-24. Source data remain attributed to their original producers.

- `iss-oem-20261021.txt`: unchanged public NASA/JSC/FOD/TOPO ISS forecast, retrieved from the [NASA OEM endpoint](https://nasa-public-data.s3.amazonaws.com/iss-coords/current/ISS_OEM/ISS.OEM_J2K_EPH.txt). The original CRLF bytes are preserved. SHA256 `7c4f78b7f3e4bb5fafc210ab29f4e80bdae6dd66c84e27e8f47cd8f9e7976eb7`.
- `iss-teme.csv`: first 72 hours converted by watson's native C++ qualification tool from EME2000/km/km/s to TEME/m/m/s. SHA256 `0e1fe5e97b2a95737b8bcc5e77c3607347d4dd8bb95af264b8d66f719020e8a8`.
- `leap-seconds.list`: unchanged [IANA-distributed table](https://data.iana.org/time-zones/data/leap-seconds.list), with its original comments and attribution. Expiry 2027-06-28; TAI−UTC 37 s during the fixture. SHA256 `db5a895f16853b03bfc865e8d68f9fc8710ef1740e3400c701cd46a5bbbc3433`.
- `iss-{6,12,24,48}h-{6,7}p.tle`: two numerical output lines from the pinned external Project Pluto `eph2tle` executable. Six parameters hold BSTAR at zero; seven estimate it. Upstream output boilerplate is excluded; the original executable/source are not redistributed here. Author of the reference estimator: Bill Gray and Project Pluto contributors. Campaign setup/curation: watson.

The [qualification report](../../../qualification/README.md) records all commits, archive hashes, frame conventions, exact fitting controls, comparison limitations, and reproduction commands. `../check.cmake` checks every reference TLE plus the source and converted data. The normal CTest suite uses these frozen files without downloading anything.

These are forecast/estimator regression fixtures, not observed spacecraft truth. Do not replace them with today's moving NASA endpoint or prepend authorship comments to the TLEs; update provenance and checksums deliberately when introducing a new dataset.
