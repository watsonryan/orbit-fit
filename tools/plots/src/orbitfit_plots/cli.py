"""@brief Provide the optional orbitfit-plot command for native reports and the ISS campaign.
@author watson
"""

from __future__ import annotations

import argparse
import logging
from pathlib import Path

from . import __version__
from .data import InputError, load_fit, load_qualification
from .figures import (
    WIDTHS,
    export_bundle,
    plt,
    product_figure,
    publication_style,
    qualification_figure,
    residual_figure,
)


def parser() -> argparse.ArgumentParser:
    """Build the two explicit input modes and publication export options."""
    result = argparse.ArgumentParser(
        description="Publication figures from native orbit-fit outputs; no fitting or propagation."
    )
    result.add_argument("--version", action="version", version=f"orbitfit-plots {__version__}")
    commands = result.add_subparsers(dest="command", required=True)
    fit = commands.add_parser("fit", help="Plot residuals and serialization from a results directory")
    fit.add_argument("results", type=Path, help="Directory containing report.json and resolved request.json")
    fit.add_argument("--qualification", type=Path, help="Also render the frozen ISS comparison CSV")
    qualification = commands.add_parser("qualification", help="Plot the frozen ISS comparison CSV alone")
    qualification.add_argument("csv", type=Path)
    for command in (fit, qualification):
        command.add_argument(
            "--output-dir",
            type=Path,
            required=True,
            help="New directory; existing outputs are never overwritten",
        )
        command.add_argument(
            "--width",
            choices=WIDTHS,
            default="single",
            help="Exact page width: single=3.5 in, double=7.16 in",
        )
        command.add_argument(
            "--formats", nargs="+", choices=("pdf", "svg", "png"), default=["pdf", "svg", "png"]
        )
        command.add_argument(
            "--dpi", type=int, default=600, help="PNG resolution, 150–1200 dpi (default 600)"
        )
        command.add_argument(
            "--font", default="STIXGeneral", help="Installed font family; default is bundled with Matplotlib"
        )
    return result


def main(argv: list[str] | None = None) -> int:
    """Render selected inputs; return 0 on success, 1 on input/I/O failure, 2 on usage errors."""
    arguments = parser().parse_args(argv)
    logging.basicConfig(level=logging.WARNING, format="orbitfit-plot: %(levelname)s: %(message)s")
    if not 150 <= arguments.dpi <= 1200:
        parser().error("--dpi must be between 150 and 1200")
    if len(set(arguments.formats)) != len(arguments.formats):
        parser().error("--formats must not contain duplicates")
    figures = []
    try:
        fit = load_fit(arguments.results) if arguments.command == "fit" else None
        csv = arguments.qualification if arguments.command == "fit" else arguments.csv
        qualification = load_qualification(csv) if csv is not None else None
        sources = dict(fit.sources) if fit else {}
        if qualification:
            sources["qualification"] = qualification.source
        width = WIDTHS[arguments.width]
        with publication_style(arguments.font) as font:
            if fit:
                figures.append(residual_figure(fit, width))
                figures.append(product_figure(fit, width))
            if qualification:
                figures.append(qualification_figure(qualification, width))
            export_bundle(
                figures,
                arguments.output_dir,
                sources,
                font,
                width,
                tuple(arguments.formats),
                arguments.dpi,
                native=fit.native if fit else None,
            )
        print(f"Wrote {len(figures)} figures, captions, and manifest to {arguments.output_dir}")
        return 0
    except (InputError, OSError, ValueError) as error:
        logging.error("%s", error)
        return 1
    finally:
        for item in figures:
            plt.close(item.figure)


if __name__ == "__main__":
    raise SystemExit(main())
