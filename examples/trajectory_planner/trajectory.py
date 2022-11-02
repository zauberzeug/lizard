from dataclasses import dataclass

import numpy as np


@dataclass(kw_only=True, slots=True)
class TrajectoryPart:
    t0: float
    x0: float
    v0: float
    a: float
    dt: float

    @property
    def t_end(self) -> float:
        return self.t0 + self.dt


@dataclass(kw_only=True, slots=True)
class Trajectory:
    parts: list[TrajectoryPart]

    @property
    def duration(self) -> float:
        return sum(p.dt for p in self.parts)

    def position(self, t: float) -> float:
        assert self.parts
        if t < self.parts[0].t0:
            return self.parts[0].x0
        for part in self.parts:
            if part.t0 <= t <= part.t_end:
                dt = t - part.t0
                return part.x0 + part.v0 * dt + 1/2 * part.a * dt**2
        else:
            part = self.parts[-1]
            dt = part.t_end - part.t0
            return part.x0 + part.v0 * dt + 1/2 * part.a * dt**2

    def throttle(self, factor: float) -> None:
        for part in self.parts:
            part.t0 *= factor
            part.v0 /= factor
            part.a /= factor**2
            part.dt *= factor


def trajectory(x0: float, x1: float, v0: float, v1: float, v_max: float, a_max: float) -> Trajectory:
    v0 = min(max(v0, -v_max), v_max)
    v1 = min(max(v1, -v_max), v_max)

    # find maximum possible velocity
    for a in [a_max, -a_max]:
        r = (v0**2 + v1**2) / 2 + a * (x1 - x0)
        if r >= 0:
            break
    dt_acc = max((-v0 - np.sqrt(r)) / a, (-v0 + np.sqrt(r)) / a)
    dt_dec = (v0 - v1) / a + dt_acc
    v_mid = v0 + dt_acc * a
    if abs(v_mid) <= v_max:
        # no linear part necessary
        x_mid = x0 + v0 * dt_acc + 1/2 * a * dt_acc**2
        return Trajectory(parts=[
            TrajectoryPart(t0=0,      x0=x0,    v0=v0,    a=a,  dt=dt_acc),
            TrajectoryPart(t0=dt_acc, x0=x_mid, v0=v_mid, a=-a, dt=dt_dec),
        ])
    else:
        # introduce linear motion
        dt_acc = abs(v_max - v0 if v_mid > 0 else -v_max - v0) / a_max
        dt_dec = abs(v_max - v1 if v_mid > 0 else -v_max - v1) / a_max
        xa = x0 + v0 * dt_acc + 1/2 * a * dt_acc**2
        xb = x1 - v1 * dt_dec - 1/2 * a * dt_dec**2
        v_lin = v0 + dt_acc * a
        dt_lin = abs(xb - xa) / abs(v_max)
        return Trajectory(parts=[
            TrajectoryPart(t0=0,             x0=x0, v0=v0,    a=a,  dt=dt_acc),
            TrajectoryPart(t0=dt_acc,        x0=xa, v0=v_lin, a=0,  dt=dt_lin),
            TrajectoryPart(t0=dt_acc+dt_lin, x0=xb, v0=v_lin, a=-a, dt=dt_dec),
        ])


def trajectories(x0: float, y0: float, x1: float, y1: float, v0: float, w0: float, v1: float, w1: float,
                 v_max: float, a_max: float, curved: bool) -> tuple[Trajectory, Trajectory]:
    if curved:
        tx = trajectory(x0, x1, v0, v1, v_max, a_max)
        ty = trajectory(y0, y1, w0, w1, v_max, a_max)
    else:
        yaw = np.arctan2(y1 - y0, x1 - x0)
        tx = trajectory(x0, x1, v0 * np.cos(yaw), v1 * np.cos(yaw), v_max * np.cos(yaw), a_max * np.cos(yaw))
        ty = trajectory(y0, y1, v0 * np.sin(yaw), v1 * np.sin(yaw), v_max * np.sin(yaw), a_max * np.sin(yaw))
    duration = max(tx.duration, ty.duration)
    tx.throttle(duration / tx.duration)
    ty.throttle(duration / ty.duration)
    return tx, ty
