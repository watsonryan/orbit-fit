"""@brief Render fixed-size publication figures and reproducibility metadata with SciencePlots.
@author watson

PDF embeds TrueType subsets, SVG outlines text, and PNG uses the requested DPI.
No TeX installation is needed. Explicit layout and fonts override the upstream
IEEE preset; users must still check their target journal's submission rules.
"""

from __future__ import annotations

import hashlib
import json
import platform
import shutil
from collections.abc import Iterator
from contextlib import contextmanager
from dataclasses import asdict, dataclass
from importlib.metadata import version
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import numpy as np  # noqa: E402
import scienceplots  # noqa: E402,F401 - import registers its styles
from matplotlib import font_manager  # noqa: E402
from matplotlib import pyplot as plt
from matplotlib.figure import Figure  # noqa: E402
from matplotlib.ft2font import __freetype_version__  # noqa: E402

from . import __version__  # noqa: E402
from .data import FitData, InputError, QualificationData, Source  # noqa: E402

WIDTHS = {"single": 3.5, "double": 7.16}
COLORS = ("#0072B2", "#D55E00", "#009E73")


@dataclass(frozen=True)
class PublicationFigure:
    """@brief One live Matplotlib figure, stable basename, and scientific caption."""

    name: str
    figure: Figure
    caption: str


