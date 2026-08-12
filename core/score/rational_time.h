// ============================================================================
// core/score/rational_time.h - exact rational musical time for ScoreIR.
//
// Lane: M0-001 minimal vertical slice. DRAFT implementation, NON-FROZEN.
// The Frozen Baseline (spec/v0.1) mandates exact rational musical time:
//   * decision M1  - exact rational numbers for musical time
//   * spec/v0.1/02 S4.5 - ScoreIR time model: exact duration, tuplet ratio
//   * spec/v0.1/02 S8.3 - MusicalTime is exact rational; PlaybackTime (seconds)
//     is derived and separate
// Full policy documentation lives in docs/rational-time-notes.md.
//
// Design summary
//   * Immutable-by-convention value type; every constructor/operator returns a
//     fully reduced rational with a strictly positive denominator.
//   * Canonical zero is 0/1.
//   * Negative mathematical values are fully supported (field-level
//     non-negativity constraints are intentionally out of scope here).
//   * Exact total ordering; comparison never overflows (magnitude-based
//     Euclidean algorithm, no int64 cross-multiplication).
//   * Arithmetic is checked and exact. add/subtract work on exact two-limb
//     unsigned 128-bit values built from uint64 pairs (portable standard C++;
//     no compiler extensions, no arbitrary precision) and reduce by factor
//     cancellation: gcd of the exact sum against each denominator factor
//     (g, b/g, d/g) is removed before any product is materialized, so a
//     representable reduced result is never rejected because an unreduced
//     intermediate or lcm overflowed int64. multiply/divide use
//     cross-cancellation in unsigned magnitudes with signed reconstruction,
//     and never narrow a uint64 gcd (including the 2^63 value of
//     gcd(int64_min, int64_min)) into int64. There is NO wrap-around, NO
//     saturation, and NO floating-point fallback anywhere in the value
//     computation path.
//   * Overflow: any value that cannot be represented as an int64 numerator /
//     positive int64 denominator throws std::overflow_error.
//   * Errors: zero denominator (construction), division by zero, and
//     reciprocal of zero throw std::domain_error.
//
// Canonical JSON (draft wire format; whole note == 1)
//   * to_canonical_json()   -> exactly  {"numerator":"<signed int64>",
//     "denominator":"<positive int64>"}
//   * from_canonical_json() parses exactly that object (JSON whitespace is
//     permitted between tokens; unknown, missing and duplicate fields are
//     rejected). Values must be JSON strings with canonical decimal integer
//     syntax: numerator is "0" or "-?[1-9][0-9]*" (no '+', no leading zeros,
//     no "-0"), denominator is "[1-9][0-9]*". The fraction must be fully
//     reduced (zero requires denominator 1). Malformed or noncanonical JSON
//     throws std::invalid_argument; values outside the int64 range throw
//     std::out_of_range. Values are never clamped or silently normalized.
//
// C++20 header + source. No external dependencies.
// ============================================================================

#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <limits>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace choirloom::score {

// Exact rational musical time. Whole note == 1, so a quarter note is 1/4.
//
// Invariant (always holds after construction):
//   * den_ > 0
//   * gcd(|num_|, den_) == 1
//   * zero is stored as (0, 1)
class RationalTime {
public:
    using Int = std::int64_t;

    static constexpr Int kMin = (std::numeric_limits<Int>::min)();
    static constexpr Int kMax = (std::numeric_limits<Int>::max)();

    // -- Construction --------------------------------------------------------

    /// Canonical zero (0/1).
    constexpr RationalTime() noexcept : num_(0), den_(1) {}

    /// Whole value n (denominator 1).
    constexpr explicit RationalTime(Int whole) noexcept : num_(whole), den_(1) {}

