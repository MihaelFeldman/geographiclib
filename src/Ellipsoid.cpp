/**
 * \file Ellipsoid.cpp
 * \brief Implementation for GeographicLib::Ellipsoid class
 *
 * Copyright (c) Charles Karney (2012-2014) <charles@karney.com> and licensed
 * under the MIT/X11 License.  For more information, see
 * http://geographiclib.sourceforge.net/
 **********************************************************************/

#include <GeographicLib/Ellipsoid.hpp>

namespace GeographicLib {

  using namespace std;

  Ellipsoid::Ellipsoid(real a, real f)
    : stol_(real(0.01) * sqrt(numeric_limits<real>::epsilon()))
    , _a(a)
    , _f(f <= 1 ? f : 1/f)
    , _f1(1 - _f)
    , _f12(Math::sq(_f1))
    , _e2(_f * (2 - _f))
    , _e(sqrt(abs(_e2)))
    , _e12(_e2 / (1 - _e2))
    , _n(_f / (2  - _f))
    , _b(_a * _f1)
    , _tm(_a, _f, real(1))
    , _ell(-_e12)
    , _au(_a, _f, real(0), real(1), real(0), real(1), real(1))
  {}

  const Ellipsoid& Ellipsoid::WGS84() {
    static const Ellipsoid wgs84(Constants::WGS84_a(), Constants::WGS84_f());
    return wgs84;
  }

  Math::real Ellipsoid::QuarterMeridian() const
  { return _b * _ell.E(); }

  Math::real Ellipsoid::Area() const {
    return 4 * Math::pi() *
      ((Math::sq(_a) + Math::sq(_b) *
        (_e2 == 0 ? 1 :
         (_e2 > 0 ? Math::atanh(sqrt(_e2)) : atan(sqrt(-_e2))) /
         sqrt(abs(_e2))))/2);
  }

  Math::real Ellipsoid::ParametricLatitude(real phi) const
  { return atand(_f1 * tand(phi)); }

  Math::real Ellipsoid::InverseParametricLatitude(real beta) const
  { return atand(tand(beta) / _f1); }

  Math::real Ellipsoid::GeocentricLatitude(real phi) const
  { return atand(_f12 * tand(phi)); }

  Math::real Ellipsoid::InverseGeocentricLatitude(real theta) const
  { return atand(tand(theta) / _f12); }

  Math::real Ellipsoid::RectifyingLatitude(real phi) const {
    return abs(phi) == 90 ? phi:
      90 * MeridianDistance(phi) / QuarterMeridian();
  }

  Math::real Ellipsoid::InverseRectifyingLatitude(real mu) const {
    if (abs(mu) == 90)
      return mu;
    return InverseParametricLatitude(_ell.Einv(mu * _ell.E() / 90) /
                                     Math::degree());
  }

  Math::real Ellipsoid::AuthalicLatitude(real phi) const
  { return atand(_au.txif(tand(phi))); }

  Math::real Ellipsoid::InverseAuthalicLatitude(real xi) const
  { return atand(_au.tphif(tand(xi))); }

  Math::real Ellipsoid::ConformalLatitude(real phi) const
  { return atand(_tm.taupf(tand(phi))); }

  Math::real Ellipsoid::InverseConformalLatitude(real chi) const
  { return atand(_tm.tauf(tand(chi))); }

  Math::real Ellipsoid::IsometricLatitude(real phi) const
  { return Math::asinh(_tm.taupf(tand(phi))) / Math::degree(); }

  Math::real Ellipsoid::InverseIsometricLatitude(real psi) const
  { return atand(_tm.tauf(sinh(psi * Math::degree()))); }

  Math::real Ellipsoid::CircleRadius(real phi) const {
    return abs(phi) == 90 ? 0 :
      // a * cos(beta)
      _a / Math::hypot(real(1), _f1 * tand(phi));
  }

  Math::real Ellipsoid::CircleHeight(real phi) const {
    real tbeta = _f1 * tand(phi);
    // b * sin(beta)
    return _b * tbeta / Math::hypot(real(1), _f1 * tand(phi));
  }

  Math::real Ellipsoid::MeridianDistance(real phi) const
  { return _b * _ell.Ed( ParametricLatitude(phi) ); }

  Math::real Ellipsoid::MeridionalCurvatureRadius(real phi) const {
    real v = 1 - _e2 * Math::sq(sin(phi * Math::degree()));
    return _a * (1 - _e2) / (v * sqrt(v));
  }

  Math::real Ellipsoid::TransverseCurvatureRadius(real phi) const {
    real v = 1 - _e2 * Math::sq(sin(phi * Math::degree()));
    return _a / sqrt(v);
  }

  Math::real Ellipsoid::NormalCurvatureRadius(real phi, real azi)
    const {
    real
      alpha = azi * Math::degree(),
      v = 1 - _e2 * Math::sq(sin(phi * Math::degree()));
    return _a / (sqrt(v) *
                 (Math::sq(cos(alpha)) * v / (1 - _e2) + Math::sq(sin(alpha))));
  }

} // namespace GeographicLib
