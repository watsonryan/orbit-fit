"""@brief Verify RTN geometry, scientific input contracts, publication exports, and CLI failures.
@author watson
"""

from __future__ import annotations

import hashlib
import io
import json
import re
import shutil
import subprocess
import tempfile
import unittest
import xml.etree.ElementTree as ET
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

import numpy as np
from PIL import Image

from orbitfit_plots.cli import main
from orbitfit_plots.data import (
    InputError,
    epoch,
    load_fit,
    load_qualification,
    norms,
    project_rtn,
    read_json,
    read_source,
)
from orbitfit_plots.figures import (
    WIDTHS,
    export_bundle,
    plt,
    product_figure,
    publication_style,
    qualification_figure,
    residual_figure,
)

ROOT = Path(__file__).resolve().parents[3]
INPUT = ROOT / "docs/figures/input"
CSV = ROOT / "docs/benchmarks/2026-10-24-iss-comparison-macos-arm64.csv"


class Geometry(unittest.TestCase):
    """@brief Independent geometric checks for projection signs and invariants."""

    def test_known_axes_and_transverse_sign(self):
        result = project_rtn(
            np.array([[7e6, 0.0, 0.0], [0.0, 7e6, 0.0]]),
            np.array([[0.0, 7e3, 0.0], [-7e3, 0.0, 0.0]]),
            np.array([[3.0, -4.0, 12.0], [3.0, -4.0, 12.0]]),
        )
        np.testing.assert_allclose(result, [[3, -4, 12], [-4, -3, 12]], atol=1e-14)
        np.testing.assert_allclose(norms(result), [13, 13], atol=1e-14)

    def test_projection_is_invariant_under_common_rigid_rotation(self):
        generator = np.random.default_rng(42)
        rotation, _ = np.linalg.qr(generator.normal(size=(3, 3)))
        rotation[:, 0] *= np.linalg.det(rotation)
        r = np.array([[7e6, 2e6, -1e6], [1e6, -6e6, 3e6]])
        v = np.array([[2e3, -7e3, 1e3], [5e3, 1e3, 3e3]])
        dr = np.array([[10.0, 20.0, 30.0], [-2.0, 5.0, 7.0]])
        original = project_rtn(r, v, dr)
        rotated = project_rtn(r @ rotation, v @ rotation, dr @ rotation)
        np.testing.assert_allclose(original, rotated, rtol=1e-13, atol=1e-13)
        np.testing.assert_allclose(norms(original), norms(dr), rtol=1e-13)

    def test_radial_velocity_and_zero_states_have_no_rtn_frame(self):
        r = np.array([[7e6, 0.0, 0.0]])
        for v in (np.zeros((1, 3)), np.array([[1.0, 0.0, 0.0]]), np.array([[1.0, 1e-14, 0.0]])):
            with self.subTest(v=v), self.assertRaises(InputError):
                project_rtn(r, v, r)
        with self.assertRaises(InputError):
            project_rtn(np.zeros((1, 3)), r, r)
        with self.assertRaises(InputError):
            project_rtn(r, r, np.ones((2, 3)))

    def test_extreme_finite_states_do_not_overflow_the_cross_product(self):
        result = project_rtn(
            np.array([[1e200, 0.0, 0.0]]), np.array([[0.0, 1e200, 0.0]]), np.array([[3.0, 4.0, 0.0]])
        )
        np.testing.assert_array_equal(result, [[3, 4, 0]])


