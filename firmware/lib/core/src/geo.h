#pragma once
double geo_distance_m(double lat1, double lon1, double lat2, double lon2);  // haversine, R=6371008.8
double geo_bearing_deg(double lat1, double lon1, double lat2, double lon2); // initial bearing 0..360
const char* geo_cardinal(double deg);  // "N","NE","E","SE","S","SW","W","NW"
