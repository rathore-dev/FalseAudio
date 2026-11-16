#pragma once

#include <algorithm>
#include <cmath>

namespace fa
{
/** Headline grade label that cannot out-shine a poor key sub-score.
    The grade is based on min (score, weakestSubScore + 20): a sub-score under 30 caps the label at
    "Fair", under 45 at "Good", under 60 at "Excellent"; "Reference-grade" needs every key sub-score >= 70.
    Sub-scores <= 0 are treated as "not measured" and ignored (cleanliness is 0 without driver data). */
inline int gradeScoreFor (int score, double tonal, double imaging, double clean) noexcept
{
    double worst = 1.0e9;
    for (double s : { tonal, imaging, clean })
        if (s > 0.0)
            worst = std::min (worst, s);
    if (worst > 1.0e8)
        return score;
    return std::min (score, (int) std::lround (worst + 20.0));
}

inline const char* verdictLabel (int score) noexcept
{
    if (score >= 90) return "Reference-grade";
    if (score >= 80) return "Excellent";
    if (score >= 65) return "Good";
    if (score >= 45) return "Fair - check findings";
    return "Needs attention";
}

inline const char* verdictLabel (int score, double tonal, double imaging, double clean) noexcept
{
    return verdictLabel (gradeScoreFor (score, tonal, imaging, clean));
}
} // namespace fa
