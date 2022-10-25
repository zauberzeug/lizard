#!/usr/bin/env python3
import asyncio

import numpy as np
import pylab as pl
from nicegui import ui

from trajectory import trajectory

with ui.row().classes('w-full items-stretch'):
    with ui.column().classes('flex-grow'):
        ui.markdown('Start position: `x0` and `y0`')
        x0 = ui.slider(min=-10, max=10, step=0.1, value=-3, on_change=run).props('label-always color=blue')
        y0 = ui.slider(min=-10, max=10, step=0.1, value=-1, on_change=run).props('label-always color=blue')
        ui.markdown('Start velocity: `v0` (and `w0`)')
        v0 = ui.slider(min=-10, max=10, step=0.1, value=0, on_change=run).props('label-always color=green')
        w0 = ui.slider(min=-10, max=10, step=0.1, value=0, on_change=run).props('label-always color=green')
    with ui.column().classes('flex-grow'):
        ui.markdown('Target position: `x1` and `y1`')
        x1 = ui.slider(min=-10, max=10, step=0.1, value=3, on_change=run).props('label-always color=blue')
        y1 = ui.slider(min=-10, max=10, step=0.1, value=1, on_change=run).props('label-always color=blue')
        ui.markdown('Target velocity: `v1` (and `w1`)')
        v1 = ui.slider(min=-10, max=10, step=0.1, value=0, on_change=run).props('label-always color=green')
        w1 = ui.slider(min=-10, max=10, step=0.1, value=0, on_change=run).props('label-always color=green')
    with ui.column().classes('flex-grow'):
        ui.markdown('Velocity and acceleration limits: `v_max` and `a_max`')
        v_max = ui.slider(min=0, max=10, step=0.1, value=10, on_change=run).props('label-always color=orange')
        a_max = ui.slider(min=0, max=10, step=0.1, value=10, on_change=run).props('label-always color=red')
        curved = ui.checkbox('allow curved trajectory', on_change=run)
        w0.bind_visibility_from(curved, 'value')
        w1.bind_visibility_from(curved, 'value')
        ui.button('replay', on_click=run).props('outline icon=replay')
    with ui.column():
        location_plot = ui.plot(figsize=(4, 3))

with ui.row():
    with ui.column():
        position_plot = ui.plot(figsize=(6, 2))
        velocity_plot = ui.plot(figsize=(6, 2))
        acceleration_plot = ui.plot(figsize=(6, 2))
    with ui.scene(width=800, height=600) as scene:
        sphere = scene.sphere(0.5).material('#1f77b4')

ui.on_startup(run)

ui.run(port=8081)
