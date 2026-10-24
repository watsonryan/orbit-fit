/**
 * @file second_order.hpp
 * @brief Adapt nested Ceres Jets for bounded-storage residual Hessian evaluation.
 * @author watson
 */
#pragma once

#include <ceres/jet.h>

#include <cmath>
#include <compare>
#include <stdexcept>
#include <utility>

namespace orbitfit::detail {
/**
 * @brief Lift constants and scalar operations into bounded-storage nested Ceres Jets.
 * @details The inner seven tangents contain the gradient; one outer tangent
 * contains a selected directional derivative and its gradient, one Hessian row.
 * Repeating directions bounds stack use without hand-maintained derivative formulas.
 * Branch comparisons inspect only the primal value; smoothness remains local.
 */
struct SecondOrder {
  using First = ceres::Jet<double, 7>;  ///< Primal and seven first partial derivatives.
  using Nested = ceres::Jet<First, 1>;  ///< One outer direction over the full inner gradient.
  Nested jet;  ///< jet.a.a: value; jet.a.v: gradient; jet.v[0].v: selected Hessian row.

  /** @brief Construct zero with zero tangents using Ceres' default initialization. */
  SecondOrder() = default;
  /** @brief Implicitly lift a scalar constant with zero tangents. @param value Real constant. */
  SecondOrder(double value) : jet(First(value)) {}  // Intentional scalar conversion.
  /** @brief Wrap an evaluated nested Jet. @param value Value and both derivative levels. */
  explicit SecondOrder(Nested value) : jet(std::move(value)) {}

  /** @brief Seed a coordinate. @param value Primal coordinate. @param index Inner tangent index in [0,6].
   * @param direction Selected Hessian row in [0,6]. @return Independently seeded coordinate. */
  static SecondOrder variable(double value, int index, int direction) {
    Nested result{First(value, index)};
    result.v[0].a = index == direction ? 1 : 0;
    return SecondOrder(result);
  }

  /** @brief Unary identity. @param x Operand. @return Unchanged value and derivatives. */
  friend SecondOrder operator+(const SecondOrder& x) { return x; }
  /** @brief Negate both derivative levels. @param x Operand. @return Negated scalar. */
  friend SecondOrder operator-(const SecondOrder& x) { return SecondOrder(-x.jet); }
  /** @brief Add nested Jets. @param x Left operand. @param y Right operand. @return Sum and derivatives. */
  friend SecondOrder operator+(const SecondOrder& x, const SecondOrder& y) {
    return SecondOrder(x.jet + y.jet);
  }
  /** @brief Subtract nested Jets. @param x Minuend. @param y Subtrahend. @return Difference and derivatives.
   */
  friend SecondOrder operator-(const SecondOrder& x, const SecondOrder& y) {
    return SecondOrder(x.jet - y.jet);
  }
  /** @brief Apply the nested product rule. @param x Left factor. @param y Right factor. @return Product and
   * derivatives. */
  friend SecondOrder operator*(const SecondOrder& x, const SecondOrder& y) {
    return SecondOrder(x.jet * y.jet);
  }
  /** @brief Apply the nested quotient rule. @param x Numerator. @param y Nonzero denominator. @return
   * Quotient and derivatives. */
  friend SecondOrder operator/(const SecondOrder& x, const SecondOrder& y) {
    return SecondOrder(x.jet / y.jet);
  }
  /** @brief Accumulate a sum. @param other Addend. @return This scalar after addition. */
  SecondOrder& operator+=(const SecondOrder& other) { return *this = *this + other; }
  /** @brief Accumulate a difference. @param other Subtrahend. @return This scalar after subtraction. */
  SecondOrder& operator-=(const SecondOrder& other) { return *this = *this - other; }
  /** @brief Accumulate a product. @param other Factor. @return This scalar after multiplication. */
  SecondOrder& operator*=(const SecondOrder& other) { return *this = *this * other; }
  /** @brief Accumulate a quotient. @param other Nonzero divisor. @return This scalar after division. */
  SecondOrder& operator/=(const SecondOrder& other) { return *this = *this / other; }

