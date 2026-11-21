# Curved event fades are shaped in decibels and semitones

The curved fade functions (`Synth.addVolumeFadeWithCurve`, `Synth.addPitchFadeWithCurve`) shape the fade in decibels for volume and semitones for pitch, so a neutral curve (0.5) moves evenly in those units. The original `addVolumeFade` / `addPitchFade` interpolate linearly in raw gain factor and frequency ratio, and are left unchanged. We first tried making curve 0.5 match the old functions, but in raw gain units every volume fade to silence holds loud and collapses in the last few percent regardless of the curve value, which made the curve useless for expression and legato shaping. Anyone who needs the old shape keeps using the old functions.

## Considered Options

- Shape in raw gain / frequency ratio so curve 0.5 matches the old functions: rejected, fade-outs to silence cannot be made smooth.
- Shape in raw units with a much steeper curve range: rejected, extremes become abrupt and it still never sounds even.

## Consequences

- The curve uses an exponential family, `y = (1 - e^(-a*t)) / (1 - e^(-a))` with `a = (2c - 1) * 4`, chosen over a power curve because power curves start vertically and look uneven.
- -100 dB is treated as silence in both directions, and a -100 dB target kills the note, matching `addVolumeFade`.
- The curve parameter is a `var` so a fade curve table (issue #26) can later extend the same functions.
