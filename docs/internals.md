<!-- @brief Specify internal chart, differentiation, factor ownership, delivery, and cancellation invariants. @author watson -->
# Internal contracts and maintenance

The generated reference includes every authored library/CLI `.hpp` and `.cpp` under `include`, `src`, and `apps`, including private members, static functions, and anonymous namespaces. Doxygen warnings and missing parameter documentation fail the documentation target. Tests, examples, and upstream kernel internals have their own explanatory sources; immutable upstream files are not rewritten to imitate project authorship.

## Call flow and ownership

`fit` validates the complete caller span, selects a contiguous in-window span, obtains an explicit/coarse seed, fixes the coordinate chart, invokes estimation, then evaluates every in-window sample once for physical metrics and residuals. Options/spans are borrowed synchronously. Results own their text and vectors. Nothing in the public API retains a caller span after return.

`estimate` owns the normalized vector, `StateCost` objects, their optional Huber losses, a Ceres problem, and the iteration monitor. Each factor owns its observation and whitening matrix and borrows the immutable `Chart`. Ceres is configured with `DO_NOT_TAKE_OWNERSHIP` for costs/losses. Destruction order destroys the problem before factors and losses. The chart outlives the entire synchronous call. Ceres uses one evaluation thread per fit; independent fits can run concurrently.

`StateCost::linearize` produces raw SI derivatives, applies its whitener exactly once to residual and Jacobian, and leaves robust loss application to Ceres. Both value-only and derivative callbacks obtain residual values from the unmodified reference kernel; the AD callback supplies the Jacobian. Using the Jet primal in only one callback can change the objective at the last bits and corrupt trust-region gain ratios near convergence. The monitor computes robust first-order weights explicitly for its column-normalized gradient. It directly evaluates immutable factors because Ceres prohibits `Problem::Evaluate` from an iteration callback. Final rank uses the solver's whitened/robust Jacobian at relative singular-value threshold 1e-8. Changing scales changes conditioning; rank is not a statistical covariance estimate.

The monitor owns a snapshot of the latest accepted parameters and their cost. Ceres 2.2 restores the initial parameter block after an unsuccessful solve, even when iteration updates were enabled; the adapter restores its snapshot for failed/cancelled diagnostics. Restoration does not change the failure classification or permit acceptance. Cancelled results retain the corresponding cost while skipping unavailable rank/physical metrics.

## Fixed coordinate chart

The chart stores six unscaled seed coordinates: log mean motion; eccentricity-vector sine/cosine components; plane-vector sine/cosine components; and mean longitude. First six local increments scale by 1e-3. Optional BSTAR increments scale by 1e-4. Hemisphere selection, seed origin, element epoch, and fixed BSTAR stay constant throughout the solve.

`sgp4_model::decode` is shared by value, first-order, and second-order paths. The eccentricity/plane vectors must have nonzero magnitude for derivatives: the associated norms and `atan2` are undefined at exactly singular origins. Reject these trials explicitly. Do not silently perturb them or switch differentiation methods. `Chart::decode` can produce value-only elements at points that the derivative domain cannot support; callers must use the correct domain check for their operation.

`InvalidModel` represents an inadmissible numerical trial. The Ceres adapter returns `false` for it, allowing trust-region contraction. Initial/final invalid models become explicit library errors. Allocation failures and unrelated programming errors are not intended to masquerade as successful numerical convergence.

## Derivatives and SGP4 state

First-order evaluation uses seven tangent slots and returns only the six or seven active columns. Nested AD uses `Jet<Jet<double,7>,1>` through `SecondOrder`: the inner tangent contains the Jacobian; one outer direction supplies a Hessian row. Repeating six/seven directions limits stack storage. Inactive public output slots remain zero. Output Hessians are not symmetrized to hide numerical discrepancies.

The adapter's power operation accepts constant exponents only and checks that both derivative levels are zero in the exponent. Its remainder operation retains `std::fmod`'s primal value and differentiates the local quotient branch. Comparisons use primal values. Clamps, resonance selection, and bounded iterations retain SGP4's branch semantics. No derivative across a genuine discontinuity is claimed. Validate a kernel change with reference-state conformance, first-derivative step sweeps, and Hessian/Taylor tests, not merely successful compilation.

Every propagation initializes a fresh record, including deep-space resonance integrator state. Never cache one mutable record across residual calls or threads. Observation time, element epoch, gravity constants, and chart selection are treated as constants. The unmodified Vallado implementation supplies public propagation, scoring, and finite-difference checks; the adapted scalar kernel supplies AD. Shared lineage means agreement between them is not independent orbit-truth evidence.

## One delivery decision

`prepare_products` receives the original fit, options, and observations. It validates bounds/order and checks epoch/window sample-count consistency, emits OMM and TLE, reparses the emitted bytes, and scores the full in-window span. This pairing is a caller contract: matching counts/epoch do not prove that arbitrary replaced observations came from the original solve. Do not mutate the fit/options/samples between fitting and product preparation.

A `Delivery` owns optional exact output text, per-format validation, continuous metrics, policy, and the final decision/reason. The CLI and JSON encoder consume it without independently reconstructing acceptance. A product's physical pass is independent of continuous numerical acceptance. Overall delivery requires continuous acceptance, OMM acceptance, and, for `tle_required`, TLE acceptance. Error precedence and stable enum spellings are documented in [workflows](workflows.md).

The report interface moved to v2 because it now exposes OMM validation and an overall policy decision, and defines product acceptance independently of continuous convergence. The request format is unchanged. Treat value-type results as immutable snapshots when reporting/writing them.

## Cancellation and publication

The CLI's `main` owns an unregistered synchronous spdlog logger and its stderr sink. No global default logger is changed. Log levels are parsed before filesystem effects and applied after successful argument validation. All logging runs in normal execution context; the signal handler only sets its existing flag. Core library code returns errors and diagnostics instead of logging. `std::print`/`std::println` remain the command-output mechanism, including a last-resort stderr message if logging setup fails.

Cancellation checkpoints occur during request JSON parsing/sample conversion, input validation, factor creation/evaluation, monitor callbacks, final diagnostics, product evaluation, and CLI chunked I/O. `read_request` retains its optional stop token in the returned options. Some cancelled fits retain candidates; product preparation returns no delivery on cancellation. No deadline is promised for a single solver/propagator operation or parser allocation/destruction.

The process signal handler performs only a lock-free atomic store. Stop-source callbacks and cleanup run in normal thread context. A scoped owner restores previous handlers and joins its monitor; concurrent signal owners are rejected. Signals are process-wide even though numerical library calls are independently usable from multiple threads.

`StagingDirectory` exclusively creates a unique sibling and owns its cleanup. The only publication step is an OS no-replace rename. No earlier existence check is relied upon for correctness. The successful rename is the commit point; later cancellation does not undo it. Close failures reject publication, but files are not individually fsynced and the directory transaction is not a crash-durability guarantee. A stale directory not owned by the current instance is never automatically deleted.

## Verification boundaries

Public numerical/format tests, native CLI failure tests, compiled usage examples, installed-header compilation, relocated package checks, and separate-runner archive consumers cover distinct contracts. Sanitizers instrument authored code and the SGP4 kernels under the documented build options; they do not automatically instrument all upstream dependencies. Hosted-platform evidence and independent orbital qualification remain separately reported in [validation](validation.md).
