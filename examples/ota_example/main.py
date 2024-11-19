#!/usr/bin/env python3
import os
from glob import glob

from nicegui import app, ui
from starlette.requests import Request
from starlette.responses import FileResponse, Response

ui.markdown('# Lizard OTA Demo')


def get_binary():
    binaries = glob('../../build/*.bin')
    log.push(f'found {len(binaries)} binaries: {binaries}')
    binary = next((b for b in binaries if 'lizard' in b), None)
    log.push(f'offering binary: {binary}')
    return binary


@app.get('/ota/binary/verify')
def ota_binary_verify(request: Request) -> Response:
    assert request.client is not None
    log.push('verification request received from ' + request.client.host)
    verify_bin = get_binary()
    try:
        verify_bin_str = os.path.basename(verify_bin).replace('.bin', '').replace('lizard', '').replace('-', '')
        if verify_bin_str is None:
            log.push('No "Lizard" binary found. But connection established')
            return Response('No Lizard')
        if verify_bin_str == '':
            log.push('Binary without version found')
            return Response('Unknown version')
        response = Response(verify_bin_str)
        log.push(f'Send response: {verify_bin_str}')
        return response
    except Exception:  # pylint: disable=broad-except
        log.push('Error: binary for verification not found')
        return Response(status_code=503, content='Lizard binary not found')


@app.get('/ota/binary')
def ota_binary(request: Request) -> Response:
    assert request.client is not None
    log.push('binary request received from ' + request.client.host)
    try:
        binary = get_binary()
        if binary is None:
            log.push('binary not found')
            return Response(status_code=503, content='Lizard binary not found')
        log.push(f'send binary to {request.client.host}')
        return FileResponse(os.path.abspath(binary))
    except Exception as e:  # pylint: disable=broad-except
        log.push(f'Error: {str(e)}')
        return Response(status_code=503, content='Something went wrong')


log = ui.log().classes('w-full h-96')

log.push('ready to serve OTA updates')
log.push('--------------------------')

ui.run(port=1111)
