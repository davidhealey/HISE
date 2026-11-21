# HISE

Domain language for the HISE audio framework: events, voices, and the scripting layer that drives them.

## Event fades

**Event fade**:
A timed change of volume or pitch applied to every voice started by a given event ID.
_Avoid_: Ramp, glide, slide

**Volume fade**:
An event fade that moves the voice gain towards a target level in decibels.
_Avoid_: Gain ramp, fader

**Pitch fade**:
An event fade that moves the voice pitch towards a target detune in semitones and cents.
_Avoid_: Pitch bend, glide, portamento

**Fade curve**:
A single value from 0 to 1 that shapes how an event fade progresses over its duration; 0 starts slow, 1 starts fast.
_Avoid_: Curvature, shape, skew

**Neutral curve**:
A fade curve of 0.5, where the fade moves evenly in decibels for volume and semitones for pitch.
_Avoid_: Linear curve, default curve

**Fade curve table**:
A list of points that defines an arbitrary fade curve, as an alternative to a single fade curve value.
_Avoid_: Fade envelope, curve points