    /// Reduced fraction numerator/denominator.
    /// Throws std::domain_error if denominator == 0.
    /// Throws std::overflow_error if the reduced form is not representable
    /// (numerator outside int64, or denominator > int64 max).
    constexpr RationalTime(Int numerator, Int denominator)
        : num_(0), den_(1)
    {
        if (denominator == 0) {
            throw std::domain_error("RationalTime: zero denominator");
        }
        const bool negative = (numerator < 0) != (denominator < 0);
        std::uint64_t an = magnitude(numerator);
        std::uint64_t ad = magnitude(denominator);
        const std::uint64_t g = gcd_u64(an, ad);
        an /= g;
        ad /= g;
        if (ad > static_cast<std::uint64_t>(kMax)) {
            throw std::overflow_error("RationalTime: denominator out of range");
        }
        if (negative) {
            if (an > kMinMagnitude) {
                throw std::overflow_error("RationalTime: numerator out of range");
            }
            num_ = (an == kMinMagnitude) ? kMin : -static_cast<Int>(an);
        } else {
            if (an > static_cast<std::uint64_t>(kMax)) {
                throw std::overflow_error("RationalTime: numerator out of range");
            }
            num_ = static_cast<Int>(an);
        }
        den_ = static_cast<Int>(ad);
    }

    /// Canonical zero (0/1).
    static constexpr RationalTime zero() noexcept { return RationalTime(); }

    // -- Accessors / predicates ----------------------------------------------

    /// Reduced numerator (sign carried here).
    constexpr Int numerator() const noexcept { return num_; }

    /// Reduced positive denominator.
    constexpr Int denominator() const noexcept { return den_; }

    constexpr bool is_zero() const noexcept { return num_ == 0; }
    constexpr bool is_positive() const noexcept { return num_ > 0; }
    constexpr bool is_negative() const noexcept { return num_ < 0; }
    constexpr bool is_integer() const noexcept { return den_ == 1; }

    // -- Unary arithmetic ----------------------------------------------------

    constexpr RationalTime operator+() const noexcept { return *this; }

    /// Throws std::overflow_error if *this == kMin (negation not representable).
    constexpr RationalTime operator-() const
    {
        if (num_ == kMin) {
            throw std::overflow_error("RationalTime: negate overflow");
        }
        return RationalTime(-num_, den_);
    }

    /// 1 / *this. Throws std::domain_error if *this is zero; throws
    /// std::overflow_error if the result is not representable.
    constexpr RationalTime reciprocal() const
    {
        if (num_ == 0) {
            throw std::domain_error("RationalTime: reciprocal of zero");
        }
        if (num_ < 0) {
            if (num_ == kMin) {
                throw std::overflow_error("RationalTime: reciprocal overflow");
            }
            return RationalTime(-den_, -num_);
        }
        return RationalTime(den_, num_);
    }

    // -- Binary arithmetic (checked and exact; see header comment) -----------

    constexpr RationalTime& operator+=(RationalTime const& rhs)
    {
        auto const p = add_pair(num_, den_, rhs.num_, rhs.den_);
        num_ = p.first;
        den_ = p.second;
        return *this;
    }

    constexpr RationalTime& operator-=(RationalTime const& rhs)
    {
        auto const p = sub_pair(num_, den_, rhs.num_, rhs.den_);
        num_ = p.first;
        den_ = p.second;
        return *this;
    }

    constexpr RationalTime& operator*=(RationalTime const& rhs)
    {
        auto const p = mul_pair(num_, den_, rhs.num_, rhs.den_);
        num_ = p.first;
        den_ = p.second;
        return *this;
    }

    constexpr RationalTime& operator/=(RationalTime const& rhs)
    {
        auto const p = div_pair(num_, den_, rhs.num_, rhs.den_);
        num_ = p.first;
        den_ = p.second;
        return *this;
    }

    friend constexpr RationalTime operator+(RationalTime a, RationalTime const& b)
    {
        a += b;
        return a;
    }
    friend constexpr RationalTime operator-(RationalTime a, RationalTime const& b)
    {
        a -= b;
        return a;
    }
    friend constexpr RationalTime operator*(RationalTime a, RationalTime const& b)
    {
        a *= b;
        return a;
    }
    friend constexpr RationalTime operator/(RationalTime a, RationalTime const& b)
    {
        a /= b;
        return a;
    }

    // -- Comparison (exact total ordering; never overflows) -------------------

    friend constexpr bool operator==(RationalTime a, RationalTime b) noexcept
    {
        return a.num_ == b.num_ && a.den_ == b.den_;
    }

