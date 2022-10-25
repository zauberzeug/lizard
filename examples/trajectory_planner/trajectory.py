import numpy as np


def trajectory(x0: float, v0: float, x1: float, v1: float, v_max: float, a_max: float):
    assert v_max > 0, 'Positive velocity limit expected.'
    assert a_max > 0, 'Positive acceleration limit expected.'
    assert abs(v0) <= v_max, 'Start velocity exceeds velocity limit.'
    assert abs(v1) <= v_max, 'Target velocity exceeds velocity limit.'

    # find perfect S-curve
    for a in [a_max, -a_max]:
        r = (v0**2 + v1**2) / 2 + a * (x1 - x0)
        if r >= 0:
            break
    t0 = max((-v0 - np.sqrt(r)) / a, (-v0 + np.sqrt(r)) / a)
    t1 = (v0 - v1) / a + t0
    v_mid = v0 + t0 * a
    if abs(v_mid) <= v_max:
        def f(t): return x0 + v0 * t + 1/2 * a * t**2 if t < t0 else \
            x1 - v1 * (t0 + t1 - t) - 1/2 * a * (t0 + t1 - t)**2
        return f, t0 + t1

    # introduce linear motion
    t0 = abs(v_max - v0 if v_mid > 0 else -v_max - v0) / a_max
    t1 = abs(v_max - v1 if v_mid > 0 else -v_max - v1) / a_max
    xa = x0 + v0 * t0 + 1/2 * a * t0**2
    va = v0 + t0 * a
    xb = x1 - v1 * t1 - 1/2 * a * t1**2
    t_lin = abs(xb - xa) / abs(v_max)
    duration = t0 + t_lin + t1

    def f(t): return x0 + v0 * t + 1/2 * a * t**2 if t < t0 else \
        xa + va * (t - t0) if t < t0 + t_lin else \
        x1 - v1 * (duration - t) - 1/2 * a * (duration - t)**2
    return f, duration