@contextmanager
def publication_style(font: str = "STIXGeneral") -> Iterator[dict[str, str]]:
    """Scope all styles and require an installed font instead of silently substituting.

    STIXGeneral ships with the pinned Matplotlib wheel. Its file hash and name
    are recorded; callers may explicitly select another installed journal font.
    """
    try:
        path = Path(
            font_manager.findfont(font_manager.FontProperties(family=font), fallback_to_default=False)
        )
    except ValueError as error:
        raise InputError(f"Font {font!r} is unavailable; install it or use STIXGeneral") from error
    with (
        plt.style.context(["science", "ieee", "no-latex"]),
        matplotlib.rc_context(
            {
                "font.family": "serif",
                "font.serif": [font],
                "font.size": 9,
                "axes.labelsize": 9,
                "axes.titlesize": 9,
                "xtick.labelsize": 9,
                "ytick.labelsize": 9,
                "legend.fontsize": 9,
                "figure.titlesize": 9,
                "mathtext.fontset": "stix",
                "text.usetex": False,
                "pdf.fonttype": 42,
                "ps.fonttype": 42,
                "svg.fonttype": "path",
                "svg.hashsalt": f"orbitfit-plots-{__version__}",
                "savefig.bbox": None,
                "figure.dpi": 120,
                "axes.linewidth": 0.7,
                "lines.linewidth": 1.1,
                "axes.formatter.useoffset": False,
                "axes.unicode_minus": True,
            }
        ),
    ):
        yield {"family": font, "file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}


def _units(maximum: float, *, velocity: bool = False) -> tuple[float, str]:
    """Choose a readable SI-prefix display unit; stored data and metrics stay SI."""
    scale, name = (1.0, "m")
    if 0 < maximum < 0.001:
        scale, name = (1e6, "µm")
    elif 0 < maximum < 1:
        scale, name = (1e3, "mm")
    elif maximum >= 1000:
        scale, name = (0.001, "km")
    return scale, name + ("/s" if velocity else "")


def _axes(width: float, panels: int, height: float) -> tuple[Figure, np.ndarray]:
    """Create constrained-layout panels at an exact physical page width."""
    figure, axes = plt.subplots(panels, 1, figsize=(width, height), layout="constrained", squeeze=False)
    for axis in axes.flat:
        axis.grid(axis="y", color="0.85", linewidth=0.5)
        axis.set_axisbelow(True)
    return figure, axes[:, 0]


def residual_figure(data: FitData, width: float) -> PublicationFigure:
    """Plot continuous RTN position residuals and TEME velocity error magnitude."""
    figure, axes = _axes(width, 2, 4.35)
    if not data.accepted:
        figure.suptitle("Delivery rejected: " + data.reason.replace("_", " "))
    position_scale, position_unit = _units(float(np.abs(data.position_rtn_m).max()))
    velocity_scale, velocity_unit = _units(float(data.velocity_norm_mps.max()), velocity=True)
    for index, (label, style) in enumerate(zip(("Radial", "Transverse", "Normal"), ("-", "--", ":"))):
        axes[0].plot(
            data.hours,
            data.position_rtn_m[:, index] * position_scale,
            color=COLORS[index],
            linestyle=style,
            label=label,
        )
    metric = data.products[0].metrics
    assert metric is not None
    axes[0].set_title("(a) Position residuals in reference RTN", loc="left")
    axes[0].set_ylabel(f"Position error [{position_unit}]")
    axes[0].legend(
        loc="upper center", ncol=3, bbox_to_anchor=(0.5, 1.02), columnspacing=0.8, handlelength=1.6
    )
    axes[0].margins(y=0.3)
    axes[0].text(
        0.02,
        0.03,
        f"3D RMS {metric.rms_position_m * position_scale:.3g}; max {metric.max_position_m * position_scale:.3g} {position_unit}",
        transform=axes[0].transAxes,
        va="bottom",
        bbox={"facecolor": "white", "edgecolor": "none", "alpha": 0.9, "pad": 1},
    )
    axes[1].plot(data.hours, data.velocity_norm_mps * velocity_scale, color=COLORS[0], linestyle="-")
    axes[1].set_title("(b) Velocity residual magnitude", loc="left")
    axes[1].set_ylabel(f"Velocity error [{velocity_unit}]")
    axes[1].set_ylim(0, max(float(data.velocity_norm_mps.max()) * velocity_scale * 1.3, 1e-12))
    axes[1].text(
        0.02,
        0.96,
        f"RMS {metric.rms_velocity_mps * velocity_scale:.3g}; max {metric.max_velocity_mps * velocity_scale:.3g} {velocity_unit}",
        transform=axes[1].transAxes,
        va="top",
        bbox={"facecolor": "white", "edgecolor": "none", "alpha": 0.9, "pad": 1},
    )
    for axis in axes:
        axis.set_xlabel("Time from fit start [h]")
        axis.set_xlim(data.hours[0], data.hours[-1])
    decision = "accepted" if data.accepted else f"rejected ({data.reason})"
    caption = (
        f"Continuous-fit residuals for catalog {data.catalog_number} ({data.object_name or 'unnamed'}), "
        f"{data.start.isoformat()} through {data.stop.isoformat()}. Delivery: {decision}. "
        "Residuals are predicted minus supplied states. R points along the supplied TEME position, "
        "N along its position–velocity cross product, and T=N×R. Velocity is the norm of the TEME "
        "velocity difference, not a derivative in rotating RTN. Statistics include every in-window "
        "sample with equal validation weight; the 3D position RMS is not per-axis RMS. "
        "The fit's RMS gate is not an instantaneous error bound."
    )
    return PublicationFigure("residuals", figure, caption)


def product_figure(data: FitData, width: float) -> PublicationFigure:
    """Compare continuous, reparsed OMM, and reparsed TLE RMS/max position errors.

    Logarithmic scaling is used only when every plotted value is positive and
    the dynamic range exceeds 100. Zero remains exact on a linear axis.
    Missing or failed evaluations are labelled rather than drawn at zero.
    """
    figure, (axis,) = _axes(width, 1, 3.15)
    if not data.accepted:
        figure.suptitle("Delivery rejected: " + data.reason.replace("_", " "))
    values = [
        value
        for product in data.products
        if product.metrics is not None
        for value in (product.metrics.rms_position_m, product.metrics.max_position_m)
    ]
    values.append(data.rms_gate_m)
    logarithmic = min(values) > 0 and max(values) / min(values) > 100
    if logarithmic:
        axis.set_yscale("log")
    for offset, attribute, label, marker, color in (
        (-0.08, "rms_position_m", "RMS", "o", COLORS[0]),
        (0.08, "max_position_m", "Maximum", "s", COLORS[1]),
    ):
        points = [
            (index + offset, getattr(product.metrics, attribute))
            for index, product in enumerate(data.products)
            if product.metrics is not None
        ]
        axis.plot(
            [point[0] for point in points],
            [point[1] for point in points],
            linestyle="none",
            marker=marker,
            color=color,
            markersize=5,
            label=label,
        )
    axis.axhline(data.rms_gate_m, color="0.25", linestyle="--", linewidth=0.9, label="RMS gate")
    for index, product in enumerate(data.products):
        if product.metrics is None:
            axis.text(
                index, 0.12, "Not evaluated", ha="center", rotation=90, transform=axis.get_xaxis_transform()
            )
        else:
            axis.annotate(
                f"{product.metrics.rms_position_m:.3g} m",
                (index - 0.08, product.metrics.rms_position_m),
                xytext=(0, -16),
                textcoords="offset points",
                ha="center",
            )
    if logarithmic:
        axis.set_ylim(min(values) / 6, max(values) * 12)
    else:
        axis.set_ylim(-max(values) * 0.12, max(values) * 1.4)
    axis.set_xlim(-0.5, 2.5)
    axis.set_xticks(
        range(3),
        [
            f"{product.name}\n{'passed' if product.accepted else 'unavailable' if product.metrics is None else 'rejected'}"
            for product in data.products
        ],
    )
    axis.set_ylabel("Position error [m]")
    axis.set_title("Serialization and physical acceptance", loc="left")
    axis.legend(loc="upper left", ncol=3, columnspacing=0.8, handlelength=1.4)
    decision = "accepted" if data.accepted else f"rejected ({data.reason})"
    caption = (
        f"Position errors for the continuous model and reparsed OMM/TLE products against the same "
        f"{data.products[0].metrics.sample_count} supplied states. Overall delivery: {decision}. "
        "Labels beside markers give RMS; square markers give maximum Euclidean error. "
        f"The {data.rms_gate_m:g} m dashed gate applies to RMS only. Product labels describe physical "
        "accuracy and do not override continuous solver acceptance. Missing metrics are not zeros. "
        f"The ordinate is {'logarithmic' if logarithmic else 'linear'}. "
        "This comparison exposes serialization effects; it is not an independent accuracy measurement."
    )
    return PublicationFigure("serialization", figure, caption)


def qualification_figure(data: QualificationData, width: float) -> PublicationFigure:
    """Plot the frozen ISS training/holdout comparison without rerunning estimation."""
    figure, axes = _axes(width, 2, 4.3)
    for drag, color, style, marker, label in (
        (False, COLORS[0], "-", "o", "Fixed BSTAR = 0"),
        (True, COLORS[1], "--", "s", "Estimated BSTAR"),
    ):
        for axis, values, scale in ((axes[0], data.training, 1), (axes[1], data.prediction, 0.001)):
            axis.plot(
                data.arcs,
                [values[arc, drag] * scale for arc in data.arcs],
                color=color,
                linestyle=style,
                marker=marker,
                markersize=4,
                label=label,
            )
    axes[0].set_title("(a) Fit to NASA ISS forecast", loc="left")
    axes[1].set_title("(b) Prediction: 12–24 h after fit end", loc="left")
    axes[0].set_ylabel("Position RMS [m]")
    axes[1].set_ylabel("Position RMS [km]")
    axes[0].legend(loc="upper left")
    for axis in axes:
        axis.set_xlabel("Fit arc duration [h]")
        axis.set_xticks(data.arcs)
        axis.set_ylim(bottom=0)
        axis.margins(y=0.25)
    caption = (
        "Reparsed orbit-fit TLEs compared with the frozen NASA ISS forecast for paired fixed/estimated "
        "BSTAR fits. The upper panel shows in-window RMS; the lower panel shows the disjoint interval "
        "(fit end + 12 h, fit end + 24 h]. Each arc has its own fit end, so the prediction panels do "
        "not compare identical absolute epochs. Lines connect tested cases only. A smaller training "
        "error does not guarantee better prediction. Values measure agreement with a forecast, not "
        "observed tracking truth. These are the retained qualification CSV results, not a new campaign."
    )
    return PublicationFigure("iss-prediction", figure, caption)


def export_bundle(
    figures: list[PublicationFigure],
    output: Path,
    sources: dict[str, Source],
    font: dict[str, str],
    width: float,
    formats: tuple[str, ...],
    dpi: int,
    native: dict | None = None,
) -> None:
    """Write figures, captions, and hashes into a new exclusively created directory.

    Existing directories are never overwritten. A failure removes only the new
    directory owned by this call. This is not atomic publication or crash durability;
    manifest.json is written last and identifies a completed bundle.
    """
    if output.exists():
        raise InputError(f"Output directory already exists: {output}")
    output.parent.mkdir(parents=True, exist_ok=True)
    output.mkdir()
    try:
        artifacts = []
        for item in figures:
            for extension in formats:
                target = output / f"{item.name}.{extension}"
                if extension == "pdf":
                    metadata = {
                        "Title": item.name,
                        "Author": "watson",
                        "Subject": item.caption,
                        "Creator": f"orbitfit-plots {__version__}",
                        "CreationDate": None,
                        "ModDate": None,
                    }
                elif extension == "svg":
                    metadata = {
                        "Title": item.name,
                        "Creator": "watson",
                        "Description": item.caption,
                        "Date": None,
                    }
                else:
                    metadata = {
                        "Title": item.name,
                        "Author": "watson",
                        "Description": item.caption,
                        "Software": f"orbitfit-plots {__version__}",
                    }
                item.figure.savefig(target, format=extension, dpi=dpi, metadata=metadata, facecolor="white")
                artifacts.append(
                    {
                        "file": target.name,
                        "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
                        "size_bytes": target.stat().st_size,
                    }
                )
        captions = "<!-- @brief Captions and interpretation for generated orbit-fit figures. @author watson -->\n# Figure captions\n\n"
        captions += "\n\n".join(f"## {item.name}\n\n{item.caption}" for item in figures) + "\n"
        (output / "captions.md").write_text(captions, encoding="utf-8", newline="\n")
        code = {
            path.name: hashlib.sha256(path.read_bytes()).hexdigest()
            for path in sorted(Path(__file__).parent.glob("*.py"))
        }
        manifest = {
            "schema": "orbit-fit.figures.v1",
            "author": "watson",
            "description": "Publication figures from native orbit-fit numerical outputs",
            "versions": {
                "orbitfit-plots": __version__,
                "python": platform.python_version(),
                **{
                    name: version(name)
                    for name in ("numpy", "matplotlib", "SciencePlots", "fonttools", "Pillow")
                },
            },
            "platform": {"system": platform.system(), "machine": platform.machine()},
            "renderer": {"backend": "Agg", "freetype": __freetype_version__},
            "native_fit": native,
            "plot_code_sha256": code,
            "inputs": {role: asdict(source) for role, source in sources.items()},
            "style": {
                "base": ["science", "ieee", "no-latex"],
                "width_inches": width,
                "font_size_pt": 9,
                "font": font,
                "png_dpi": dpi,
                "pdf_fonttype": 42,
                "svg_text": "paths",
                "tex": False,
            },
            "figures": [
                {
                    "name": item.name,
                    "caption": item.caption,
                    "height_inches": float(item.figure.get_figheight()),
                }
                for item in figures
            ],
            "artifacts": artifacts,
        }
        (output / "manifest.json").write_text(
            json.dumps(manifest, indent=2, sort_keys=True, allow_nan=False) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    except BaseException:
        shutil.rmtree(output)
        raise
