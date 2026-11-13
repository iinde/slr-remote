#include "geo.h"

#include <math.h>

namespace {
constexpr double R_EARTH_M = 6371008.8;
constexpr double K_PI = 3.14159265358979323846;
double rad(double d) { return d * K_PI / 180.0; }
}  // namespace

double geo_distance_m(double lat1, double lon1, double lat2, double lon2) {
  const double dlat = rad(lat2 - lat1), dlon = rad(lon2 - lon1);
  const double s1 = sin(dlat / 2), s2 = sin(dlon / 2);
  double a = s1 * s1 + cos(rad(lat1)) * cos(rad(lat2)) * s2 * s2;
  if (a > 1.0) a = 1.0;
  return 2.0 * R_EARTH_M * atan2(sqrt(a), sqrt(1.0 - a));
}

double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2) {
  const double p1 = rad(lat1), p2 = rad(lat2), dl = rad(lon2 - lon1);
  const double y = sin(dl) * cos(p2);
  const double x = cos(p1) * sin(p2) - sin(p1) * cos(p2) * cos(dl);
  double deg = atan2(y, x) * 180.0 / K_PI;
  deg = fmod(deg + 360.0, 360.0);
  return deg >= 360.0 ? 0.0 : deg;
}

const char* geo_cardinal(double deg) {
  static const char* const names[8] = {"N", "NE", "E", "SE", "S", "SW", "W", "NW"};
  if (!isfinite(deg)) return "N";
  double d = fmod(deg, 360.0);
  if (d < 0) d += 360.0;
  const int idx = (int)floor((d + 22.5) / 45.0) % 8;
  return names[idx];
}