    friend constexpr std::strong_ordering operator<=>(RationalTime a,
                                                      RationalTime b) noexcept
    {
        const int c = cmp_frac(a.num_, a.den_, b.num_, b.den_);
        return c < 0 ? std::strong_ordering::less
                     : c > 0 ? std::strong_ordering::greater
                             : std::strong_ordering::equal;
    }

    // -- Conversion / display -------------------------------------------------
    //
    // NOTE: to_double() is a DISPLAY/export helper only. It is never used as
    // source truth (frozen invariant: exact rational numbers, no float truth).

    constexpr double to_double() const noexcept
    {
        return static_cast<double>(num_) / static_cast<double>(den_);
    }

    /// Canonical decimal-string text form "num/den" (den > 0).
    std::string to_string() const;

    /// Canonical JSON wire form:
    ///   {"numerator":"<signed int64>","denominator":"<positive int64>"}
    std::string to_canonical_json() const;

    /// Parse exactly the canonical JSON object produced by to_canonical_json().
    /// Throws std::invalid_argument for malformed/noncanonical JSON and
    /// std::out_of_range for out-of-int64 wire values. See header comment.
    static RationalTime from_canonical_json(std::string_view json);

    friend std::ostream& operator<<(std::ostream& os, RationalTime t);

private:
    Int num_;  // reduced numerator; sign carried here
    Int den_;  // reduced denominator; always > 0; zero == 0/1

    // -- Internal helpers ----------------------------------------------------

    // Two-limb unsigned 128-bit value built from portable uint64 arithmetic.
    // Used only for exact intermediates that may exceed int64; no compiler
    // extensions and no arbitrary-precision library.
    struct U128 {
        std::uint64_t lo = 0;
        std::uint64_t hi = 0;
    };

    static constexpr std::uint64_t kMinMagnitude = (std::uint64_t(1) << 63);

    /// |v| as uint64 - exact even for Int::min.
    static constexpr std::uint64_t magnitude(Int v) noexcept
    {
        return v < 0 ? (std::uint64_t(-(v + 1)) + std::uint64_t(1))
                     : std::uint64_t(v);
    }

    /// Euclidean gcd over magnitudes.
    static constexpr std::uint64_t gcd_u64(std::uint64_t a,
                                           std::uint64_t b) noexcept
    {
        while (b != 0) {
            std::uint64_t const t = a % b;
            a = b;
            b = t;
        }
        return a;
    }

    // -- Two-limb (uint64 pair) arithmetic ------------------------------------

    // 64 x 64 -> 128 via 32-bit limb long multiplication.
    static constexpr U128 mul_u64(std::uint64_t x, std::uint64_t y) noexcept
    {
        const std::uint64_t xl = x & 0xFFFFFFFFu;
        const std::uint64_t xh = x >> 32;
        const std::uint64_t yl = y & 0xFFFFFFFFu;
        const std::uint64_t yh = y >> 32;
        const std::uint64_t ll = xl * yl;
        const std::uint64_t lh = xl * yh;
        const std::uint64_t hl = xh * yl;
        const std::uint64_t hh = xh * yh;
        const std::uint64_t t = ll >> 32;
        const std::uint64_t mid = (lh & 0xFFFFFFFFu) + (hl & 0xFFFFFFFFu) + t;
        const std::uint64_t lo = (mid << 32) | (ll & 0xFFFFFFFFu);
        const std::uint64_t hi = hh + (lh >> 32) + (hl >> 32) + (mid >> 32);
        return {lo, hi};
    }

    static constexpr U128 add_u128(U128 a, U128 b) noexcept
    {
        const std::uint64_t lo = a.lo + b.lo;
        const std::uint64_t carry = (lo < a.lo) ? 1 : 0;
        return {lo, a.hi + b.hi + carry};
    }

    // Requires a >= b.
    static constexpr U128 sub_u128(U128 a, U128 b) noexcept
    {
        const std::uint64_t lo = a.lo - b.lo;
        const std::uint64_t borrow = (a.lo < b.lo) ? 1 : 0;
        return {lo, a.hi - b.hi - borrow};
    }

    static constexpr bool ge_u128(U128 a, U128 b) noexcept
    {
        return a.hi > b.hi || (a.hi == b.hi && a.lo >= b.lo);
    }

