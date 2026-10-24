"""@brief Validate plotting inputs, match epochs, and project residuals into reference RTN.
@author watson

All numerical inputs are SI. RTN is defined by the supplied TEME position and
velocity at each observation, never by the fitted trajectory. This module
rotates position residual vectors only; it does not propagate or refit an orbit.
"""

from __future__ import annotations

import csv
import hashlib
import io
import json
import math
import re
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path
from typing import Any

import numpy as np
from numpy.typing import NDArray

MAX_BYTES = 64 * 1024 * 1024
MAX_SAMPLES = 100_000
VectorArray = NDArray[np.float64]


class InputError(ValueError):
    """@brief A missing, malformed, inconsistent, or unsupported plotting input."""


@dataclass(frozen=True)
class Source:
    """@brief Hash of the exact bytes parsed, without machine-specific absolute paths."""

    name: str
    sha256: str
    size_bytes: int


@dataclass(frozen=True)
class Metrics:
    """@brief Unweighted Euclidean RMS and maximum errors in SI units."""

    sample_count: int
    rms_position_m: float
    max_position_m: float
    rms_velocity_mps: float
    max_velocity_mps: float


@dataclass(frozen=True)
class Product:
    """@brief Physical product status; unavailable metrics remain absent, never zero."""

    name: str
    status: str
    accepted: bool
    metrics: Metrics | None


@dataclass(frozen=True)
class FitData:
    """@brief Matched continuous-fit residuals and the three delivery metric records."""

    start: datetime
    stop: datetime
    object_name: str
    catalog_number: int
    accepted: bool
    reason: str
    hours: VectorArray
    position_rtn_m: VectorArray
    velocity_norm_mps: VectorArray
    products: tuple[Product, ...]
    rms_gate_m: float
    sources: dict[str, Source]
    native: dict[str, Any]


@dataclass(frozen=True)
class QualificationData:
    """@brief Frozen ISS TLE RMS comparison, keyed by arc hours and fitted BSTAR."""

    arcs: tuple[int, ...]
    training: dict[tuple[int, bool], float]
    prediction: dict[tuple[int, bool], float]
    source: Source


def read_source(path: Path) -> tuple[bytes, Source]:
    """Read at most 64 MiB from a regular file and hash the consumed bytes."""
    if not path.is_file():
        raise InputError(f"Expected a regular input file: {path}")
    with path.open("rb") as stream:
        raw = stream.read(MAX_BYTES + 1)
    if len(raw) > MAX_BYTES:
        raise InputError(f"Input exceeds 64 MiB: {path}")
    return raw, Source(path.name, hashlib.sha256(raw).hexdigest(), len(raw))


def _unique(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
    """Reject duplicate JSON keys instead of silently keeping the last value."""
    result: dict[str, Any] = {}
    for key, value in pairs:
        if key in result:
            raise InputError(f"Duplicate JSON key: {key}")
        result[key] = value
    return result


def _constant(value: str) -> None:
    """Reject non-standard JSON NaN and infinity tokens."""
    raise InputError(f"Nonfinite JSON number: {value}")


def read_json(path: Path) -> tuple[dict[str, Any], Source]:
    """Read a JSON object, rejecting duplicates, nonfinite tokens, and bad UTF-8."""
    raw, source = read_source(path)
    try:
        value = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_unique,
            parse_constant=_constant,
            parse_float=lambda text: _number(float(text), "JSON number"),
        )
    except (ValueError, UnicodeError, RecursionError) as error:
        raise InputError(f"{path}: {error}") from error
    if not isinstance(value, dict):
        raise InputError(f"{path}: expected a JSON object")
    return value, source


def _number(value: Any, label: str, *, nonnegative: bool = False) -> float:
    """Require a finite JSON number; booleans and numeric strings are not numbers."""
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise InputError(f"{label}: expected a finite number")
    try:
        result = float(value)
    except OverflowError as error:
        raise InputError(f"{label}: number is out of range") from error
    if not math.isfinite(result) or (nonnegative and result < 0):
        raise InputError(f"{label}: expected a finite {'nonnegative ' if nonnegative else ''}number")
    return result


def _boolean(value: Any, label: str) -> bool:
    """Require an actual boolean rather than Python truthiness."""
    if not isinstance(value, bool):
        raise InputError(f"{label}: expected a boolean")
    return value


