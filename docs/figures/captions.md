<!-- @brief Captions and interpretation for generated orbit-fit figures. @author watson -->
# Figure captions

## residuals

Continuous-fit residuals for catalog 99999 (SYNTHETIC EXAMPLE), 2026-01-01T00:00:00+00:00 through 2026-01-01T02:00:00+00:00. Delivery: accepted. Residuals are predicted minus supplied states. R points along the supplied TEME position, N along its position–velocity cross product, and T=N×R. Velocity is the norm of the TEME velocity difference, not a derivative in rotating RTN. Statistics include every in-window sample with equal validation weight; the 3D position RMS is not per-axis RMS. The fit's RMS gate is not an instantaneous error bound.

## serialization

Position errors for the continuous model and reparsed OMM/TLE products against the same 61 supplied states. Overall delivery: accepted. Labels beside markers give RMS; square markers give maximum Euclidean error. The 10 m dashed gate applies to RMS only. Product labels describe physical accuracy and do not override continuous solver acceptance. Missing metrics are not zeros. The ordinate is logarithmic. This comparison exposes serialization effects; it is not an independent accuracy measurement.

## iss-prediction

Reparsed orbit-fit TLEs compared with the frozen NASA ISS forecast for paired fixed/estimated BSTAR fits. The upper panel shows in-window RMS; the lower panel shows the disjoint interval (fit end + 12 h, fit end + 24 h]. Each arc has its own fit end, so the prediction panels do not compare identical absolute epochs. Lines connect tested cases only. A smaller training error does not guarantee better prediction. Values measure agreement with a forecast, not observed tracking truth. These are the retained qualification CSV results, not a new campaign.
