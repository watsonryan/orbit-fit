#pragma once

#include <gtsam/base/Matrix.h>
#include <gtsam/base/Vector.h>

#include "orbitfit/orbitfit.hpp"

namespace orbitfit::detail {
// Equinoctial-style chart of SGP4 mean elements. The chart is selected once
// from the seed; no discontinuous parameter switching inside an optimization.
class Chart {
 public:
  Chart(const Elements& seed, bool drag);
  [[nodiscard]] Elements decode(const gtsam::Vector& offset) const;
  [[nodiscard]] std::size_t dimension() const { return drag_ ? 7 : 6; }

 private:
  Elements seed_;
  gtsam::Vector base_;
  bool retrograde_;
  bool drag_;
};

[[nodiscard]] gtsam::Vector state_residual(const Chart& chart, const gtsam::Vector& offset,
                                           const Sample& sample, bool velocity);
[[nodiscard]] gtsam::Matrix state_jacobian(const Chart& chart, const gtsam::Vector& offset,
                                           const Sample& sample, bool velocity, double step = 1e-4);
}  // namespace orbitfit::detail