def epoch(value: Any) -> datetime:
    """Parse a civil UTC timestamp with at most six fractional digits; no leap labels."""
    if not isinstance(value, str) or not re.fullmatch(
        r"[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}(?:\.[0-9]{1,6})?Z", value
    ):
        raise InputError(f"Expected a microsecond-resolution UTC timestamp: {value!r}")
    try:
        return datetime.fromisoformat(value)
    except ValueError as error:
        raise InputError(f"Invalid UTC timestamp: {value!r}") from error


def _vector(value: Any, label: str) -> list[float]:
    """Require exactly three finite Cartesian components."""
    if not isinstance(value, list) or len(value) != 3:
        raise InputError(f"{label}: expected three components")
    return [_number(component, label) for component in value]


def _records(value: Any, label: str) -> tuple[list[datetime], VectorArray, VectorArray]:
    """Decode ordered state/residual arrays with a shared seven-scalar vocabulary."""
    if not isinstance(value, list) or not 3 <= len(value) <= MAX_SAMPLES:
        raise InputError(f"{label}: expected 3 to {MAX_SAMPLES} records")
    times, positions, velocities = [], [], []
    for row in value:
        times.append(epoch(row["epoch"]))
        if len(times) > 1 and times[-1] <= times[-2]:
            raise InputError(f"{label}: epochs must be strictly increasing")
        positions.append(_vector(row["position_m"], f"{label}.position_m"))
        velocities.append(_vector(row["velocity_mps"], f"{label}.velocity_mps"))
    return times, np.asarray(positions, dtype=float), np.asarray(velocities, dtype=float)


def norms(vectors: VectorArray) -> VectorArray:
    """Compute Euclidean row norms without squaring extreme finite coordinates."""
    result = np.hypot.reduce(vectors, axis=1)
    if not np.isfinite(result).all():
        raise InputError("Vector norm exceeds the supported floating-point range")
    return result


def project_rtn(position: VectorArray, velocity: VectorArray, residual: VectorArray) -> VectorArray:
    """Project delta-r onto R=r/|r|, N=(r×v)/|r×v|, T=N×R.

    Inputs have shape (N,3) in TEME/SI. Normalizing before the cross product
    avoids intermediate overflow. A zero state or nearly radial velocity
    (sine of flight-plane angle <= 1e-12) has no usable RTN frame and fails.
    This is a position-vector projection, not a rotating-frame velocity derivative.
    """
    if (
        position.ndim != 2
        or position.shape[1] != 3
        or position.shape != velocity.shape
        or position.shape != residual.shape
    ):
        raise InputError("RTN inputs must have matching (N,3) shapes")
    if not all(np.isfinite(array).all() for array in (position, velocity, residual)):
        raise InputError("RTN inputs must be finite")
    rnorm, vnorm = norms(position), norms(velocity)
    if np.any(rnorm == 0) or np.any(vnorm == 0):
        raise InputError("RTN is undefined for zero position or velocity")
    radial = position / rnorm[:, None]
    normal = np.cross(radial, velocity / vnorm[:, None])
    hnorm = norms(normal)
    if np.any(hnorm <= 1e-12):
        raise InputError("RTN is undefined for nearly parallel position and velocity")
    normal /= hnorm[:, None]
    transverse = np.cross(normal, radial)
    projected = np.column_stack(
        [np.einsum("ij,ij->i", residual, axis) for axis in (radial, transverse, normal)]
    )
    if not np.isfinite(projected).all():
        raise InputError("RTN projection overflow")
    return projected


def _metrics(value: Any, count: int) -> Metrics:
    """Validate physical metrics and their relationship to the sampled residuals."""
    if (
        not isinstance(value, dict)
        or type(value.get("sample_count")) is not int
        or value["sample_count"] != count
    ):
        raise InputError("Metric sample count does not match the fitting window")
    names = ("rms_position_m", "max_position_m", "rms_velocity_mps", "max_velocity_mps")
    numbers = [_number(value[name], name, nonnegative=True) for name in names]
    if numbers[0] > numbers[1] * (1 + 1e-12) or numbers[2] > numbers[3] * (1 + 1e-12):
        raise InputError("RMS cannot exceed the maximum error")
    return Metrics(count, *numbers)


