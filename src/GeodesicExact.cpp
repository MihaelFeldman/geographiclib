/**
 * \file GeodesicExact.cpp
 * \brief Implementation for GeographicLib::GeodesicExact class
 *
 * Copyright (c) Charles Karney (2012-2014) <charles@karney.com> and licensed
 * under the MIT/X11 License.  For more information, see
 * http://geographiclib.sourceforge.net/
 *
 * This is a reformulation of the geodesic problem.  The notation is as
 * follows:
 * - at a general point (no suffix or 1 or 2 as suffix)
 *   - phi = latitude
 *   - beta = latitude on auxiliary sphere
 *   - omega = longitude on auxiliary sphere
 *   - lambda = longitude
 *   - alpha = azimuth of great circle
 *   - sigma = arc length along great circle
 *   - s = distance
 *   - tau = scaled distance (= sigma at multiples of pi/2)
 * - at northwards equator crossing
 *   - beta = phi = 0
 *   - omega = lambda = 0
 *   - alpha = alpha0
 *   - sigma = s = 0
 * - a 12 suffix means a difference, e.g., s12 = s2 - s1.
 * - s and c prefixes mean sin and cos
 **********************************************************************/

#include <GeographicLib/GeodesicExact.hpp>
#include <GeographicLib/GeodesicLineExact.hpp>

#if defined(_MSC_VER)
// Squelch warnings about potentially uninitialized local variables and
// constant conditional expressions
#  pragma warning (disable: 4701 4127)
#endif

namespace GeographicLib {

  using namespace std;

  GeodesicExact::GeodesicExact(real a, real f)
    : maxit2_(maxit1_ + Math::digits() + 10)
      // Underflow guard.  We require
      //   tiny_ * epsilon() > 0
      //   tiny_ + epsilon() == epsilon()
    , tiny_(sqrt(numeric_limits<real>::min()))
    , tol0_(numeric_limits<real>::epsilon())
      // Increase multiplier in defn of tol1_ from 100 to 200 to fix inverse
      // case 52.784459512564 0 -52.784459512563990912 179.634407464943777557
      // which otherwise failed for Visual Studio 10 (Release and Debug)
    , tol1_(200 * tol0_)
    , tol2_(sqrt(tol0_))
      // Check on bisection interval
    , tolb_(tol0_ * tol2_)
    , xthresh_(1000 * tol2_)
    , _a(a)
    , _f(f <= 1 ? f : 1/f)
    , _f1(1 - _f)
    , _e2(_f * (2 - _f))
    , _ep2(_e2 / Math::sq(_f1))       // e2 / (1 - e2)
    , _n(_f / ( 2 - _f))
    , _b(_a * _f1)
    , _c2((Math::sq(_a) + Math::sq(_b) *
           (_e2 == 0 ? 1 :
            (_e2 > 0 ? Math::atanh(sqrt(_e2)) : atan(sqrt(-_e2))) /
            sqrt(abs(_e2))))/2) // authalic radius squared
      // The sig12 threshold for "really short".  Using the auxiliary sphere
      // solution with dnm computed at (bet1 + bet2) / 2, the relative error in
      // the azimuth consistency check is sig12^2 * abs(f) * min(1, 1-f/2) / 2.
      // (Error measured for 1/100 < b/a < 100 and abs(f) >= 1/1000.  For a
      // given f and sig12, the max error occurs for lines near the pole.  If
      // the old rule for computing dnm = (dn1 + dn2)/2 is used, then the error
      // increases by a factor of 2.)  Setting this equal to epsilon gives
      // sig12 = etol2.  Here 0.1 is a safety factor (error decreased by 100)
      // and max(0.001, abs(f)) stops etol2 getting too large in the nearly
      // spherical case.
    , _etol2(0.1 * tol2_ /
             sqrt( max(real(0.001), abs(_f)) * min(real(1), 1 - _f/2) / 2 ))
  {
    if (!(Math::isfinite(_a) && _a > 0))
      throw GeographicErr("Major radius is not positive");
    if (!(Math::isfinite(_b) && _b > 0))
      throw GeographicErr("Minor radius is not positive");
    C4coeff();
  }

  const GeodesicExact& GeodesicExact::WGS84() {
    static const GeodesicExact wgs84(Constants::WGS84_a(),
                                     Constants::WGS84_f());
    return wgs84;
  }

  Math::real GeodesicExact::CosSeries(real sinx, real cosx,
                                      const real c[], int n) {
    // Evaluate
    // y = sum(c[i] * cos((2*i+1) * x), i, 0, n-1)
    // using Clenshaw summation.
    // Approx operation count = (n + 5) mult and (2 * n + 2) add
    c += n ;                    // Point to one beyond last element
    real
      ar = 2 * (cosx - sinx) * (cosx + sinx), // 2 * cos(2 * x)
      y0 = n & 1 ? *--c : 0, y1 = 0;          // accumulators for sum
    // Now n is even
    n /= 2;
    while (n--) {
      // Unroll loop x 2, so accumulators return to their original role
      y1 = ar * y0 - y1 + *--c;
      y0 = ar * y1 - y0 + *--c;
    }
    return cosx * (y0 - y1);    // cos(x) * (y0 - y1)
  }

