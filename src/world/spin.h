#ifndef WORLD_SPIN_H_INCLUDED
#define WORLD_SPIN_H_INCLUDED

#include <cmath>
#include <inttypes.h>

#include "../matrix.h"

namespace World
{
namespace Spin
{

// Script-facing spin is deliberately a small, unitless 0..10 strength.
// Each point is one full revolution per second; 10 is a drill-fast 10 rps.
constexpr double MIN_STRENGTH = 0.0;
constexpr double MAX_STRENGTH = 10.0;
constexpr double DEGREES_PER_SECOND_PER_STRENGTH = 360.0;

inline double ClampStrength(double value)
{
    if ( !std::isfinite(value) || value <= MIN_STRENGTH )
        return MIN_STRENGTH;

    return value >= MAX_STRENGTH ? MAX_STRENGTH : value;
}

inline bool HasStrength(const vec3d &strength)
{
    return strength.x != 0.0 || strength.y != 0.0 || strength.z != 0.0;
}

inline mat3x3 BuildMatrix(const vec3d &strength, int32_t age)
{
    constexpr double PI_OVER_180 = 0.01745329251994329577;
    const double radiansPerStrengthPerMillisecond =
        DEGREES_PER_SECOND_PER_STRENGTH * 0.001 * PI_OVER_180;
    vec3d angle = strength * ((double)age * radiansPerStrengthPerMillisecond);
    mat3x3 spin = mat3x3::Ident();

    if ( angle.x != 0.0 )
        spin *= mat3x3::RotateX(angle.x);
    if ( angle.y != 0.0 )
        spin *= mat3x3::RotateY(angle.y);
    if ( angle.z != 0.0 )
        spin *= mat3x3::RotateZ(angle.z);

    return spin;
}

}
}

#endif