class Inputs(unittest.TestCase):
    """@brief Input pairing, validation, and missing-versus-zero regression checks."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.directory = Path(self.temporary.name) / "input"
        shutil.copytree(INPUT, self.directory)

    def tearDown(self):
        self.temporary.cleanup()
        plt.close("all")

    def change(self, filename, edit):
        path = self.directory / filename
        value = json.loads(path.read_text())
        edit(value)
        path.write_text(json.dumps(value), encoding="utf-8")

    def test_native_example_and_qualification_values(self):
        fit = load_fit(self.directory)
        self.assertEqual(len(fit.hours), 61)
        self.assertTrue(fit.accepted)
        self.assertAlmostEqual(fit.products[2].metrics.rms_position_m, 9.024916197064433, places=10)
        self.assertAlmostEqual(
            np.sqrt(np.mean(norms(fit.position_rtn_m) ** 2)),
            fit.products[0].metrics.rms_position_m,
            places=14,
        )
        qualification = load_qualification(CSV)
        self.assertEqual(qualification.arcs, (6, 12, 24, 48))
        self.assertAlmostEqual(qualification.prediction[12, True], 16706.226107720, places=8)
        self.assertLess(qualification.training[12, True], qualification.training[12, False])
        self.assertGreater(qualification.prediction[12, True], qualification.prediction[12, False])

    def test_request_report_identity_and_time_must_match(self):
        self.change("request.json", lambda value: value["metadata"].update(catalog_number=12345))
        with self.assertRaisesRegex(InputError, "identity"):
            load_fit(self.directory)
        shutil.copy(INPUT / "request.json", self.directory / "request.json")
        self.change("request.json", lambda value: value["fit"].update(epoch="2026-01-01T00:01:00Z"))
        with self.assertRaisesRegex(InputError, "epochs do not match"):
            load_fit(self.directory)

    def test_residuals_must_cover_every_in_window_sample_in_order(self):
        self.change("report.json", lambda value: value["residuals"].pop())
        with self.assertRaisesRegex(InputError, "every request sample"):
            load_fit(self.directory)
        shutil.copy(INPUT / "report.json", self.directory / "report.json")
        self.change("report.json", lambda value: value["residuals"].reverse())
        with self.assertRaisesRegex(InputError, "strictly increasing"):
            load_fit(self.directory)

    def test_report_metrics_are_checked_against_actual_residuals(self):
        self.change("report.json", lambda value: value["continuous_metrics"].update(rms_position_m=1e-6))
        with self.assertRaisesRegex(InputError, "disagree"):
            load_fit(self.directory)

    def test_schema_frame_and_metric_types_are_explicit(self):
        edits = (
            lambda value: value.update(schema="orbit-fit.report.v1"),
            lambda value: value["model"].update(frame="GCRF"),
            lambda value: value["continuous_metrics"].update(sample_count=True),
            lambda value: value["continuous_metrics"].update(rms_position_m=True),
            lambda value: value["continuous_metrics"].update(rms_position_m=-1),
            lambda value: value.update(accepted="false"),
            lambda value: value.update(accepted=False, reason="tle_rejected"),
            lambda value: value.update(product_policy="unsupported"),
            lambda value: value["serialized_tle"].update(metrics=None),
            lambda value: value["serialized_tle"]["metrics"].update(rms_position_m=100, max_position_m=100),
        )
        for edit in edits:
            with self.subTest(edit=edit):
                shutil.copy(INPUT / "report.json", self.directory / "report.json")
                self.change("report.json", edit)
                with self.assertRaises(InputError):
                    load_fit(self.directory)

    def test_missing_product_is_not_plotted_as_zero_and_rejection_is_visible(self):
        def edit(value):
            value.update(accepted=False, reason="tle_unavailable")
            value["serialized_tle"].update(
                available=False, accepted=False, status="serialization_failed", metrics=None
            )

        self.change("report.json", edit)
        fit = load_fit(self.directory)
        self.assertIsNone(fit.products[2].metrics)
        with publication_style():
            plot = product_figure(fit, WIDTHS["single"])
            self.assertIn("rejected", plot.figure.get_suptitle())
            self.assertIn("Not evaluated", [text.get_text() for text in plot.figure.axes[0].texts])
            self.assertEqual(len(plot.figure.axes[0].lines[0].get_ydata()), 2)

    def test_true_zero_metrics_use_a_linear_axis(self):
        def edit(value):
            for residual in value["residuals"]:
                residual.update(position_m=[0, 0, 0], velocity_mps=[0, 0, 0])
            for metrics in (
                value["continuous_metrics"],
                value["serialized_omm"]["metrics"],
                value["serialized_tle"]["metrics"],
            ):
                for key in list(metrics):
                    if key != "sample_count":
                        metrics[key] = 0

        self.change("report.json", edit)
        fit = load_fit(self.directory)
        with publication_style():
            plot = product_figure(fit, WIDTHS["single"])
            self.assertEqual(plot.figure.axes[0].get_yscale(), "linear")
            np.testing.assert_array_equal(plot.figure.axes[0].lines[0].get_ydata(), [0, 0, 0])

    def test_json_duplicate_nonfinite_and_byte_bounds(self):
        path = self.directory / "invalid.json"
        for content in ('{"a": 1, "a": 2}', '{"a": NaN}', '{"a": Infinity}', '{"a": 1e9999}', "[]"):
            with self.subTest(content=content):
                path.write_text(content)
                with self.assertRaises(InputError):
                    read_json(path)
        path.write_bytes(b"x" * 33)
        with patch("orbitfit_plots.data.MAX_BYTES", 32), self.assertRaisesRegex(InputError, "exceeds"):
            read_source(path)

    def test_utc_contract_rejects_offsets_leap_labels_and_excess_precision(self):
        self.assertEqual(epoch("2026-01-01T00:00:00.1Z").microsecond, 100000)
        for text in (
            "2026-01-01",
            "2026-01-01T00:00:00+00:00",
            "2026-01-01T00:00:60Z",
            "2026-01-01T00:00:00.0000001Z",
        ):
            with self.subTest(text=text), self.assertRaises(InputError):
                epoch(text)

    def test_qualification_rejects_missing_duplicate_and_nonfinite_selected_rows(self):
        content = CSV.read_text()
        selected = next(
            line for line in content.splitlines() if line.startswith("iss-12h-7p,orbit-fit,tle,12-24h,")
        )
        for bad in (
            content.replace(selected + "\n", ""),
            content + selected + "\n",
            content.replace("16706.226107720", "nan"),
        ):
            path = self.directory / "invalid.csv"
            path.write_text(bad)
            with self.assertRaises(InputError):
                load_qualification(path)


class Publication(unittest.TestCase):
    """@brief Verify output geometry, font embedding, provenance, and repeatability."""

    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    def tearDown(self):
        self.temporary.cleanup()
        plt.close("all")

    def render(self, destination, *, width="single", dpi=150):
        with redirect_stdout(io.StringIO()):
            return main(
                [
                    "fit",
                    str(INPUT),
                    "--qualification",
                    str(CSV),
                    "--output-dir",
                    str(destination),
                    "--width",
                    width,
                    "--dpi",
                    str(dpi),
                ]
            )

    def test_three_figures_export_exact_page_sizes_and_provenance(self):
        output = self.root / "figures"
        self.assertEqual(self.render(output), 0)
        manifest = json.loads((output / "manifest.json").read_text())
        self.assertEqual(len(manifest["artifacts"]), 9)
        self.assertEqual(
            manifest["inputs"]["report"]["sha256"],
            hashlib.sha256((INPUT / "report.json").read_bytes()).hexdigest(),
        )
        self.assertEqual(manifest["style"]["font_size_pt"], 9)
        self.assertFalse(manifest["style"]["tex"])
        self.assertEqual(manifest["native_fit"]["catalog_number"], 99999)
        self.assertEqual(manifest["native_fit"]["product_policy"], "tle_required")
        for item in manifest["artifacts"]:
            self.assertEqual(hashlib.sha256((output / item["file"]).read_bytes()).hexdigest(), item["sha256"])
        for name in ("residuals", "serialization", "iss-prediction"):
            tree = ET.parse(output / f"{name}.svg")
            self.assertEqual(tree.getroot().attrib["width"], "252pt")
            self.assertEqual(tree.findall(".//{http://www.w3.org/2000/svg}text"), [])
            self.assertIn("watson", (output / f"{name}.svg").read_text())
            with Image.open(output / f"{name}.png") as png:
                self.assertEqual(png.width, 525)
                self.assertEqual(png.info["Author"], "watson")
        self.assertIn("not observed tracking truth", (output / "captions.md").read_text())

    @unittest.skipUnless(
        shutil.which("pdffonts") and shutil.which("pdfinfo"), "Poppler is required for PDF auditing"
    )
    def test_pdf_fonts_are_embedded_truetype_and_dimensions_are_preserved(self):
        output = self.root / "pdf"
        self.assertEqual(self.render(output), 0)
        for path in output.glob("*.pdf"):
            fonts = subprocess.run(
                ["pdffonts", str(path)], check=True, capture_output=True, text=True, timeout=60
            ).stdout
            rows = fonts.splitlines()[2:]
            self.assertTrue(rows)
            for row in rows:
                self.assertIn("TrueType", row)
                self.assertRegex(row, r"\byes\s+yes\s+(?:yes|no)\s+[0-9]+\s+[0-9]+$")
            info = subprocess.run(
                ["pdfinfo", str(path)], check=True, capture_output=True, text=True, timeout=60
            ).stdout
            self.assertIn("Author:          watson", info)
            match = re.search(r"Page size:\s+([0-9.]+) x ([0-9.]+) pts", info)
            self.assertIsNotNone(match)
            self.assertAlmostEqual(float(match[1]), 252)

    def test_repeated_render_has_identical_artifact_bytes_in_one_environment(self):
        first, second = self.root / "first", self.root / "second"
        self.assertEqual(self.render(first), 0)
        self.assertEqual(self.render(second), 0)
        for path in first.iterdir():
            self.assertEqual(path.read_bytes(), (second / path.name).read_bytes(), path.name)

    def test_checked_in_gallery_matches_its_manifest_and_plot_sources(self):
        gallery = ROOT / "docs/figures"
        manifest = json.loads((gallery / "manifest.json").read_text())
        self.assertEqual(manifest["author"], "watson")
        for artifact in manifest["artifacts"]:
            raw = (gallery / artifact["file"]).read_bytes()
            self.assertEqual(len(raw), artifact["size_bytes"])
            self.assertEqual(hashlib.sha256(raw).hexdigest(), artifact["sha256"])
        for name, digest in manifest["plot_code_sha256"].items():
            raw = (ROOT / "tools/plots/src/orbitfit_plots" / name).read_bytes()
            self.assertEqual(hashlib.sha256(raw).hexdigest(), digest, name)
        for role, path in (
            ("report", INPUT / "report.json"),
            ("request", INPUT / "request.json"),
            ("qualification", CSV),
        ):
            self.assertEqual(
                hashlib.sha256(path.read_bytes()).hexdigest(), manifest["inputs"][role]["sha256"]
            )
        captions = (gallery / "captions.md").read_text()
        for item in manifest["figures"]:
            self.assertIn(item["caption"], captions)

    def test_two_column_layout_keeps_artists_inside_the_page(self):
        fit, qualification = load_fit(INPUT), load_qualification(CSV)
        with publication_style():
            for width in WIDTHS.values():
                for item in (
                    residual_figure(fit, width),
                    product_figure(fit, width),
                    qualification_figure(qualification, width),
                ):
                    item.figure.canvas.draw()
                    bounds = item.figure.get_tightbbox(item.figure.canvas.get_renderer())
                    self.assertGreaterEqual(bounds.x0, -0.01)
                    self.assertGreaterEqual(bounds.y0, -0.01)
                    self.assertLessEqual(bounds.x1, width + 0.01)
                    self.assertLessEqual(bounds.y1, item.figure.get_figheight() + 0.01)

    def test_cli_rejects_bad_input_or_font_without_overwriting_existing_output(self):
        output = self.root / "existing"
        output.mkdir()
        sentinel = output / "keep.txt"
        sentinel.write_text("original")
        with self.assertLogs(level="ERROR"):
            self.assertEqual(self.render(output), 1)
        self.assertEqual(sentinel.read_text(), "original")
        fresh = self.root / "fresh"
        with self.assertLogs(level="ERROR"):
            self.assertEqual(
                main(["fit", str(INPUT), "--output-dir", str(fresh), "--font", "missing-orbitfit-test-font"]),
                1,
            )
        self.assertFalse(fresh.exists())
        with self.assertLogs(level="ERROR"):
            self.assertEqual(main(["fit", str(self.root / "absent"), "--output-dir", str(fresh)]), 1)
        self.assertFalse(fresh.exists())

    def test_save_failure_cleans_only_the_owned_output(self):
        with publication_style() as font:
            item = product_figure(load_fit(INPUT), WIDTHS["single"])
            with (
                patch.object(item.figure, "savefig", side_effect=OSError("test write failure")),
                self.assertRaises(OSError),
            ):
                export_bundle([item], self.root / "failed", {}, font, WIDTHS["single"], ("pdf",), 150)
        self.assertFalse((self.root / "failed").exists())

    def test_qualification_standalone_and_invalid_command_options(self):
        with redirect_stdout(io.StringIO()):
            self.assertEqual(
                main(
                    [
                        "qualification",
                        str(CSV),
                        "--output-dir",
                        str(self.root / "iss"),
                        "--formats",
                        "svg",
                        "--width",
                        "double",
                    ]
                ),
                0,
            )
        tree = ET.parse(self.root / "iss/iss-prediction.svg")
        self.assertEqual(tree.getroot().attrib["width"], "515.52pt")
        with redirect_stderr(io.StringIO()), self.assertRaises(SystemExit) as error:
            main(["fit", str(INPUT), "--output-dir", str(self.root / "unused"), "--dpi", "0"])
        self.assertEqual(error.exception.code, 2)
        self.assertFalse((self.root / "unused").exists())


if __name__ == "__main__":
    unittest.main()