  GeodesicLineExact GeodesicExact::Line(real lat1, real lon1, real azi1,
                                        unsigned caps) const {
    return GeodesicLineExact(*this, lat1, lon1, azi1, caps);
  }

  Math::real GeodesicExact::GenDirect(real lat1, real lon1, real azi1,
                                      bool arcmode, real s12_a12,
                                      unsigned outmask,
                                      real& lat2, real& lon2, real& azi2,
                                      real& s12, real& m12,
                                      real& M12, real& M21,
                                      real& S12) const {
    return GeodesicLineExact(*this, lat1, lon1, azi1,
                        // Automatically supply DISTANCE_IN if necessary
                        outmask | (arcmode ? NONE : DISTANCE_IN))
      .                         // Note the dot!
      GenPosition(arcmode, s12_a12, outmask,
                  lat2, lon2, azi2, s12, m12, M12, M21, S12);
  }

  Math::real GeodesicExact::GenInverse(real lat1, real lon1,
                                       real lat2, real lon2,
                                       unsigned outmask,
                                       real& s12, real& azi1, real& azi2,
                                       real& m12, real& M12, real& M21,
                                       real& S12) const {
    outmask &= OUT_ALL;
    // Compute longitude difference (AngDiff does this carefully).  Result is
    // in [-180, 180] but -180 is only for west-going geodesics.  180 is for
    // east-going and meridional geodesics.
    real lon12 = Math::AngDiff(Math::AngNormalize(lon1),
                               Math::AngNormalize(lon2));
    // If very close to being on the same half-meridian, then make it so.
    lon12 = AngRound(lon12);
    // Make longitude difference positive.
    int lonsign = lon12 >= 0 ? 1 : -1;
    lon12 *= lonsign;
    // If really close to the equator, treat as on equator.
    lat1 = AngRound(lat1);
    lat2 = AngRound(lat2);
    // Swap points so that point with higher (abs) latitude is point 1
    int swapp = abs(lat1) >= abs(lat2) ? 1 : -1;
    if (swapp < 0) {
      lonsign *= -1;
      swap(lat1, lat2);
    }
    // Make lat1 <= 0
    int latsign = lat1 < 0 ? 1 : -1;
    lat1 *= latsign;
    lat2 *= latsign;
    // Now we have
    //
    //     0 <= lon12 <= 180
    //     -90 <= lat1 <= 0
    //     lat1 <= lat2 <= -lat1
    //
    // longsign, swapp, latsign register the transformation to bring the
    // coordinates to this canonical form.  In all cases, 1 means no change was
    // made.  We make these transformations so that there are few cases to
    // check, e.g., on verifying quadrants in atan2.  In addition, this
    // enforces some symmetries in the results returned.

    real phi, sbet1, cbet1, sbet2, cbet2, s12x, m12x;
    // Initialize for the meridian.  No longitude calculation is done in this
    // case to let the parameter default to 0.
    EllipticFunction E(-_ep2);

    phi = lat1 * Math::degree();
    // Ensure cbet1 = +epsilon at poles
    sbet1 = _f1 * sin(phi);
    cbet1 = lat1 == -90 ? tiny_ : cos(phi);
    SinCosNorm(sbet1, cbet1);

    phi = lat2 * Math::degree();
    // Ensure cbet2 = +epsilon at poles
    sbet2 = _f1 * sin(phi);
    cbet2 = abs(lat2) == 90 ? tiny_ : cos(phi);
    SinCosNorm(sbet2, cbet2);

    // If cbet1 < -sbet1, then cbet2 - cbet1 is a sensitive measure of the
    // |bet1| - |bet2|.  Alternatively (cbet1 >= -sbet1), abs(sbet2) + sbet1 is
    // a better measure.  This logic is used in assigning calp2 in Lambda12.
    // Sometimes these quantities vanish and in that case we force bet2 = +/-
    // bet1 exactly.  An example where is is necessary is the inverse problem
    // 48.522876735459 0 -48.52287673545898293 179.599720456223079643
    // which failed with Visual Studio 10 (Release and Debug)

    if (cbet1 < -sbet1) {
      if (cbet2 == cbet1)
        sbet2 = sbet2 < 0 ? sbet1 : -sbet1;
    } else {
      if (abs(sbet2) == -sbet1)
        cbet2 = cbet1;
    }

    real
      dn1 = (_f >= 0 ? sqrt(1 + _ep2 * Math::sq(sbet1)) :
             sqrt(1 - _e2 * Math::sq(cbet1)) / _f1),
      dn2 = (_f >= 0 ? sqrt(1 + _ep2 * Math::sq(sbet2)) :
             sqrt(1 - _e2 * Math::sq(cbet2)) / _f1);

    real
      lam12 = lon12 * Math::degree(),
      slam12 = abs(lon12) == 180 ? 0 : sin(lam12),
      clam12 = cos(lam12);      // lon12 == 90 isn't interesting

    // initial values to suppress warning
    real a12, sig12, calp1, salp1, calp2 = 0, salp2 = 0;

    bool meridian = lat1 == -90 || slam12 == 0;

    if (meridian) {

      // Endpoints are on a single full meridian, so the geodesic might lie on
      // a meridian.

      calp1 = clam12; salp1 = slam12; // Head to the target longitude
      calp2 = 1; salp2 = 0;           // At the target we're heading north

      real
        // tan(bet) = tan(sig) * cos(alp)
        ssig1 = sbet1, csig1 = calp1 * cbet1,
        ssig2 = sbet2, csig2 = calp2 * cbet2;

      // sig12 = sig2 - sig1
      sig12 = atan2(max(csig1 * ssig2 - ssig1 * csig2, real(0)),
                    csig1 * csig2 + ssig1 * ssig2);
      {
        real dummy;
        Lengths(E, sig12, ssig1, csig1, dn1, ssig2, csig2, dn2,
                cbet1, cbet2, s12x, m12x, dummy,
                (outmask & GEODESICSCALE) != 0U, M12, M21);
      }
      // Add the check for sig12 since zero length geodesics might yield m12 <
      // 0.  Test case was
      //
      //    echo 20.001 0 20.001 0 | GeodSolve -i
      //
      // In fact, we will have sig12 > pi/2 for meridional geodesic which is
      // not a shortest path.
      if (sig12 < 1 || m12x >= 0) {
        m12x *= _b;
        s12x *= _b;
        a12 = sig12 / Math::degree();
      } else
        // m12 < 0, i.e., prolate and too close to anti-podal
        meridian = false;
    }

    real omg12 = 0;             // initial value to suppress warning
    if (!meridian &&
        sbet1 == 0 &&   // and sbet2 == 0
        // Mimic the way Lambda12 works with calp1 = 0
        (_f <= 0 || lam12 <= Math::pi() - _f * Math::pi())) {

      // Geodesic runs along equator
      calp1 = calp2 = 0; salp1 = salp2 = 1;
      s12x = _a * lam12;
      sig12 = omg12 = lam12 / _f1;
      m12x = _b * sin(sig12);
      if (outmask & GEODESICSCALE)
        M12 = M21 = cos(sig12);
      a12 = lon12 / _f1;

    } else if (!meridian) {

      // Now point1 and point2 belong within a hemisphere bounded by a
      // meridian and geodesic is neither meridional or equatorial.

      // Figure a starting point for Newton's method
      real dnm;
      sig12 = InverseStart(E, sbet1, cbet1, dn1, sbet2, cbet2, dn2,
                           lam12,
                           salp1, calp1, salp2, calp2, dnm);

      if (sig12 >= 0) {
        // Short lines (InverseStart sets salp2, calp2, dnm)
        s12x = sig12 * _b * dnm;
        m12x = Math::sq(dnm) * _b * sin(sig12 / dnm);
        if (outmask & GEODESICSCALE)
          M12 = M21 = cos(sig12 / dnm);
        a12 = sig12 / Math::degree();
        omg12 = lam12 / (_f1 * dnm);
      } else {

        // Newton's method.  This is a straightforward solution of f(alp1) =
        // lambda12(alp1) - lam12 = 0 with one wrinkle.  f(alp) has exactly one
        // root in the interval (0, pi) and its derivative is positive at the
        // root.  Thus f(alp) is positive for alp > alp1 and negative for alp <
        // alp1.  During the course of the iteration, a range (alp1a, alp1b) is
        // maintained which brackets the root and with each evaluation of
        // f(alp) the range is shrunk, if possible.  Newton's method is
        // restarted whenever the derivative of f is negative (because the new
        // value of alp1 is then further from the solution) or if the new
        // estimate of alp1 lies outside (0,pi); in this case, the new starting
        // guess is taken to be (alp1a + alp1b) / 2.
        //
        // initial values to suppress warnings (if loop is executed 0 times)
        real ssig1 = 0, csig1 = 0, ssig2 = 0, csig2 = 0;
        unsigned numit = 0;
        // Bracketing range
        real salp1a = tiny_, calp1a = 1, salp1b = tiny_, calp1b = -1;
        for (bool tripn = false, tripb = false;
             numit < maxit2_ || GEOGRAPHICLIB_PANIC;
             ++numit) {
          // 1/4 meridan = 10e6 m and random input.  max err is estimated max
          // error in nm (checking solution of inverse problem by direct
          // solution).  iter is mean and sd of number of iterations
          //
          //           max   iter
          // log2(b/a) err mean  sd
          //    -7     387 5.33 3.68
          //    -6     345 5.19 3.43
          //    -5     269 5.00 3.05
          //    -4     210 4.76 2.44
          //    -3     115 4.55 1.87
          //    -2      69 4.35 1.38
          //    -1      36 4.05 1.03
          //     0      15 0.01 0.13
          //     1      25 5.10 1.53
          //     2      96 5.61 2.09
          //     3     318 6.02 2.74
          //     4     985 6.24 3.22
          //     5    2352 6.32 3.44
          //     6    6008 6.30 3.45
          //     7   19024 6.19 3.30
          real dv;
          real v = Lambda12(sbet1, cbet1, dn1, sbet2, cbet2, dn2, salp1, calp1,
                            salp2, calp2, sig12, ssig1, csig1, ssig2, csig2,
                            E, omg12, numit < maxit1_, dv) - lam12;
          // 2 * tol0 is approximately 1 ulp for a number in [0, pi].
          // Reversed test to allow escape with NaNs
          if (tripb || !(abs(v) >= (tripn ? 8 : 2) * tol0_)) break;
          // Update bracketing values
          if (v > 0 && (numit > maxit1_ || calp1/salp1 > calp1b/salp1b))
            { salp1b = salp1; calp1b = calp1; }
          else if (v < 0 && (numit > maxit1_ || calp1/salp1 < calp1a/salp1a))
            { salp1a = salp1; calp1a = calp1; }
          if (numit < maxit1_ && dv > 0) {
            real
              dalp1 = -v/dv;
            real
              sdalp1 = sin(dalp1), cdalp1 = cos(dalp1),
              nsalp1 = salp1 * cdalp1 + calp1 * sdalp1;
            if (nsalp1 > 0 && abs(dalp1) < Math::pi()) {
              calp1 = calp1 * cdalp1 - salp1 * sdalp1;
              salp1 = nsalp1;
              SinCosNorm(salp1, calp1);
              // In some regimes we don't get quadratic convergence because
              // slope -> 0.  So use convergence conditions based on epsilon
              // instead of sqrt(epsilon).
              tripn = abs(v) <= 16 * tol0_;
              continue;
            }
          }
          // Either dv was not postive or updated value was outside legal
          // range.  Use the midpoint of the bracket as the next estimate.
          // This mechanism is not needed for the WGS84 ellipsoid, but it does
          // catch problems with more eccentric ellipsoids.  Its efficacy is
          // such for the WGS84 test set with the starting guess set to alp1 =
          // 90deg:
          // the WGS84 test set: mean = 5.21, sd = 3.93, max = 24
          // WGS84 and random input: mean = 4.74, sd = 0.99
          salp1 = (salp1a + salp1b)/2;
          calp1 = (calp1a + calp1b)/2;
          SinCosNorm(salp1, calp1);
          tripn = false;
          tripb = (abs(salp1a - salp1) + (calp1a - calp1) < tolb_ ||
                   abs(salp1 - salp1b) + (calp1 - calp1b) < tolb_);
        }
        {
          real dummy;
          Lengths(E, sig12, ssig1, csig1, dn1, ssig2, csig2, dn2,
                  cbet1, cbet2, s12x, m12x, dummy,
                  (outmask & GEODESICSCALE) != 0U, M12, M21);
        }
        m12x *= _b;
        s12x *= _b;
        a12 = sig12 / Math::degree();
      }
    }

    if (outmask & DISTANCE)
      s12 = 0 + s12x;           // Convert -0 to 0

    if (outmask & REDUCEDLENGTH)
      m12 = 0 + m12x;           // Convert -0 to 0

    if (outmask & AREA) {
      real
        // From Lambda12: sin(alp1) * cos(bet1) = sin(alp0)
        salp0 = salp1 * cbet1,
        calp0 = Math::hypot(calp1, salp1 * sbet1); // calp0 > 0
      real alp12;
      if (calp0 != 0 && salp0 != 0) {
        real
          // From Lambda12: tan(bet) = tan(sig) * cos(alp)
          ssig1 = sbet1, csig1 = calp1 * cbet1,
          ssig2 = sbet2, csig2 = calp2 * cbet2,
          k2 = Math::sq(calp0) * _ep2,
          eps = k2 / (2 * (1 + sqrt(1 + k2)) + k2),
          // Multiplier = a^2 * e^2 * cos(alpha0) * sin(alpha0).
          A4 = Math::sq(_a) * calp0 * salp0 * _e2;
        SinCosNorm(ssig1, csig1);
        SinCosNorm(ssig2, csig2);
        real C4a[nC4_];
        C4f(eps, C4a);
        real
          B41 = CosSeries(ssig1, csig1, C4a, nC4_),
          B42 = CosSeries(ssig2, csig2, C4a, nC4_);
        S12 = A4 * (B42 - B41);
      } else
        // Avoid problems with indeterminate sig1, sig2 on equator
        S12 = 0;

      if (!meridian &&
          omg12 < real(0.75) * Math::pi() && // Long difference too big
          sbet2 - sbet1 < real(1.75)) {            // Lat difference too big
        // Use tan(Gamma/2) = tan(omg12/2)
        // * (tan(bet1/2)+tan(bet2/2))/(1+tan(bet1/2)*tan(bet2/2))
        // with tan(x/2) = sin(x)/(1+cos(x))
        real
          somg12 = sin(omg12), domg12 = 1 + cos(omg12),
          dbet1 = 1 + cbet1, dbet2 = 1 + cbet2;
        alp12 = 2 * atan2( somg12 * ( sbet1 * dbet2 + sbet2 * dbet1 ),
                           domg12 * ( sbet1 * sbet2 + dbet1 * dbet2 ) );
      } else {
        // alp12 = alp2 - alp1, used in atan2 so no need to normalize
        real
          salp12 = salp2 * calp1 - calp2 * salp1,
          calp12 = calp2 * calp1 + salp2 * salp1;
        // The right thing appears to happen if alp1 = +/-180 and alp2 = 0, viz
        // salp12 = -0 and alp12 = -180.  However this depends on the sign
        // being attached to 0 correctly.  The following ensures the correct
        // behavior.
        if (salp12 == 0 && calp12 < 0) {
          salp12 = tiny_ * calp1;
          calp12 = -1;
        }
        alp12 = atan2(salp12, calp12);
      }
      S12 += _c2 * alp12;
      S12 *= swapp * lonsign * latsign;
      // Convert -0 to 0
      S12 += 0;
    }

    // Convert calp, salp to azimuth accounting for lonsign, swapp, latsign.
    if (swapp < 0) {
      swap(salp1, salp2);
      swap(calp1, calp2);
      if (outmask & GEODESICSCALE)
        swap(M12, M21);
    }

    salp1 *= swapp * lonsign; calp1 *= swapp * latsign;
    salp2 *= swapp * lonsign; calp2 *= swapp * latsign;

    if (outmask & AZIMUTH) {
      // minus signs give range [-180, 180). 0- converts -0 to +0.
      azi1 = 0 - atan2(-salp1, calp1) / Math::degree();
      azi2 = 0 - atan2(-salp2, calp2) / Math::degree();
    }

    // Returned value in [0, 180]
    return a12;
  }

