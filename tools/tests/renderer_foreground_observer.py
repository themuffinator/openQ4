"""Read-only Windows foreground evidence; no input injection or screen capture."""
from __future__ import annotations

import ctypes
import os
import time


class Readiness:
    """Require an uninterrupted observed interval before releasing a test gate."""
    def __init__(self, seconds):
        self.seconds = seconds
        self.since = None

    def update(self, now, focused, competing, interrupted=False):
        if focused is not True or competing or interrupted:
            self.since = None
            return False
        if self.since is None:
            self.since = now
        return now - self.since >= self.seconds


def interval_events(events, begin, end):
    if begin is None or end is None:
        return []
    return [event for event in events if begin <= event['time'] <= end]


class ForegroundObserver:
    """Observe brief focus changes that could occur between ordinary polls.

    EVENT_SYSTEM_FOREGROUND is delivered out of process on this thread's
    message loop. Keep the callback alive until UnhookWinEvent. Only ownership
    booleans and event times are retained, never other window titles or input.
    https://learn.microsoft.com/windows/win32/api/winuser/nf-winuser-setwineventhook
    """
    def __init__(self, pid):
        if os.name != 'nt':
            raise OSError('foreground event observation requires Windows')
        from ctypes import wintypes
        self.pid = pid
        self.window = None
        self.events = []
        self.errors = []
        self.overflow = False
        self.user = ctypes.WinDLL('user32', use_last_error=True)
        self.kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        self.kernel.GetTickCount.restype = wintypes.DWORD
        self.user.GetForegroundWindow.restype = wintypes.HWND
        self.user.GetWindowThreadProcessId.argtypes = (wintypes.HWND, ctypes.POINTER(wintypes.DWORD))
        self.user.GetWindowThreadProcessId.restype = wintypes.DWORD
        callback_type = ctypes.WINFUNCTYPE(None, wintypes.HANDLE, wintypes.DWORD, wintypes.HWND,
                                          wintypes.LONG, wintypes.LONG, wintypes.DWORD, wintypes.DWORD)

        def changed(hook, event, window, object_id, child_id, thread_id, event_ms):
            try:
                # The event timestamp is a wrapping system millisecond count,
                # not the later time at which this message loop delivers it.
                age = (self.kernel.GetTickCount() - event_ms) & 0xffffffff
                observation = dict(time=time.monotonic() - age / 1000, focused=self.owns(window))
                if len(self.events) < 4096:
                    self.events.append(observation)
                else:
                    self.overflow = True
            except Exception as error:
                self.errors.append(str(error))

        self.callback = callback_type(changed)
        self.user.SetWinEventHook.argtypes = (wintypes.DWORD, wintypes.DWORD, wintypes.HMODULE,
                                              callback_type, wintypes.DWORD, wintypes.DWORD, wintypes.DWORD)
        self.user.SetWinEventHook.restype = wintypes.HANDLE
        self.user.UnhookWinEvent.argtypes = (wintypes.HANDLE,)
        self.user.UnhookWinEvent.restype = wintypes.BOOL
        self.user.PeekMessageW.argtypes = (ctypes.POINTER(wintypes.MSG), wintypes.HWND,
                                         wintypes.UINT, wintypes.UINT, wintypes.UINT)
        self.user.PeekMessageW.restype = wintypes.BOOL
        self.user.TranslateMessage.argtypes = (ctypes.POINTER(wintypes.MSG),)
        self.user.DispatchMessageW.argtypes = (ctypes.POINTER(wintypes.MSG),)
        self.user.DispatchMessageW.restype = ctypes.c_ssize_t
        self.message = wintypes.MSG()
        # Do not skip this process: focus returning to the caller must be seen.
        # OUTOFCONTEXT (0) does not map a callback DLL into another process.
        self.hook = self.user.SetWinEventHook(3, 3, None, self.callback, 0, 0, 0)
        if not self.hook:
            raise ctypes.WinError(ctypes.get_last_error())

    def owner(self, window):
        from ctypes import wintypes
        owner = wintypes.DWORD()
        if not window or not self.user.GetWindowThreadProcessId(window, ctypes.byref(owner)):
            return None
        return owner.value

    def bind_window(self, window):
        if not window or self.owner(window) != self.pid:
            raise ValueError('reported render window does not belong to the launched process')
        if self.window is not None and self.window != window:
            raise ValueError('render window changed during foreground qualification')
        self.window = window

    def owns(self, window):
        if self.window is None:
            return None
        owner = self.owner(window)
        if owner is None:
            return None
        return window == self.window and owner == self.pid

    def foreground(self):
        return self.owns(self.user.GetForegroundWindow())

    def pump(self):
        while self.user.PeekMessageW(ctypes.byref(self.message), None, 0, 0, 1):
            self.user.TranslateMessage(ctypes.byref(self.message))
            self.user.DispatchMessageW(ctypes.byref(self.message))

    def close(self):
        if self.hook:
            self.pump()
            if not self.user.UnhookWinEvent(self.hook):
                self.errors.append('foreground event hook could not be removed')
            self.hook = None