    // -- Modular helpers (used by the two-limb Euclid gcd) ---------------------

    // (a + b) mod m; requires a, b < m and m > 0.
    static constexpr std::uint64_t addmod64(std::uint64_t a, std::uint64_t b,
                                            std::uint64_t m) noexcept
    {
        const std::uint64_t s = a + b;  // < 2m; m <= kMax < 2^63 so no wrap
        return s >= m ? s - m : s;
    }

    // (a * b) mod m via binary doubling; requires m > 0 (portable, O(log b)).
    static constexpr std::uint64_t mulmod64(std::uint64_t a, std::uint64_t b,
                                            std::uint64_t m) noexcept
    {
        std::uint64_t r = 0;
        std::uint64_t x = a % m;
        std::uint64_t y = b % m;
        while (y != 0) {
            if (y & 1) {
                r = addmod64(r, x, m);
            }
            x = addmod64(x, x, m);
            y >>= 1;
        }
        return r;
    }

    // x mod m; requires m > 0.
    static constexpr std::uint64_t mod_u128_u64(U128 x, std::uint64_t m) noexcept
    {
        if (m == 1) {
            return 0;
        }
        // 2^64 mod m = ((2^64 - 1) mod m + 1) mod m
        const std::uint64_t two64_mod =
            (std::numeric_limits<std::uint64_t>::max() % m + 1) % m;
        const std::uint64_t hi_part =
            mulmod64(x.hi % m, two64_mod, m);
        const std::uint64_t lo_part = x.lo % m;
        return addmod64(hi_part, lo_part, m);
    }

    // Euclid gcd of a 128-bit value and a positive uint64.
    static constexpr std::uint64_t gcd_u128_u64(U128 x, std::uint64_t m) noexcept
    {
        while (m != 0) {
            const std::uint64_t r = mod_u128_u64(x, m);
            x = U128{m, 0};
            m = r;
        }
        return x.lo;
    }

    // -- Exact 128-bit / 64-bit division (used only for exact divisions) ------

    static constexpr std::uint64_t bit_of(U128 x, int i) noexcept
    {
        return (i >= 64) ? ((x.hi >> (i - 64)) & 1u) : ((x.lo >> i) & 1u);
    }

    static constexpr U128 set_bit(U128 x, int i) noexcept
    {
        if (i >= 64) {
            x.hi |= (std::uint64_t(1) << (i - 64));
        } else {
            x.lo |= (std::uint64_t(1) << i);
        }
        return x;
    }

    // x / d via binary long division; requires d | x and d != 0.
    static constexpr U128 div_u128_u64(U128 x, std::uint64_t d) noexcept
    {
        U128 q{0, 0};
        std::uint64_t r = 0;
        for (int i = 127; i >= 0; --i) {
            r = (r << 1) | bit_of(x, i);
            if (r >= d) {
                r -= d;
                q = set_bit(q, i);
            }
        }
        return q;
    }

    // -- Result assembly / cancellation ---------------------------------------

    // Reconstruct a signed int64 from magnitude + sign with exact bounds.
    static constexpr Int make_int(bool neg, std::uint64_t mag)
    {
        if (neg) {
            if (mag > kMinMagnitude) {
                throw std::overflow_error("RationalTime: result out of range");
            }
            return (mag == kMinMagnitude) ? kMin : -static_cast<Int>(mag);
        }
        if (mag > static_cast<std::uint64_t>(kMax)) {
            throw std::overflow_error("RationalTime: result out of range");
        }
        return static_cast<Int>(mag);
    }

    // Canonical reduced form check (den > 0 and gcd(|num|, den) == 1).
    static constexpr bool is_canonical_form(Int num, Int den) noexcept
    {
        return den > 0 && gcd_u64(magnitude(num), magnitude(den)) == 1;
    }