  void GeodesicExact::Lengths(const EllipticFunction& E,
                              real sig12,
                              real ssig1, real csig1, real dn1,
                              real ssig2, real csig2, real dn2,
                              real cbet1, real cbet2,
                              real& s12b, real& m12b, real& m0,
                              bool scalep, real& M12, real& M21) const {
    // Return m12b = (reduced length)/_b; also calculate s12b = distance/_b,
    // and m0 = coefficient of secular term in expression for reduced length.

    // It's OK to have repeated dummy arguments,
    // e.g., s12b = m0 = M12 = M21 = dummy
    m0 = - E.k2() * E.D() / (Math::pi() / 2);
    real J12 = m0 *
      (sig12 + E.deltaD(ssig2, csig2, dn2) - E.deltaD(ssig1, csig1, dn1));
    // Missing a factor of _b.
    // Add parens around (csig1 * ssig2) and (ssig1 * csig2) to ensure accurate
    // cancellation in the case of coincident points.
    m12b = dn2 * (csig1 * ssig2) - dn1 * (ssig1 * csig2) - csig1 * csig2 * J12;
    // Missing a factor of _b
    s12b = E.E() / (Math::pi() / 2) *
      (sig12 + E.deltaE(ssig2, csig2, dn2) - E.deltaE(ssig1, csig1, dn1));
    if (scalep) {
      real csig12 = csig1 * csig2 + ssig1 * ssig2;
      real t = _ep2 * (cbet1 - cbet2) * (cbet1 + cbet2) / (dn1 + dn2);
      M12 = csig12 + (t * ssig2 - csig2 * J12) * ssig1 / dn1;
      M21 = csig12 - (t * ssig1 - csig1 * J12) * ssig2 / dn2;
    }
  }