def _rms(values: VectorArray) -> float:
    """Compute RMS with scaling to avoid overflow when summing squares."""
    scale = float(values.max())
    return scale * float(np.sqrt(np.mean((values / scale) ** 2))) if scale else 0.0


def load_fit(directory: Path) -> FitData:
    """Load a report.v2/request.v1 pair and validate epoch/identity/metric consistency.

    Files need not originate from an accepted run. Complete rejected candidates
    remain plottable and are labelled as rejected; incomplete cancelled outputs fail.
    The checks detect common file mismatches, not authenticity or orbital correctness.
    """
    report, report_source = read_json(directory / "report.json")
    request, request_source = read_json(directory / "request.json")
    try:
        if report["schema"] != "orbit-fit.report.v2" or request["schema"] != "orbit-fit.request.v1":
            raise InputError("Expected orbit-fit.report.v2 and orbit-fit.request.v1")
        if (
            request["frame"] != "TEME"
            or request["time_system"] != "UTC"
            or report["model"]["frame"] != "TEME"
        ):
            raise InputError("Plotting requires TEME/UTC input")
        if report["model"]["time_system"] != "UTC; 86400 model seconds per civil day":
            raise InputError("Unsupported report time convention")
        start, stop, element_epoch = [epoch(report["fit"][key]) for key in ("start", "stop", "epoch")]
        if not start <= element_epoch <= stop or start == stop:
            raise InputError("Invalid fit window or element epoch")
        if any(epoch(request["fit"][key]) != epoch(report["fit"][key]) for key in ("start", "stop", "epoch")):
            raise InputError("Report and request fit epochs do not match")
        identity = report["metadata"]["catalog_number"]
        if (
            type(identity) is not int
            or not 1 <= identity <= 999_999_999
            or type(request["metadata"]["catalog_number"]) is not int
            or identity != request["metadata"]["catalog_number"]
        ):
            raise InputError("Report and request catalog identity do not match")
        name = report["metadata"].get("object_name", "")
        if not isinstance(name, str) or len(name) > 128:
            raise InputError("Invalid object name")
        times, position, velocity = _records(request["samples"], "samples")
        selected = [index for index, time in enumerate(times) if start <= time <= stop]
        residual_times, delta_r, delta_v = _records(report["residuals"], "residuals")
        if residual_times != [times[index] for index in selected]:
            raise InputError("Residual epochs must match every request sample inside the fit window")
        continuous = _metrics(report["continuous_metrics"], len(selected))
        rnorm, vnorm = norms(delta_r), norms(delta_v)
        actual = (_rms(rnorm), float(rnorm.max()), _rms(vnorm), float(vnorm.max()))
        expected = (
            continuous.rms_position_m,
            continuous.max_position_m,
            continuous.rms_velocity_mps,
            continuous.max_velocity_mps,
        )
        if not np.allclose(actual, expected, rtol=1e-9, atol=1e-12):
            raise InputError("Reported continuous metrics disagree with the residual arrays")
        accepted = _boolean(report["accepted"], "accepted")
        continuous_accepted = _boolean(report["continuous_accepted"], "continuous_accepted")
        products = [
            Product(
                "Continuous",
                "accepted" if continuous_accepted else "rejected",
                continuous_accepted,
                continuous,
            )
        ]
        for key, label in (("serialized_omm", "OMM"), ("serialized_tle", "TLE")):
            product = report[key]
            available = _boolean(product["available"], f"{key}.available")
            passed = _boolean(product["accepted"], f"{key}.accepted")
            metric = _metrics(product["metrics"], len(selected)) if product["metrics"] is not None else None
            status = product["status"]
            if status not in {
                "accepted",
                "accuracy_rejected",
                "serialization_failed",
                "parse_failed",
                "evaluation_failed",
            }:
                raise InputError(f"Unsupported {label} status")
            if (
                passed != (status == "accepted")
                or (passed and metric is None)
                or (not available and metric is not None)
            ):
                raise InputError(f"Inconsistent {label} status and metrics")
            products.append(Product(label, status, passed, metric))
        gate = _number(report["fit"]["maximum_rms_position_m"], "RMS gate", nonnegative=True)
        if gate <= 0 or gate != _number(
            request["fit"].get("maximum_rms_position_m", 1000), "Request RMS gate"
        ):
            raise InputError("Report and request RMS gates do not match")
        reason = report["reason"]
        if not isinstance(reason, str) or accepted != (reason == "accepted"):
            raise InputError("Inconsistent overall delivery status")
        policy = report["product_policy"]
        if policy not in {"tle_required", "omm_sufficient"}:
            raise InputError("Unsupported product policy")
        expected_acceptance = (
            continuous_accepted
            and products[1].accepted
            and (policy == "omm_sufficient" or products[2].accepted)
        )
        if accepted != expected_acceptance:
            raise InputError("Overall delivery status disagrees with its product policy")
        if continuous_accepted and continuous.rms_position_m > gate:
            raise InputError("Accepted continuous fit exceeds its RMS gate")
        for product in products[1:]:
            if product.metrics is not None and product.accepted != (product.metrics.rms_position_m <= gate):
                raise InputError(f"{product.name} physical status disagrees with its RMS gate")
        return FitData(
            start,
            stop,
            name,
            identity,
            accepted,
            reason,
            np.array([(time - start).total_seconds() / 3600 for time in residual_times]),
            project_rtn(position[selected], velocity[selected], delta_r),
            vnorm,
            tuple(products),
            gate,
            {"report": report_source, "request": request_source},
            {
                "version": report["version"],
                "model": report["model"],
                "fit": report["fit"],
                "catalog_number": identity,
                "accepted": accepted,
                "reason": reason,
                "product_policy": policy,
            },
        )
    except (KeyError, TypeError, IndexError) as error:
        raise InputError(f"Missing or malformed report/request field: {error}") from error