    // Shared add/sub tail: s is the exact signed sum (or difference) of the
    // two cross products, g = gcd(b, d), b1 = b/g, d1 = d/g. Reduces by factor
    // cancellation so that an unreduced lcm or numerator that exceeds int64 is
    // never an obstacle when the reduced result is representable.
    static constexpr std::pair<Int, Int> finish_add_sub(bool s_neg, U128 s_mag,
                                                        std::uint64_t g,
                                                        std::uint64_t b1,
                                                        std::uint64_t d1)
    {
        if (s_mag.hi == 0 && s_mag.lo == 0) {
            return {0, 1};
        }
        const std::uint64_t h1 = gcd_u128_u64(s_mag, g);
        const U128 r1 = div_u128_u64(s_mag, h1);
        const std::uint64_t h2 = gcd_u128_u64(r1, b1);
        const U128 r2 = div_u128_u64(r1, h2);
        const std::uint64_t h3 = gcd_u128_u64(r2, d1);
        const U128 r3 = div_u128_u64(r2, h3);  // |reduced numerator|
        // reduced denominator = (g/h1) * (b1/h2) * (d1/h3).
        // (g/h1)*(b1/h2) <= g*b1 = b <= kMax, so d12.hi == 0.
        const U128 d12 = mul_u64(g / h1, b1 / h2);
        const U128 d123 = mul_u64(d12.lo, d1 / h3);
        if (r3.hi != 0) {
            throw std::overflow_error("RationalTime: add/sub result out of range");
        }
        if (d123.hi != 0 || d123.lo > static_cast<std::uint64_t>(kMax)) {
            throw std::overflow_error("RationalTime: add/sub result out of range");
        }
        const Int num = make_int(s_neg, r3.lo);
        return {num, static_cast<Int>(d123.lo)};
    }

    // a/b + c/d - inputs normalized (b, d > 0). Result reduced, den > 0.
    static constexpr std::pair<Int, Int> add_pair(Int a, Int b, Int c, Int d)
    {
        const std::uint64_t g = gcd_u64(magnitude(b), magnitude(d));
        const std::uint64_t b1 = magnitude(b) / g;
        const std::uint64_t d1 = magnitude(d) / g;
        const U128 p1 = mul_u64(magnitude(a), d1);
        const U128 p2 = mul_u64(magnitude(c), b1);
        const bool na = a < 0;
        const bool nc = c < 0;
        bool s_neg;
        U128 s_mag;
        if (na == nc) {
            s_mag = add_u128(p1, p2);
            s_neg = na;
        } else {
            if (ge_u128(p1, p2)) {
                s_mag = sub_u128(p1, p2);
                s_neg = na;
            } else {
                s_mag = sub_u128(p2, p1);
                s_neg = nc;
            }
        }
        return finish_add_sub(s_neg, s_mag, g, b1, d1);
    }

    // a/b - c/d - inputs normalized (b, d > 0). Result reduced, den > 0.
    static constexpr std::pair<Int, Int> sub_pair(Int a, Int b, Int c, Int d)
    {
        const std::uint64_t g = gcd_u64(magnitude(b), magnitude(d));
        const std::uint64_t b1 = magnitude(b) / g;
        const std::uint64_t d1 = magnitude(d) / g;
        const U128 p1 = mul_u64(magnitude(a), d1);
        const U128 p2 = mul_u64(magnitude(c), b1);
        const bool na = a < 0;
        const bool nc = c < 0;
        bool s_neg;
        U128 s_mag;
        if (na == nc) {
            if (ge_u128(p1, p2)) {
                s_mag = sub_u128(p1, p2);
                s_neg = na;
            } else {
                s_mag = sub_u128(p2, p1);
                s_neg = !na;
            }
        } else {
            s_mag = add_u128(p1, p2);
            s_neg = na;
        }
        return finish_add_sub(s_neg, s_mag, g, b1, d1);
    }

    // a/b * c/d with cross-cancellation in unsigned magnitudes. The reduced
    // result is representable iff the (bounded) products are, so this is exact.
    static constexpr std::pair<Int, Int> mul_pair(Int a, Int b, Int c, Int d)
    {
        const std::uint64_t mag_a = magnitude(a);
        const std::uint64_t mag_c = magnitude(c);
        const std::uint64_t mb = magnitude(b);
        const std::uint64_t md = magnitude(d);
        const std::uint64_t g1 = gcd_u64(mag_a, md);
        const std::uint64_t g2 = gcd_u64(mag_c, mb);
        const U128 num_mag = mul_u64(mag_a / g1, mag_c / g2);
        const U128 den_mag = mul_u64(mb / g2, md / g1);
        if (num_mag.hi != 0) {
            throw std::overflow_error("RationalTime: multiply result out of range");
        }
        if (den_mag.hi != 0 || den_mag.lo > static_cast<std::uint64_t>(kMax)) {
            throw std::overflow_error("RationalTime: multiply result out of range");
        }
        return {make_int((a < 0) != (c < 0), num_mag.lo),
                static_cast<Int>(den_mag.lo)};
    }

