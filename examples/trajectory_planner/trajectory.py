import numpy as np


def trajectory(x0: float, v0: float, x1: float, v1: float, v_max: float, a_max: float):
    assert v_max > 0, 'Positive velocity limit expected.'
    assert a_max > 0, 'Positive acceleration limit expected.'
    assert abs(v0) <= v_max, 'Start velocity exceeds velocity limit.'
    assert abs(v1) <= v_max, 'Target velocity exceeds velocity limit.'

    # find maximum possible velocity
    for a in [a_max, -a_max]:
        r = (v0**2 + v1**2) / 2 + a * (x1 - x0)
        if r >= 0:
            break
    t_acc = max((-v0 - np.sqrt(r)) / a, (-v0 + np.sqrt(r)) / a)
    t_dec = (v0 - v1) / a + t_acc
    v_mid = v0 + t_acc * a
    if abs(v_mid) <= v_max:
        # no linear part necessary
        def f(t): return x0 + v0 * t + 1/2 * a * t**2 if t < t_acc else \
            x1 - v1 * (t_acc + t_dec - t) - 1/2 * a * (t_acc + t_dec - t)**2
        return f, t_acc + t_dec
    else:
        # introduce linear motion
        t_acc = abs(v_max - v0 if v_mid > 0 else -v_max - v0) / a_max
        t_dec = abs(v_max - v1 if v_mid > 0 else -v_max - v1) / a_max
        xa = x0 + v0 * t_acc + 1/2 * a * t_acc**2
        va = v0 + t_acc * a
        xb = x1 - v1 * t_dec - 1/2 * a * t_dec**2
        t_lin = abs(xb - xa) / abs(v_max)
        duration = t_acc + t_lin + t_dec

        def f(t): return x0 + v0 * t + 1/2 * a * t**2 if t < t_acc else \
            xa + va * (t - t_acc) if t < t_acc + t_lin else \
            x1 - v1 * (duration - t) - 1/2 * a * (duration - t)**2
        return f, duration
