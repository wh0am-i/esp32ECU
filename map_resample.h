#pragma once
#include <math.h>

// Usado na migração NVS 12x8 -> 16x16. Sem dependência de Arduino.
template<int N> static int mapInterval(const float (&axis)[N], float value) {
  int i = 0;
  while (i < N - 2 && value > axis[i + 1]) ++i;
  return i;
}

template<int R, int T> struct StoredMap {
  float rpmAxis[R];
  float tpsAxis[T];
  float advance[T][R];
};

template<int R, int T> static bool validStoredMap(const StoredMap<R,T>& m) {
  for (int i=0; i<R; ++i)
    if (!isfinite(m.rpmAxis[i]) || (i && m.rpmAxis[i] <= m.rpmAxis[i-1])) return false;
  for (int j=0; j<T; ++j) {
    if (!isfinite(m.tpsAxis[j]) || (j && m.tpsAxis[j] <= m.tpsAxis[j-1])) return false;
    for (int i=0; i<R; ++i) if (!isfinite(m.advance[j][i])) return false;
  }
  return true;
}

template<int R, int T, typename Destination>
static void resampleStoredMap(const StoredMap<R,T>& source, Destination& dest, int cols, int rows) {
  for (int i=0; i<cols; ++i)
    dest.rpmAxis[i] = source.rpmAxis[0] + i*(source.rpmAxis[R-1]-source.rpmAxis[0])/(cols-1);
  for (int j=0; j<rows; ++j) {
    dest.tpsAxis[j] = source.tpsAxis[0] + j*(source.tpsAxis[T-1]-source.tpsAxis[0])/(rows-1);
    for (int i=0; i<cols; ++i) {
      int x=mapInterval(source.rpmAxis,dest.rpmAxis[i]), y=mapInterval(source.tpsAxis,dest.tpsAxis[j]);
      float fx=(dest.rpmAxis[i]-source.rpmAxis[x])/(source.rpmAxis[x+1]-source.rpmAxis[x]);
      float fy=(dest.tpsAxis[j]-source.tpsAxis[y])/(source.tpsAxis[y+1]-source.tpsAxis[y]);
      float a=source.advance[y][x]*(1-fx)+source.advance[y][x+1]*fx;
      float b=source.advance[y+1][x]*(1-fx)+source.advance[y+1][x+1]*fx;
      dest.advance[j][i]=a*(1-fy)+b*fy;
    }
  }
}