  Math::real GeodesicExact::Astroid(real x, real y) {
    // Solve k^4+2*k^3-(x^2+y^2-1)*k^2-2*y^2*k-y^2 = 0 for positive root k.
    // This solution is adapted from Geocentric::Reverse.
    real k;
    real
      p = Math::sq(x),
      q = Math::sq(y),
      r = (p + q - 1) / 6;
    if ( !(q == 0 && r <= 0) ) {
      real
        // Avoid possible division by zero when r = 0 by multiplying equations
        // for s and t by r^3 and r, resp.
        S = p * q / 4,            // S = r^3 * s
        r2 = Math::sq(r),
        r3 = r * r2,
        // The discrimant of the quadratic equation for T3.  This is zero on
        // the evolute curve p^(1/3)+q^(1/3) = 1
        disc = S * (S + 2 * r3);
      real u = r;
      if (disc >= 0) {
        real T3 = S + r3;
        // Pick the sign on the sqrt to maximize abs(T3).  This minimizes loss
        // of precision due to cancellation.  The result is unchanged because
        // of the way the T is used in definition of u.
        T3 += T3 < 0 ? -sqrt(disc) : sqrt(disc); // T3 = (r * t)^3
        // N.B. cbrt always returns the real root.  cbrt(-8) = -2.
        real T = Math::cbrt(T3); // T = r * t
        // T can be zero; but then r2 / T -> 0.
        u += T + (T ? r2 / T : 0);
      } else {
        // T is complex, but the way u is defined the result is real.
        real ang = atan2(sqrt(-disc), -(S + r3));
        // There are three possible cube roots.  We choose the root which
        // avoids cancellation.  Note that disc < 0 implies that r < 0.
        u += 2 * r * cos(ang / 3);
      }
      real
        v = sqrt(Math::sq(u) + q),    // guaranteed positive
        // Avoid loss of accuracy when u < 0.
        uv = u < 0 ? q / (v - u) : u + v, // u+v, guaranteed positive
        w = (uv - q) / (2 * v);           // positive?
      // Rearrange expression for k to avoid loss of accuracy due to
      // subtraction.  Division by 0 not possible because uv > 0, w >= 0.
      k = uv / (sqrt(uv + Math::sq(w)) + w);   // guaranteed positive
    } else {               // q == 0 && r <= 0
      // y = 0 with |x| <= 1.  Handle this case directly.
      // for y small, positive root is k = abs(y)/sqrt(1-x^2)
      k = 0;
    }
    return k;
  }