  // Branch selection depends only on the value, just as in the first-order path.
  /** @brief Compare primal values for branch selection. @param x Left operand. @param y Right operand.
   * @return Value equality, ignoring derivatives. */
  friend bool operator==(const SecondOrder& x, const SecondOrder& y) { return x.jet.a.a == y.jet.a.a; }
  /** @brief Order primal values for branch selection. @param x Left operand. @param y Right operand. @return
   * Floating-point partial ordering. */
  friend std::partial_ordering operator<=>(const SecondOrder& x, const SecondOrder& y) {
    return x.jet.a.a <=> y.jet.a.a;
  }
  /** @brief Extract the primal for validation/branching. @param x Scalar. @return Value without derivatives.
   */
  friend double value(const SecondOrder& x) { return x.jet.a.a; }
  /** @brief Differentiate sine at both levels. @param x Radians. @return Sine and derivatives. */
  friend SecondOrder sin(const SecondOrder& x) { return SecondOrder(ceres::sin(x.jet)); }
  /** @brief Differentiate cosine at both levels. @param x Radians. @return Cosine and derivatives. */
  friend SecondOrder cos(const SecondOrder& x) { return SecondOrder(ceres::cos(x.jet)); }
  /** @brief Differentiate square root. @param x Positive radicand for finite derivatives. @return Root and
   * derivatives. */
  friend SecondOrder sqrt(const SecondOrder& x) { return SecondOrder(ceres::sqrt(x.jet)); }
  /** @brief Differentiate the exponential. @param x Exponent. @return Exponential and derivatives. */
  friend SecondOrder exp(const SecondOrder& x) { return SecondOrder(ceres::exp(x.jet)); }
  /** @brief Differentiate arctangent. @param x Argument. @return Angle in radians and derivatives. */
  friend SecondOrder atan(const SecondOrder& x) { return SecondOrder(ceres::atan(x.jet)); }
  /** @brief Differentiate local polar angle. @param y Numerator coordinate. @param x Denominator coordinate.
   * @return atan2 angle and local derivatives; origin is singular. */
  friend SecondOrder atan2(const SecondOrder& y, const SecondOrder& x) {
    return SecondOrder(ceres::atan2(y.jet, x.jet));
  }
  /** @brief Differentiate Euclidean radius. @param x First coordinate. @param y Second coordinate. @return
   * Radius and derivatives; origin is singular. */
  friend SecondOrder hypot(const SecondOrder& x, const SecondOrder& y) {
    return SecondOrder(ceres::hypot(x.jet, y.jet));
  }
  /** @brief Differentiate a constant-exponent power. @param x Base in the differentiable domain. @param y
   * Constant exponent with zero derivatives at both levels. @return Power and derivatives. @throws
   * std::logic_error If y carries any derivative. */
  friend SecondOrder pow(const SecondOrder& x, const SecondOrder& y) {
    // Every exponent in this SGP4 kernel is constant. Use Ceres' constant
    // exponent overload; its general pow does not support unequal Jet widths.
    // Check the contract so a future variable exponent cannot lose derivatives.
    if (!y.jet.a.v.isZero(0) || y.jet.v[0].a != 0 || !y.jet.v[0].v.isZero(0))
      throw std::logic_error("The second-order SGP4 adapter requires constant power exponents");
    return SecondOrder(ceres::pow(x.jet, value(y)));
  }
  /** @brief Differentiate the executed sign branch. @param x Scalar. @return Absolute value; no two-sided
   * derivative is claimed at zero. */
  friend SecondOrder abs(const SecondOrder& x) { return value(x) < 0 ? -x : x; }
  /** @brief Select the local floor value. @param x Scalar. @return Integral value with zero tangents;
   * discontinuities are not differentiated. */
  friend SecondOrder floor(const SecondOrder& x) { return SecondOrder(std::floor(value(x))); }
  /** @brief Differentiate the fixed remainder quotient. @param x Dividend. @param y Nonzero divisor. @return
   * libm remainder value with nested local branch derivatives. */
  friend SecondOrder sgp4_fmod(const SecondOrder& x, const SecondOrder& y) {
    // Differentiate the local quotient branch; preserve libm's primal value
    // instead of the potentially less accurate subtraction x - q*y.
    const double q = std::trunc(value(x) / value(y));
    SecondOrder result = x - q * y;
    result.jet.a.a = std::fmod(value(x), value(y));
    return result;
  }
};
}  // namespace orbitfit::detail
