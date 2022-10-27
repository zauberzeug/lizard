#!/usr/bin/env python3
import asyncio
import logging

import numpy as np
import pylab as pl
from nicegui import ui

from serial_connection import SerialConnection
from trajectory import trajectories

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
true_x = 0
true_y = 0

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
            global true_x, true_y
            true_x = int(words[2])
            true_y = int(words[3])
            true_sphere.move(true_x / SCALE, true_y / SCALE)
        except ValueError:
            pass


ui.timer(0.01, read)
ui.on_startup(lambda: serial.configure(STARTUP))


async def run() -> None:
    try:
        dt = 0.01

        tx, ty = trajectories(true_x, true_y, x0.value, y0.value, 0, 0, v0.value, w0.value,
                              v_max.value, a_max.value, True)
        t = np.arange(0, tx.duration, dt)
        x_ = [tx.position(ti) for ti in t]
        y_ = [ty.position(ti) for ti in t]

        tx, ty = trajectories(x0.value, y0.value, x1.value, y1.value, v0.value, w0.value, v1.value, w1.value,
                              v_max.value, a_max.value, curved.value)
        t = np.arange(0, tx.duration, dt)
        x = [tx.position(ti) for ti in t]
        y = [ty.position(ti) for ti in t]

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

        serial.send(f'rmd.v_max = {v_max.value}')
        serial.send(f'rmd.a_max = {a_max.value}')
        serial.send(f'rmd.move({x0.value}, {y0.value}, {v0.value}, {w0.value})')
        serial.send(f'rmd.move({x1.value}, {y1.value}, {v1.value}, {w1.value})')
        for xi, yi in zip((x_ + x)[::5], (y_ + y)[::5]):
            target_sphere.move(x=xi / SCALE, y=yi / SCALE)
            await asyncio.sleep(5 * dt)

    except Exception as e:
        logging.exception(e)
        ui.notify(str(e) or repr(e), close_button='OK')

with ui.row().classes('w-full items-stretch'):
    with ui.column().classes('flex-grow'):
        ui.markdown('Start position: `x0` and `y0`')
        x0 = ui.slider(min=-720, max=720, value=-300, step=10, on_change=run).props('label-always color=blue')
        y0 = ui.slider(min=-720, max=720, value=-100, step=10, on_change=run).props('label-always color=blue')
        ui.markdown('Start velocity: `v0` (and `w0`)')
        v0 = ui.slider(min=-420, max=420, value=0, step=10, on_change=run).props('label-always color=green')
        w0 = ui.slider(min=-420, max=420, value=0, step=10, on_change=run).props('label-always color=green')
    with ui.column().classes('flex-grow'):
        ui.markdown('Target position: `x1` and `y1`')
        x1 = ui.slider(min=-720, max=720, value=300, step=10, on_change=run).props('label-always color=blue')
        y1 = ui.slider(min=-720, max=720, value=100, step=10, on_change=run).props('label-always color=blue')
        ui.markdown('Target velocity: `v1` (and `w1`)')
        v1 = ui.slider(min=-420, max=420, value=0, step=10, on_change=run).props('label-always color=green')
        w1 = ui.slider(min=-420, max=420, value=0, step=10, on_change=run).props('label-always color=green')
    with ui.column().classes('flex-grow'):
        ui.markdown('Velocity and acceleration limits: `v_max` and `a_max`')
        v_max = ui.slider(min=0, max=420,  value=360, on_change=run).props('label-always color=orange')
        a_max = ui.slider(min=0, max=1000, value=360, on_change=run).props('label-always color=red')
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

ui.run(title='RMD Motor Synchronization')