  Math::real GeodesicExact::InverseStart(EllipticFunction& E,
                                         real sbet1, real cbet1, real dn1,
                                         real sbet2, real cbet2, real dn2,
                                         real lam12,
                                         real& salp1, real& calp1,
                                         // Only updated if return val >= 0
                                         real& salp2, real& calp2,
                                         // Only updated for short lines
                                         real& dnm)
    const {
    // Return a starting point for Newton's method in salp1 and calp1 (function
    // value is -1).  If Newton's method doesn't need to be used, return also
    // salp2 and calp2 and function value is sig12.
    real
      sig12 = -1,               // Return value
      // bet12 = bet2 - bet1 in [0, pi); bet12a = bet2 + bet1 in (-pi, 0]
      sbet12 = sbet2 * cbet1 - cbet2 * sbet1,
      cbet12 = cbet2 * cbet1 + sbet2 * sbet1;
#if defined(__GNUC__) && __GNUC__ == 4 && \
  (__GNUC_MINOR__ < 6 || defined(__MINGW32__))
    // Volatile declaration needed to fix inverse cases
    // 88.202499451857 0 -88.202499451857 179.981022032992859592
    // 89.262080389218 0 -89.262080389218 179.992207982775375662
    // 89.333123580033 0 -89.333123580032997687 179.99295812360148422
    // which otherwise fail with g++ 4.4.4 x86 -O3 (Linux)
    // and g++ 4.4.0 (mingw) and g++ 4.6.1 (tdm mingw).
    real sbet12a;
    {
      GEOGRAPHICLIB_VOLATILE real xx1 = sbet2 * cbet1;
      GEOGRAPHICLIB_VOLATILE real xx2 = cbet2 * sbet1;
      sbet12a = xx1 + xx2;
    }
#else
    real sbet12a = sbet2 * cbet1 + cbet2 * sbet1;
#endif
    bool shortline = cbet12 >= 0 && sbet12 < real(0.5) &&
      cbet2 * lam12 < real(0.5);
    real omg12 = lam12;
    if (shortline) {
      real sbetm2 = Math::sq(sbet1 + sbet2);
      // sin((bet1+bet2)/2)^2
      // =  (sbet1 + sbet2)^2 / ((sbet1 + sbet2)^2 + (cbet1 + cbet2)^2)
      sbetm2 /= sbetm2 + Math::sq(cbet1 + cbet2);
      dnm = sqrt(1 + _ep2 * sbetm2);
      omg12 /= _f1 * dnm;
    }
    real somg12 = sin(omg12), comg12 = cos(omg12);

    salp1 = cbet2 * somg12;
    calp1 = comg12 >= 0 ?
      sbet12 + cbet2 * sbet1 * Math::sq(somg12) / (1 + comg12) :
      sbet12a - cbet2 * sbet1 * Math::sq(somg12) / (1 - comg12);

    real
      ssig12 = Math::hypot(salp1, calp1),
      csig12 = sbet1 * sbet2 + cbet1 * cbet2 * comg12;

    if (shortline && ssig12 < _etol2) {
      // really short lines
      salp2 = cbet1 * somg12;
      calp2 = sbet12 - cbet1 * sbet2 *
        (comg12 >= 0 ? Math::sq(somg12) / (1 + comg12) : 1 - comg12);
      SinCosNorm(salp2, calp2);
      // Set return value
      sig12 = atan2(ssig12, csig12);
    } else if (abs(_n) > real(0.1) || // Skip astroid calc if too eccentric
               csig12 >= 0 ||
               ssig12 >= 6 * abs(_n) * Math::pi() * Math::sq(cbet1)) {
      // Nothing to do, zeroth order spherical approximation is OK
    } else {
      // Scale lam12 and bet2 to x, y coordinate system where antipodal point
      // is at origin and singular point is at y = 0, x = -1.
      real y, lamscale, betscale;
      // Volatile declaration needed to fix inverse case
      // 56.320923501171 0 -56.320923501171 179.664747671772880215
      // which otherwise fails with g++ 4.4.4 x86 -O3
      GEOGRAPHICLIB_VOLATILE real x;
      if (_f >= 0) {            // In fact f == 0 does not get here
        // x = dlong, y = dlat
        {
          real k2 = Math::sq(sbet1) * _ep2;
          E.Reset(-k2, -_ep2, 1 + k2, 1 + _ep2);
          lamscale = _e2/_f1 * cbet1 * 2 * E.H();
        }
        betscale = lamscale * cbet1;

        x = (lam12 - Math::pi()) / lamscale;
        y = sbet12a / betscale;
      } else {                  // _f < 0
        // x = dlat, y = dlong
        real
          cbet12a = cbet2 * cbet1 - sbet2 * sbet1,
          bet12a = atan2(sbet12a, cbet12a);
        real m12b, m0, dummy;
        // In the case of lon12 = 180, this repeats a calculation made in
        // Inverse.
        Lengths(E, Math::pi() + bet12a,
                sbet1, -cbet1, dn1, sbet2, cbet2, dn2,
                cbet1, cbet2, dummy, m12b, m0, false,
                dummy, dummy);
        x = -1 + m12b / (cbet1 * cbet2 * m0 * Math::pi());
        betscale = x < -real(0.01) ? sbet12a / x :
          -_f * Math::sq(cbet1) * Math::pi();
        lamscale = betscale / cbet1;
        y = (lam12 - Math::pi()) / lamscale;
      }

      if (y > -tol1_ && x > -1 - xthresh_) {
        // strip near cut
        // Need real(x) here to cast away the volatility of x for min/max
        if (_f >= 0) {
          salp1 = min(real(1), -real(x)); calp1 = - sqrt(1 - Math::sq(salp1));
        } else {
          calp1 = max(real(x > -tol1_ ? 0 : -1), real(x));
          salp1 = sqrt(1 - Math::sq(calp1));
        }
      } else {
        // Estimate alp1, by solving the astroid problem.
        //
        // Could estimate alpha1 = theta + pi/2, directly, i.e.,
        //   calp1 = y/k; salp1 = -x/(1+k);  for _f >= 0
        //   calp1 = x/(1+k); salp1 = -y/k;  for _f < 0 (need to check)
        //
        // However, it's better to estimate omg12 from astroid and use
        // spherical formula to compute alp1.  This reduces the mean number of
        // Newton iterations for astroid cases from 2.24 (min 0, max 6) to 2.12
        // (min 0 max 5).  The changes in the number of iterations are as
        // follows:
        //
        // change percent
        //    1       5
        //    0      78
        //   -1      16
        //   -2       0.6
        //   -3       0.04
        //   -4       0.002
        //
        // The histogram of iterations is (m = number of iterations estimating
        // alp1 directly, n = number of iterations estimating via omg12, total
        // number of trials = 148605):
        //
        //  iter    m      n
        //    0   148    186
        //    1 13046  13845
        //    2 93315 102225
        //    3 36189  32341
        //    4  5396      7
        //    5   455      1
        //    6    56      0
        //
        // Because omg12 is near pi, estimate work with omg12a = pi - omg12
        real k = Astroid(x, y);
        real
          omg12a = lamscale * ( _f >= 0 ? -x * k/(1 + k) : -y * (1 + k)/k );
        somg12 = sin(omg12a); comg12 = -cos(omg12a);
        // Update spherical estimate of alp1 using omg12 instead of lam12
        salp1 = cbet2 * somg12;
        calp1 = sbet12a - cbet2 * sbet1 * Math::sq(somg12) / (1 - comg12);
      }
    }
    // Sanity check on starting guess.  Backwards check allows NaN through.
    if (!(salp1 <= 0))
      SinCosNorm(salp1, calp1);
    else {
      salp1 = 1; calp1 = 0;
    }
    return sig12;
  }