def load_qualification(path: Path) -> QualificationData:
    """Read the ISS campaign's orbit-fit TLE training and 12–24 h holdout RMS.

    Other backends/products/intervals are not plotted. Every selected arc must
    have both BSTAR choices and both intervals; duplicate or missing rows fail.
    Values are agreement with the frozen NASA forecast, not observed tracking truth.
    """
    raw, source = read_source(path)
    try:
        reader = csv.DictReader(io.StringIO(raw.decode("utf-8")))
        required = {
            "case",
            "backend",
            "product",
            "interval",
            "samples",
            "rms_position_m",
            "max_position_m",
            "continuous_fit_accepted",
        }
        if (
            not reader.fieldnames
            or len(set(reader.fieldnames)) != len(reader.fieldnames)
            or not required.issubset(reader.fieldnames)
        ):
            raise InputError("Unsupported qualification CSV header")
        values: dict[tuple[int, bool, str], float] = {}
        for number, row in enumerate(reader, 2):
            if number > 10_001 or None in row or any(value is None for value in row.values()):
                raise InputError(f"Malformed or excessive qualification rows at line {number}")
            if (row["backend"], row["product"]) != ("orbit-fit", "tle") or row["interval"] not in {
                "fit",
                "12-24h",
            }:
                continue
            match = re.fullmatch(r"iss-([0-9]+)h-([67])p", row["case"])
            if not match or int(match[1]) <= 0 or row["continuous_fit_accepted"] != "true":
                raise InputError(f"Invalid ISS case or rejected training fit at CSV line {number}")
            count = int(row["samples"])
            rms = _number(float(row["rms_position_m"]), "qualification RMS", nonnegative=True)
            maximum = _number(float(row["max_position_m"]), "qualification maximum", nonnegative=True)
            if not 1 <= count <= MAX_SAMPLES or rms > maximum:
                raise InputError(f"Invalid metrics at CSV line {number}")
            key = (int(match[1]), match[2] == "7", row["interval"])
            if key in values:
                raise InputError(f"Duplicate qualification row: {row['case']} / {row['interval']}")
            values[key] = rms
        arcs = tuple(sorted({key[0] for key in values}))
        if len(arcs) < 2 or any(
            (arc, drag, interval) not in values
            for arc in arcs
            for drag in (False, True)
            for interval in ("fit", "12-24h")
        ):
            raise InputError("Qualification comparison needs complete paired arcs and prediction intervals")
        return QualificationData(
            arcs,
            {(arc, drag): values[arc, drag, "fit"] for arc in arcs for drag in (False, True)},
            {(arc, drag): values[arc, drag, "12-24h"] for arc in arcs for drag in (False, True)},
            source,
        )
    except (ValueError, UnicodeError, csv.Error) as error:
        raise InputError(f"{path}: {error}") from error
