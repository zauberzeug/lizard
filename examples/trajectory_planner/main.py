#!/usr/bin/env python3
import asyncio

import numpy as np
import pylab as pl
from nicegui import ui

from serial_connection import SerialConnection
from trajectory import trajectory

STARTUP = '''
can = Can(32, 33, 1000000)

rmd1 = RmdMotor(can, 1)
rmd2 = RmdMotor(can, 2)
rmd1.ratio = 9
rmd2.ratio = 9
rmd = RmdPair(rmd1, rmd2)

core.output('core.millis rmd1.position rmd2.position')
'''

SCALE = 100

WHITE = '#FFFFFF'
BLUE = '#6E93D6'
GRAY = '#3A3E42'
SILVER = '#F8F8F8'
GREEN = '#53B689'
BLACK = '#111B1E'

ui.colors(primary=BLUE, secondary=GREEN)

serial = SerialConnection()

with ui.header().style(f'background-color: {BLUE}').classes('items-center'):
    ui.label('RMD Motor Synchronization').classes('text-lg font-medium')
    input = ui.input('Lizard Command', on_change=lambda e: serial.send(e.value) or e.sender.set_value('')) \
        .props('dark').classes('ml-auto')
    ui.button('Configure', on_click=lambda: serial.configure(STARTUP)).props(f'flat color={WHITE}')


def read() -> None:
    while True:
        line = serial.read()
        if not line:
            return
        words = line.split()
        if len(words) != 4:
            return
        try:
            true_sphere.move(int(words[2]) / SCALE, int(words[3]) / SCALE)
        except ValueError:
            pass


ui.timer(0.01, read)
ui.on_startup(lambda: serial.configure(STARTUP))


async def run():
    try:
        dt = 0.01
        if curved.value:
            fx, x_duration = trajectory(x0.value, v0.value, x1.value, v1.value, v_max.value, a_max.value)
            fy, y_duration = trajectory(y0.value, w0.value, y1.value, w1.value, v_max.value, a_max.value)
            throttle_x = min(x_duration / y_duration, 1.0)
            throttle_y = min(y_duration / x_duration, 1.0)
            t = np.arange(0, max(x_duration, y_duration), dt)
            x = [fx(ti * throttle_x) for ti in t]
            y = [fy(ti * throttle_y) for ti in t]
        else:
            l = np.sqrt((x1.value - x0.value)**2 + (y1.value - y0.value)**2)
            fs, duration = trajectory(0, v0.value, l, v1.value, v_max.value, a_max.value)
            t = np.arange(0, duration, dt)
            x = [x0.value + fs(ti) * (x1.value - x0.value) / l for ti in t]
            y = [y0.value + fs(ti) * (y1.value - y0.value) / l for ti in t]

        with position_plot:
            pl.clf()
            pl.ylabel('position')
            pl.plot(t, x, 'C0')
            pl.plot(t, y, 'C1')
            pl.legend(['x', 'y'])
            position_plot.update()
        with velocity_plot:
            pl.clf()
            pl.ylabel('velocity')
            pl.axhline(v_max.value, c='orange', ls='--', lw=1, label='_nolegend_')
            pl.axhline(-v_max.value, c='orange', ls='--', lw=1, label='_nolegend_')
            pl.plot(t[:-1], np.diff(x) / dt, 'C0')
            pl.plot(t[:-1], np.diff(y) / dt, 'C1')
            pl.legend(['x', 'y'])
            velocity_plot.update()
        with acceleration_plot:
            pl.clf()
            pl.ylabel('acceleration')
            pl.axhline(a_max.value, c='r', ls='--', lw=1, label='_nolegend_')
            pl.axhline(-a_max.value, c='r', ls='--', lw=1, label='_nolegend_')
            pl.plot(t[:-2], np.diff(x, 2) / dt**2, 'C0')
            pl.plot(t[:-2], np.diff(y, 2) / dt**2, 'C1')
            pl.legend(['x', 'y'])
            acceleration_plot.update()
        with location_plot:
            pl.clf()
            pl.axis('equal')
            pl.title('trajectory')
            pl.plot(x, y, 'k')
            pl.plot(x[::10], y[::10], 'k.')
            pl.plot(x0.value, y0.value, 'ko')
            pl.plot(x1.value, y1.value, 'ko')
            location_plot.update()

        for xi, yi in zip(x[::5], y[::5]):
            target_sphere.move(x=xi / SCALE, y=yi / SCALE)
            await asyncio.sleep(5 * dt)

    except Exception as e:
        ui.notify(str(e) or repr(e), close_button='OK')

with ui.row().classes('w-full items-stretch'):
    with ui.column().classes('flex-grow'):
        ui.markdown('Start position: `x0` and `y0`')
        x0 = ui.slider(min=-1000, max=1000, value=-300, on_change=run).props('label-always color=blue')
        y0 = ui.slider(min=-1000, max=1000, value=-100, on_change=run).props('label-always color=blue')
        ui.markdown('Start velocity: `v0` (and `w0`)')
        v0 = ui.slider(min=-1000, max=1000, value=0, on_change=run).props('label-always color=green')
        w0 = ui.slider(min=-1000, max=1000, value=0, on_change=run).props('label-always color=green')
    with ui.column().classes('flex-grow'):
        ui.markdown('Target position: `x1` and `y1`')
        x1 = ui.slider(min=-1000, max=1000, value=300, on_change=run).props('label-always color=blue')
        y1 = ui.slider(min=-1000, max=1000, value=100, on_change=run).props('label-always color=blue')
        ui.markdown('Target velocity: `v1` (and `w1`)')
        v1 = ui.slider(min=-1000, max=1000, value=0, on_change=run).props('label-always color=green')
        w1 = ui.slider(min=-1000, max=1000, value=0, on_change=run).props('label-always color=green')
    with ui.column().classes('flex-grow'):
        ui.markdown('Velocity and acceleration limits: `v_max` and `a_max`')
        v_max = ui.slider(min=0, max=1000, value=1000, on_change=run).props('label-always color=orange')
        a_max = ui.slider(min=0, max=1000, value=1000, on_change=run).props('label-always color=red')
        curved = ui.checkbox('allow curved trajectory', on_change=run)
        w0.bind_visibility_from(curved, 'value')
        w1.bind_visibility_from(curved, 'value')
        ui.button('replay', on_click=run).props('outline icon=replay')
    with ui.column():
        location_plot = ui.plot(figsize=(4, 3))

with ui.row():
    with ui.scene(width=800, height=600) as scene:
        target_sphere = scene.sphere(0.5).material(BLUE)
        true_sphere = scene.sphere(0.5).material(GREEN)
    with ui.column():
        position_plot = ui.plot(figsize=(6, 2))
        velocity_plot = ui.plot(figsize=(6, 2))
        acceleration_plot = ui.plot(figsize=(6, 2))

ui.run()