  Math::real GeodesicExact::Lambda12(real sbet1, real cbet1, real dn1,
                                     real sbet2, real cbet2, real dn2,
                                     real salp1, real calp1,
                                     real& salp2, real& calp2,
                                     real& sig12,
                                     real& ssig1, real& csig1,
                                     real& ssig2, real& csig2,
                                     EllipticFunction& E,
                                     real& omg12,
                                     bool diffp, real& dlam12) const
    {

    if (sbet1 == 0 && calp1 == 0)
      // Break degeneracy of equatorial line.  This case has already been
      // handled.
      calp1 = -tiny_;

    real
      // sin(alp1) * cos(bet1) = sin(alp0)
      salp0 = salp1 * cbet1,
      calp0 = Math::hypot(calp1, salp1 * sbet1); // calp0 > 0

    real somg1, comg1, somg2, comg2, cchi1, cchi2, lam12;
    // tan(bet1) = tan(sig1) * cos(alp1)
    // tan(omg1) = sin(alp0) * tan(sig1) = tan(omg1)=tan(alp1)*sin(bet1)
    ssig1 = sbet1; somg1 = salp0 * sbet1;
    csig1 = comg1 = calp1 * cbet1;
    // Without normalization we have schi1 = somg1.
    cchi1 = _f1 * dn1 * comg1;
    SinCosNorm(ssig1, csig1);
    // SinCosNorm(somg1, comg1); -- don't need to normalize!
    // SinCosNorm(schi1, cchi1); -- don't need to normalize!

    // Enforce symmetries in the case abs(bet2) = -bet1.  Need to be careful
    // about this case, since this can yield singularities in the Newton
    // iteration.
    // sin(alp2) * cos(bet2) = sin(alp0)
    salp2 = cbet2 != cbet1 ? salp0 / cbet2 : salp1;
    // calp2 = sqrt(1 - sq(salp2))
    //       = sqrt(sq(calp0) - sq(sbet2)) / cbet2
    // and subst for calp0 and rearrange to give (choose positive sqrt
    // to give alp2 in [0, pi/2]).
    calp2 = cbet2 != cbet1 || abs(sbet2) != -sbet1 ?
      sqrt(Math::sq(calp1 * cbet1) +
           (cbet1 < -sbet1 ?
            (cbet2 - cbet1) * (cbet1 + cbet2) :
            (sbet1 - sbet2) * (sbet1 + sbet2))) / cbet2 :
      abs(calp1);
    // tan(bet2) = tan(sig2) * cos(alp2)
    // tan(omg2) = sin(alp0) * tan(sig2).
    ssig2 = sbet2; somg2 = salp0 * sbet2;
    csig2 = comg2 = calp2 * cbet2;
    // Without normalization we have schi2 = somg2.
    cchi2 = _f1 * dn2 * comg2;
    SinCosNorm(ssig2, csig2);
    // SinCosNorm(somg2, comg2); -- don't need to normalize!
    // SinCosNorm(schi2, cchi2); -- don't need to normalize!

    // sig12 = sig2 - sig1, limit to [0, pi]
    sig12 = atan2(max(csig1 * ssig2 - ssig1 * csig2, real(0)),
                  csig1 * csig2 + ssig1 * ssig2);

    // omg12 = omg2 - omg1, limit to [0, pi]
    omg12 = atan2(max(comg1 * somg2 - somg1 * comg2, real(0)),
                  comg1 * comg2 + somg1 * somg2);
    real k2 = Math::sq(calp0) * _ep2;
    E.Reset(-k2, -_ep2, 1 + k2, 1 + _ep2);
    real chi12 = atan2(max(cchi1 * somg2 - somg1 * cchi2, real(0)),
                       cchi1 * cchi2 + somg1 * somg2);
    lam12 = chi12 -
      _e2/_f1 * salp0 * E.H() / (Math::pi() / 2) *
      (sig12 + E.deltaH(ssig2, csig2, dn2) - E.deltaH(ssig1, csig1, dn1) );

    if (diffp) {
      if (calp2 == 0)
        dlam12 = - 2 * _f1 * dn1 / sbet1;
      else {
        real dummy;
        Lengths(E, sig12, ssig1, csig1, dn1, ssig2, csig2, dn2,
                cbet1, cbet2, dummy, dlam12, dummy,
                false, dummy, dummy);
        dlam12 *= _f1 / (calp2 * cbet2);
      }
    }

    return lam12;
  }