    // (a/b) / (c/d) with cross-cancellation in unsigned magnitudes; c != 0.
    // Never narrows a uint64 gcd (gcd of int64_min and int64_min is 2^63).
    static constexpr std::pair<Int, Int> div_pair(Int a, Int b, Int c, Int d)
    {
        if (c == 0) {
            throw std::domain_error("RationalTime: division by zero");
        }
        const std::uint64_t mag_a = magnitude(a);
        const std::uint64_t mag_c = magnitude(c);
        const std::uint64_t mb = magnitude(b);
        const std::uint64_t md = magnitude(d);
        const std::uint64_t g1 = gcd_u64(mag_a, mag_c);
        const std::uint64_t g2 = gcd_u64(mb, md);
        const U128 num_mag = mul_u64(mag_a / g1, md / g2);
        const U128 den_mag = mul_u64(mb / g2, mag_c / g1);
        if (num_mag.hi != 0) {
            throw std::overflow_error("RationalTime: divide result out of range");
        }
        if (den_mag.hi != 0 || den_mag.lo > static_cast<std::uint64_t>(kMax)) {
            throw std::overflow_error("RationalTime: divide result out of range");
        }
        return {make_int((a < 0) != (c < 0), num_mag.lo),
                static_cast<Int>(den_mag.lo)};
    }

    // -- Exact, overflow-free comparison of a/b vs c/d (b, d > 0). ------------
    // Returns -1, 0, or 1.

    static constexpr int cmp_frac(Int a, Int b, Int c, Int d) noexcept
    {
        if (a < 0 && c >= 0) {
            return -1;
        }
        if (a >= 0 && c < 0) {
            return 1;
        }
        if (a >= 0) {
            return cmp_nonneg(std::uint64_t(a), std::uint64_t(b),
                              std::uint64_t(c), std::uint64_t(d));
        }
        // Both negative: a/b < c/d  <=>  |a|/b > |c|/d.
        return -cmp_nonneg(magnitude(a), std::uint64_t(b),
                           magnitude(c), std::uint64_t(d));
    }

    // Compare A/B vs C/D for B, D > 0 and A, C >= 0 without overflow.
    // Euclidean remainder/reciprocal iteration; denominators strictly
    // decrease, so this terminates and never multiplies large int64s.
    static constexpr int cmp_nonneg(std::uint64_t A, std::uint64_t B,
                                    std::uint64_t C, std::uint64_t D) noexcept
    {
        if (A == C && B == D) {
            return 0;
        }
        const std::uint64_t q1 = A / B, r1 = A % B;
        const std::uint64_t q2 = C / D, r2 = C % D;
        if (q1 != q2) {
            return q1 < q2 ? -1 : 1;
        }
        if (r1 == 0) {
            return (r2 == 0) ? 0 : -1;
        }
        if (r2 == 0) {
            return 1;
        }
        // Equal integer parts and both in (0,1): compare reciprocals
        // (reversed): r1/B ? r2/D  <=>  B/r1 ? D/r2.
        return -cmp_nonneg(B, r1, D, r2);
    }
};

}  // namespace choirloom::score

namespace std {
template <>
struct hash<choirloom::score::RationalTime> {
    size_t operator()(choirloom::score::RationalTime const& t) const noexcept
    {
        using Int = choirloom::score::RationalTime::Int;
        const size_t h1 = std::hash<Int>()(t.numerator());
        const size_t h2 = std::hash<Int>()(t.denominator());
        return h1 ^ (h2 + size_t(0x9E3779B97F4A7C15ULL) + (h1 << 6) + (h1 >> 2));
    }
};
}  // namespace std
