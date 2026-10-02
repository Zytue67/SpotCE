#!/usr/bin/env python3
"""SpotCE macOS menu-bar application."""

from __future__ import annotations

import subprocess
import threading
import time

import rumps

from spotce_bridge import SpotifyInfo, get_spotify_info, run_bridge


class SpotCEMenuBar(rumps.App):
    def __init__(self) -> None:
        super().__init__("SpotCE", title="SpotCE", quit_button=None)

        self._lock = threading.Lock()
        self._bridge_running = False
        self._usb_connected = False
        self._spotify_running = False
        self._spotify_error = ""
        self._track = ""
        self._artist = ""
        self._last_error = ""
        self._bridge_thread: threading.Thread | None = None
        self._stop_event: threading.Event | None = None
        self._last_idle_spotify_poll = 0.0

        self.bridge_status = rumps.MenuItem("Bridge: Stopped")
        self.spotify_status = rumps.MenuItem("Spotify: Checking…")
        self.calc_status = rumps.MenuItem("Calculator: Not connected")
        self.track_status = rumps.MenuItem("Track: —")
        self.start_stop = rumps.MenuItem("Start Bridge", callback=self.toggle_bridge)
        self.open_spotify_item = rumps.MenuItem("Open Spotify", callback=self.open_spotify)
        self.quit_item = rumps.MenuItem("Quit SpotCE", callback=self.quit_app)

        self.menu = [
            self.bridge_status,
            self.spotify_status,
            self.calc_status,
            self.track_status,
            None,
            self.start_stop,
            self.open_spotify_item,
            None,
            self.quit_item,
        ]

        self.timer = rumps.Timer(self.refresh_ui, 0.5)
        self.timer.start()

    def bridge_event(self, event: str, value) -> None:
        with self._lock:
            if event == "bridge":
                self._bridge_running = bool(value)
            elif event == "usb":
                self._usb_connected = bool(value)
            elif event == "spotify":
                self._spotify_running = bool(value)
            elif event == "track" and isinstance(value, SpotifyInfo):
                self._spotify_running = value.running
                self._track = value.title or ""
                self._artist = value.artist or ""
            elif event == "spotify_error":
                self._spotify_error = str(value or "")
            elif event == "error":
                self._last_error = str(value)

    def toggle_bridge(self, _sender) -> None:
        with self._lock:
            running = self._bridge_running or (
                self._bridge_thread is not None and self._bridge_thread.is_alive()
            )
        if running:
            self.stop_bridge()
        else:
            self.start_bridge()

    def start_bridge(self) -> None:
        with self._lock:
            if self._bridge_thread is not None and self._bridge_thread.is_alive():
                return
            self._last_error = ""
            self._usb_connected = False
            self._bridge_running = True
            self._stop_event = threading.Event()
            stop_event = self._stop_event

        def worker() -> None:
            try:
                run_bridge(
                    no_art=False,
                    poll_interval=0.5,
                    verbose=False,
                    stop_event=stop_event,
                    status_callback=self.bridge_event,
                )
            except Exception as exc:  # keep the menu app alive if the bridge dies
                self.bridge_event("error", str(exc))
            finally:
                with self._lock:
                    self._bridge_running = False
                    self._usb_connected = False

        thread = threading.Thread(target=worker, name="SpotCEBridge", daemon=True)
        with self._lock:
            self._bridge_thread = thread
        thread.start()

    def stop_bridge(self) -> None:
        with self._lock:
            stop_event = self._stop_event
        if stop_event is not None:
            stop_event.set()

    def open_spotify(self, _sender) -> None:
        subprocess.Popen(["open", "-a", "Spotify"])

    def refresh_ui(self, _timer) -> None:
        # When the bridge is stopped, still show whether Spotify is open.
        now = time.monotonic()
        with self._lock:
            bridge_running = self._bridge_running
        if now - self._last_idle_spotify_poll >= 2.0:
            self._last_idle_spotify_poll = now
            try:
                info = get_spotify_info()
                with self._lock:
                    self._spotify_running = info.running
                    self._track = info.title or ""
                    self._artist = info.artist or ""
                    self._spotify_error = info.error
            except Exception:
                with self._lock:
                    self._spotify_running = False

        with self._lock:
            bridge_running = self._bridge_running
            usb_connected = self._usb_connected
            spotify_running = self._spotify_running
            spotify_error = self._spotify_error
            track = self._track
            artist = self._artist
            last_error = self._last_error
            thread_alive = self._bridge_thread is not None and self._bridge_thread.is_alive()

        active = bridge_running and thread_alive
        self.bridge_status.title = "Bridge: Running" if active else "Bridge: Stopped"
        if spotify_running:
            if spotify_error:
                if "timed out" in spotify_error.lower():
                    self.spotify_status.title = "Spotify: AppleScript timed out"
                else:
                    self.spotify_status.title = "Spotify: AppleScript error"
            else:
                self.spotify_status.title = "Spotify: Open"
        elif spotify_error:
            self.spotify_status.title = "Spotify: Check Automation permission"
        else:
            self.spotify_status.title = "Spotify: Closed"
        self.calc_status.title = (
            "Calculator: Connected" if usb_connected else "Calculator: Not connected"
        )
        if not usb_connected and last_error:
            shown_error = last_error if len(last_error) <= 48 else last_error[:45] + "…"
            self.track_status.title = f"USB: {shown_error}"
        elif spotify_error:
            shown_error = spotify_error if len(spotify_error) <= 48 else spotify_error[:45] + "…"
            self.track_status.title = f"Spotify: {shown_error}"
        elif spotify_running and track:
            shown = f"{track} — {artist}" if artist else track
            if len(shown) > 48:
                shown = shown[:45] + "…"
            self.track_status.title = f"Track: {shown}"
        elif last_error:
            shown_error = last_error if len(last_error) <= 48 else last_error[:45] + "…"
            self.track_status.title = f"Status: {shown_error}"
        else:
            self.track_status.title = "Track: —"
        self.start_stop.title = "Stop Bridge" if active else "Start Bridge"

    def quit_app(self, _sender) -> None:
        self.stop_bridge()
        thread = self._bridge_thread
        if thread is not None and thread.is_alive():
            thread.join(timeout=1.5)
        rumps.quit_application()


if __name__ == "__main__":
    SpotCEMenuBar().run()