  void GeodesicExact::C4f(real eps, real c[]) const {
    // Evaluate C4 coeffs by Horner's method
    // Elements c[0] thru c[nC4_ - 1] are set
    for (int j = nC4x_, k = nC4_; k; ) {
      real t = 0;
      for (int i = nC4_ - k + 1; i; --i)
        t = eps * t + _C4x[--j];
      c[--k] = t;
    }

    real mult = 1;
    for (int k = 1; k < nC4_; ) {
      mult *= eps;
      c[k++] *= mult;
    }
  }

  // Geodesic.cpp contains explicit expressions for _C4x[l].  However this
  // results in extraordinarily long compiler times with real = quad (7 mins)
  // or mpreal (15 mins).  So instead we evaluate _C4x[l] by using the Horner
  // recursion with the coefficient stored in an array by rawC4coeff.

  void GeodesicExact::C4coeff() {
    const real* cc = rawC4coeff();
    // Coefficients for C[4,m]
    for (int m = 0, k = 0, h = 0; m < nC4_; ++m) {
      // eps^j coefficient
      for (int j = m; j < nC4_; ++j) {
        real t = 0;
        // n^l coefficient
        for (int l = nC4_ - j; l--;)
          t = _n * t + cc[h++];
        _C4x[k++] = t/cc[h++];
      }
    }
    return;
  }

} // namespace GeographicLib
