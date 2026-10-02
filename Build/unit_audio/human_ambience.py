"""Human Ambience: distant night-shift ventilation, alley wind and overhead power lines.

One 34-second NightLoop region becomes a 32-second loop after the renderer overlaps its final
2 seconds into its start. Recorded beds stay alive through the region; no one-shot tails or
limiter states cross the wrap. Faders keep wind foremost and machinery/power lines far away.
"""
import numpy as np

from .core import Event, Layer, Unit, clip, dark, filt, load, seconds

SOURCES = [
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%204of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 4of9/Hzandbits - Hvac Elements/Hvac,Ventilation,Exhaust,Industrial,Drone,Slight rumble,Loop.wav'),
    ('Sonniss.com%20-%20GDC%202017%20-%20Game%20Audio%20Bundle%20Part%204of9.zip',
     'Sonniss.com - GDC 2017 - Game Audio Bundle Part 4of9/Hzandbits - Urban Winds II/Wind,Ext,Alley,Whoosh,Fluctuating slightly,Wire flapping.wav'),
    ('Sonniss.com%20-%20GDC%202019%20-%20Game%20Audio%20Bundle%20Part%202of8.zip',
     'Sonniss.com - GDC 2019 - Game Audio Bundle Part 2of8/Ivo Vicic - Structure borne sound - metal resonances and textures/08 Overhead power line in wind.wav'),
]

VENTILATION = ('Hzandbits - Hvac Elements',
               'Hvac,Ventilation,Exhaust,Industrial,Drone,Slight rumble,Loop.wav')
WIND = ('Hzandbits - Urban Winds II',
        'Wind,Ext,Alley,Whoosh,Fluctuating slightly,Wire flapping.wav')
POWER_LINES = ('Ivo Vicic - Structure borne sound - metal resonances and textures',
               '08 Overhead power line in wind.wav')
REGION_LENGTH = 34.0
WRAP_CROSSFADE = 2.0


def bed(source: tuple[str, str], start: float, highpass: float, lowpass: float) -> np.ndarray:
    """Filter continuous material with two seconds of pre-roll before taking the region window."""
    audio = load(*source)[seconds(start - 2.0):seconds(start + REGION_LENGTH)]
    audio = dark(filt(audio, 'highpass', highpass, order=2), lowpass)[seconds(2.0):]
    return clip(audio, fade_in=0.0, fade_out=0.0)


def night_loop(_variant: int) -> list[Layer]:
    return [
        Layer('Distant Ventilation', bed(VENTILATION, 10.0, 65.0, 950.0)),
        Layer('Alley Wind', bed(WIND, 8.0, 180.0, 2300.0)),
        Layer('Far Power Lines', bed(POWER_LINES, 4.0, 110.0, 1600.0)),
    ]


UNIT = Unit(
    'Human', 'Ambience',
    tracks=[('Distant Ventilation', -15.0), ('Alley Wind', -8.0), ('Far Power Lines', -23.0)],
    events=[Event('NightLoop', 1, REGION_LENGTH, -27.0, night_loop,
                  loop=True, crossfade=WRAP_CROSSFADE)],
)
