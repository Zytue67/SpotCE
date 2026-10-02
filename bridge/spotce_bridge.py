#!/usr/bin/env python3
"""SpotCE Mac bridge.

Controls the local Spotify desktop app with AppleScript and exchanges framed
packets over the calculator's CDC serial port. No Spotify Web API is used.
"""

from __future__ import annotations

import argparse
from collections import OrderedDict
from dataclasses import dataclass
import io
import struct
import subprocess
import time
import threading
import unicodedata
import urllib.request
from typing import Optional

try:
    import serial
    from serial.tools import list_ports
except ImportError:  # Protocol tests can run before Mac setup is completed.
    serial = None
    list_ports = None

from PIL import Image

VID = 0x0451
PID = 0xE008
EXPECTED_PRODUCT = "SpotCE Controller"
PACKET_SIZE = 64
PAYLOAD_SIZE = 56
PROTOCOL_VERSION = 1

ART_W = 80
ART_H = 80
ART_PIXELS = ART_W * ART_H
ART_COLORS = 224
ART_PALETTE_OFFSET = 32

PKT_CALC_HELLO = 0x01
PKT_CALC_COMMAND = 0x10
PKT_HOST_HELLO = 0x80
PKT_HOST_STATUS = 0x81
PKT_HOST_TITLE = 0x82
PKT_HOST_ARTIST = 0x83
PKT_HOST_ART_BEGIN = 0x90
PKT_HOST_ART_PALETTE = 0x91
PKT_HOST_ART_PIXELS = 0x92
PKT_HOST_ART_END = 0x93
PKT_HOST_CLEAR_ART = 0x94

CMD_PLAY_PAUSE = 1
CMD_NEXT = 2
CMD_PREVIOUS = 3
CMD_VOLUME_UP = 4
CMD_VOLUME_DOWN = 5

PLAYER_STOPPED = 0
PLAYER_PAUSED = 1
PLAYER_PLAYING = 2
PLAYER_ERROR = 3

SEP = chr(30)

QUERY_SCRIPT = r'''
if application "Spotify" is not running then
    return "0"
end if

tell application "Spotify"
    set separatorChar to character id 30
    set playerStateText to player state as text
    try
        set currentTrack to current track
        set trackName to name of currentTrack
        set artistName to artist of currentTrack
        set trackLink to ""
        try
            set trackLink to spotify url of currentTrack as text
        end try
        set trackDuration to duration of currentTrack
        set trackPosition to player position
        set currentVolume to sound volume
        return "1" & separatorChar & playerStateText & separatorChar & trackName & separatorChar & artistName & separatorChar & trackLink & separatorChar & (trackDuration as text) & separatorChar & (trackPosition as text) & separatorChar & (currentVolume as text)
    on error
        set currentVolume to sound volume
        return "1" & separatorChar & playerStateText & separatorChar & "" & separatorChar & "" & separatorChar & "" & separatorChar & "0" & separatorChar & "0" & separatorChar & (currentVolume as text)
    end try
end tell
'''

ARTWORK_SCRIPT = r'''
if application "Spotify" is not running then
    return ""
end if
tell application "Spotify"
    try
        return artwork url of current track as text
    on error
        return ""
    end try
end tell
'''


@dataclass
class SpotifyInfo:
    running: bool
    state: int = PLAYER_STOPPED
    title: str = ""
    artist: str = ""
    artwork_url: str = ""
    track_id: str = ""
    duration_ms: int = 0
    position_ms: int = 0
    volume: int = 0
    error: str = ""


class Protocol:
    def __init__(self) -> None:
        self.sequence = 1

    def pack(self, packet_type: int, payload: bytes = b"") -> bytes:
        if len(payload) > PAYLOAD_SIZE:
            raise ValueError(f"payload too large: {len(payload)}")
        seq = self.sequence & 0xFFFF
        self.sequence = (self.sequence + 1) & 0xFFFF
        header = struct.pack(
            "<BBBBHH",
            ord("S"), ord("C"), PROTOCOL_VERSION, packet_type, seq, len(payload)
        )
        return header + payload + bytes(PAYLOAD_SIZE - len(payload))

    @staticmethod
    def unpack(packet: bytes) -> Optional[tuple[int, int, bytes]]:
        if len(packet) != PACKET_SIZE:
            return None
        m0, m1, version, packet_type, seq, length = struct.unpack("<BBBBHH", packet[:8])
        if m0 != ord("S") or m1 != ord("C") or version != PROTOCOL_VERSION:
            return None
        if length > PAYLOAD_SIZE:
            return None
        return packet_type, seq, packet[8:8 + length]


def applescript(script: str, timeout: float = 5.0) -> str:
    proc = subprocess.run(
        ["osascript", "-e", script],
        text=True,
        capture_output=True,
        timeout=timeout,
    )
    if proc.returncode != 0:
        raise RuntimeError(proc.stderr.strip() or "AppleScript failed")
    return proc.stdout.rstrip("\r\n")


def spotify_process_running() -> bool:
    """Check the local process table when Apple Events time out."""
    try:
        proc = subprocess.run(
            ["pgrep", "-x", "Spotify"],
            capture_output=True,
            timeout=1.0,
        )
        return proc.returncode == 0
    except (OSError, subprocess.TimeoutExpired):
        return False


def parse_number(text: str, default: float = 0.0) -> float:
    try:
        return float(text.strip().replace(",", "."))
    except (ValueError, AttributeError):
        return default


def get_spotify_info() -> SpotifyInfo:
    try:
        raw = applescript(QUERY_SCRIPT)
    except Exception as exc:
        running = spotify_process_running()
        return SpotifyInfo(
            running=running,
            state=PLAYER_ERROR if running else PLAYER_STOPPED,
            error=str(exc),
        )

    if raw == "0" or not raw:
        return SpotifyInfo(running=False)

    parts = raw.split(SEP)
    while len(parts) < 8:
        parts.append("")

    _, state_text, title, artist, track_id, duration, position, volume = parts[:8]
    state_map = {
        "playing": PLAYER_PLAYING,
        "paused": PLAYER_PAUSED,
        "stopped": PLAYER_STOPPED,
    }
    duration_ms = max(0, int(parse_number(duration)))
    position_ms = max(0, int(parse_number(position) * 1000.0))
    volume_i = max(0, min(100, int(parse_number(volume))))

    if duration_ms and position_ms > duration_ms:
        position_ms = duration_ms

    return SpotifyInfo(
        running=True,
        state=state_map.get(state_text.lower(), PLAYER_STOPPED),
        title=title,
        artist=artist,
        track_id=track_id or f"{title}|{artist}|{duration_ms}",
        duration_ms=duration_ms,
        position_ms=position_ms,
        volume=volume_i,
    )


def get_spotify_artwork_url() -> str:
    """Fetch cover art separately so a slow artwork property cannot block status."""
    try:
        return applescript(ARTWORK_SCRIPT, timeout=2.0).strip()
    except Exception:
        return ""


def run_spotify_command(command: int) -> None:
    if command == CMD_PLAY_PAUSE:
        script = 'tell application "Spotify" to playpause'
    elif command == CMD_NEXT:
        script = 'tell application "Spotify" to next track'
    elif command == CMD_PREVIOUS:
        script = 'tell application "Spotify" to previous track'
    elif command == CMD_VOLUME_UP:
        script = r'''
tell application "Spotify"
    set v to sound volume + 5
    if v > 100 then set v to 100
    set sound volume to v
end tell
'''
    elif command == CMD_VOLUME_DOWN:
        script = r'''
tell application "Spotify"
    set v to sound volume - 5
    if v < 0 then set v to 0
    set sound volume to v
end tell
'''
    else:
        return

    try:
        if command in (CMD_PLAY_PAUSE, CMD_NEXT, CMD_PREVIOUS):
            # Spotify may perform a transport command but delay its reply.
            # Ignore that reply so a slow Apple Event cannot stall USB polling.
            script = f"ignoring application responses\n{script}\nend ignoring"
            applescript(script, timeout=3.0)
        else:
            applescript(script, timeout=10.0)
    except Exception as exc:
        print(f"[spotify] command failed: {exc}")


def calc_text(text: str) -> bytes:
    text = (text.replace("’", "'").replace("‘", "'")
                .replace("“", '"').replace("”", '"')
                .replace("–", "-").replace("—", "-"))
    text = text.replace("\r", " ").replace("\n", " ").replace("\t", " ")
    normalized = unicodedata.normalize("NFKD", text)
    ascii_text = normalized.encode("ascii", "replace").decode("ascii")
    ascii_text = "".join(ch if 32 <= ord(ch) <= 126 else " " for ch in ascii_text)
    return ascii_text.encode("ascii", "replace")[:PAYLOAD_SIZE]


def rgb1555(r: int, g: int, b: int) -> int:
    return ((r & 0xFF) >> 3) << 10 | ((g & 0xFF) >> 3) << 5 | ((b & 0xFF) >> 3)


def convert_art_image(image: Image.Image) -> tuple[list[int], bytes]:
    image = image.convert("RGB")
    resampling = getattr(Image, "Resampling", Image).LANCZOS
    image = image.resize((ART_W, ART_H), resampling)

    quantize_enum = getattr(Image, "Quantize", None)
    if quantize_enum is not None:
        q = image.quantize(colors=ART_COLORS, method=quantize_enum.MEDIANCUT)
    else:
        q = image.quantize(colors=ART_COLORS, method=Image.MEDIANCUT)

    raw_palette = q.getpalette() or []
    needed = ART_COLORS * 3
    raw_palette = (raw_palette + [0] * needed)[:needed]

    palette_1555: list[int] = []
    for i in range(ART_COLORS):
        r, g, b = raw_palette[i * 3:i * 3 + 3]
        palette_1555.append(rgb1555(r, g, b))

    pixels = bytes((index + ART_PALETTE_OFFSET) & 0xFF for index in q.tobytes())
    if len(pixels) != ART_PIXELS:
        raise ValueError("unexpected artwork size")
    return palette_1555, pixels


def download_and_convert_art(url: str) -> tuple[list[int], bytes]:
    req = urllib.request.Request(url, headers={"User-Agent": "SpotCE/1.0"})
    with urllib.request.urlopen(req, timeout=6.0) as response:
        data = response.read(2_000_000)
    with Image.open(io.BytesIO(data)) as image:
        return convert_art_image(image)


class ArtCache:
    def __init__(self, max_items: int = 10) -> None:
        self.max_items = max_items
        self.items: OrderedDict[str, tuple[list[int], bytes]] = OrderedDict()

    def get(self, url: str) -> tuple[list[int], bytes]:
        if url in self.items:
            value = self.items.pop(url)
            self.items[url] = value
            return value
        value = download_and_convert_art(url)
        self.items[url] = value
        while len(self.items) > self.max_items:
            self.items.popitem(last=False)
        return value


class PacketStream:
    """Reassemble fixed-size packets from the CDC serial byte stream."""

    def __init__(self) -> None:
        self.buffer = bytearray()

    def feed(self, data: bytes) -> list[tuple[int, int, bytes]]:
        self.buffer.extend(data)
        packets = []
        while len(self.buffer) >= PACKET_SIZE:
            if self.buffer[:2] != b"SC":
                marker = self.buffer.find(b"SC", 1)
                if marker < 0:
                    del self.buffer[:-1]
                    break
                del self.buffer[:marker]
                continue
            decoded = Protocol.unpack(bytes(self.buffer[:PACKET_SIZE]))
            if decoded is None:
                del self.buffer[0]
                continue
            packets.append(decoded)
            del self.buffer[:PACKET_SIZE]
        return packets


class SpotCESerial:
    def __init__(self, verbose: bool = False) -> None:
        self.verbose = verbose
        self.dev = None
        self.protocol = Protocol()
        self.stream = PacketStream()
        self.last_error = ""

    def disconnect(self) -> None:
        if self.dev is not None:
            try:
                self.dev.close()
            except Exception:
                pass
        self.dev = None
        self.stream = PacketStream()

    @staticmethod
    def _candidate_ports():
        if list_ports is None:
            raise RuntimeError("pyserial is missing; run ./setup_mac.sh")
        ports = list(list_ports.comports())
        exact = [p for p in ports if p.vid == VID and p.pid == PID]
        mac_serial = [p for p in ports if "/dev/cu.usbmodem" in p.device]
        # ForumCE uses the TI CDC serial interface. Some macOS drivers omit
        # VID/PID in port metadata, so retain the usbmodem fallback.
        return exact + [p for p in mac_serial if p not in exact], ports

    def connect(self) -> bool:
        self.disconnect()
        try:
            candidates, ports = self._candidate_ports()
        except Exception as exc:
            self.last_error = f"Serial scan failed: {exc}"
            if self.verbose:
                print(f"[serial] {self.last_error}")
            return False

        if not candidates:
            if ports:
                names = ", ".join(p.device for p in ports[:3])
                self.last_error = f"No TI serial port; found {names}"
            else:
                self.last_error = "No TI USB serial port; quit TI Connect CE, run SPOTCE, reconnect USB"
            return False

        failures = []
        for port in candidates:
            try:
                self.dev = serial.Serial(port.device, baudrate=115200, timeout=0, write_timeout=1)
                self.dev.reset_input_buffer()
                self.stream = PacketStream()
                self.send(PKT_HOST_HELLO, b"SpotCE Mac Bridge V1")
                self.last_error = ""
                print(f"[serial] SpotCE connected on {port.device}")
                return True
            except Exception as exc:
                failures.append(f"{port.device}: {exc}")
                if self.dev is not None:
                    try:
                        self.dev.close()
                    except Exception:
                        pass
                self.dev = None
                if self.verbose:
                    print(f"[serial] candidate failed: {exc}")

        self.last_error = "Could not open TI serial port: " + "; ".join(failures[:2])
        if self.verbose:
            print(f"[serial] {self.last_error}")
        return False

    def send(self, packet_type: int, payload: bytes = b"") -> None:
        if self.dev is None:
            raise RuntimeError("SpotCE serial port is not connected")
        packet = self.protocol.pack(packet_type, payload)
        offset = 0
        while offset < len(packet):
            written = self.dev.write(packet[offset:])
            if not written:
                raise IOError("serial write timed out")
            offset += written

    def read_packets(self) -> list[tuple[int, int, bytes]]:
        if self.dev is None:
            return []
        available = self.dev.in_waiting
        if not available:
            return []
        return self.stream.feed(bytes(self.dev.read(min(available, 1024))))

    def send_status(self, info: SpotifyInfo) -> None:
        payload = struct.pack(
            "<BBBBII",
            1 if info.running else 0,
            info.state,
            info.volume,
            0,
            info.position_ms & 0xFFFFFFFF,
            info.duration_ms & 0xFFFFFFFF,
        )
        self.send(PKT_HOST_STATUS, payload)

    def send_track_text(self, info: SpotifyInfo) -> None:
        self.send(PKT_HOST_TITLE, calc_text(info.title or "No track"))
        self.send(PKT_HOST_ARTIST, calc_text(info.artist or ""))

    def clear_art(self) -> None:
        self.send(PKT_HOST_CLEAR_ART)

    def send_art(self, palette: list[int], pixels: bytes) -> None:
        self.send(PKT_HOST_ART_BEGIN, struct.pack("<BBH", ART_W, ART_H, ART_COLORS))

        offset = 0
        while offset < ART_COLORS:
            count = min(27, ART_COLORS - offset)
            payload = bytearray([offset, count])
            for color in palette[offset:offset + count]:
                payload.extend(struct.pack("<H", color))
            self.send(PKT_HOST_ART_PALETTE, bytes(payload))
            offset += count

        offset = 0
        packets = 0
        while offset < len(pixels):
            count = min(53, len(pixels) - offset)
            payload = struct.pack("<HB", offset, count) + pixels[offset:offset + count]
            self.send(PKT_HOST_ART_PIXELS, payload)
            offset += count
            packets += 1
            if packets % 16 == 0:
                time.sleep(0.001)

        self.send(PKT_HOST_ART_END)


def print_spotify_test() -> int:
    info = get_spotify_info()
    if info.error:
        print(f"Spotify check failed: {info.error}")
        if info.running:
            print("Spotify is running, but Apple Events did not return its playback status.")
        print("Check System Settings > Privacy & Security > Automation and allow Terminal to control Spotify.")
        return 1
    if not info.running:
        print("Spotify is not running.")
        return 1
    state_name = {
        PLAYER_STOPPED: "stopped",
        PLAYER_PAUSED: "paused",
        PLAYER_PLAYING: "playing",
        PLAYER_ERROR: "status unavailable",
    }.get(info.state, "status unavailable")
    print(f"State:  {state_name}")
    print(f"Track:  {info.title}")
    print(f"Artist: {info.artist}")
    print(f"Volume: {info.volume}%")
    print(f"Time:   {info.position_ms / 1000:.1f}s / {info.duration_ms / 1000:.1f}s")
    print(f"Art:    {info.artwork_url or '(none)'}")
    return 0


def run_bridge(
    *,
    no_art: bool = False,
    poll_interval: float = 0.5,
    verbose: bool = False,
    stop_event: Optional[threading.Event] = None,
    status_callback=None,
) -> int:
    """Run the bridge loop. Safe to call from a background thread for the GUI app."""
    if stop_event is None:
        stop_event = threading.Event()

    poll_interval = max(0.2, poll_interval)
    link = SpotCESerial(verbose=verbose)
    art_cache = ArtCache()
    current_track_id: Optional[str] = None
    next_connect = 0.0
    next_spotify_poll = 0.0
    last_usb_state: Optional[bool] = None
    last_spotify_running: Optional[bool] = None

    def notify(event: str, value) -> None:
        if status_callback is None:
            return
        try:
            status_callback(event, value)
        except Exception:
            pass

    print("SpotCE bridge")
    print("-------------")
    print("1. Open Spotify on this Mac.")
    print("2. Run SPOTCE on the calculator.")
    print("3. Keep TI Connect CE fully quit while the bridge is running.")
    print("Press Ctrl+C to stop.\n")
    notify("bridge", True)

    try:
        while not stop_event.is_set():
            now = time.monotonic()

            if link.dev is None:
                if last_usb_state is not False:
                    last_usb_state = False
                    notify("usb", False)
                if now >= next_connect:
                    if not link.connect():
                        if link.last_error:
                            notify("error", link.last_error)
                        if verbose:
                            print("[serial] waiting for SpotCE...", end="\r", flush=True)
                    else:
                        last_usb_state = True
                        notify("usb", True)
                        next_spotify_poll = 0.0
                    next_connect = now + 1.0
                stop_event.wait(0.05)
                continue

            try:
                if last_usb_state is not True:
                    last_usb_state = True
                    notify("usb", True)

                for packet in link.read_packets():
                    packet_type, _seq, payload = packet
                    if packet_type == PKT_CALC_HELLO:
                        if verbose:
                            print(f"\n[serial] calculator hello: {payload!r}")
                        link.send(PKT_HOST_HELLO, b"SpotCE Mac Bridge V1")
                        next_spotify_poll = 0.0
                    elif packet_type == PKT_CALC_COMMAND and payload:
                        command = payload[0]
                        if verbose:
                            print(f"\n[calc] command {command}")
                        run_spotify_command(command)
                        next_spotify_poll = 0.0

                now = time.monotonic()
                if now >= next_spotify_poll:
                    latest_info = get_spotify_info()
                    if latest_info.error and verbose:
                        print(f"\n[spotify] status query failed: {latest_info.error}")
                    notify("spotify_error", latest_info.error)
                    if latest_info.running != last_spotify_running:
                        last_spotify_running = latest_info.running
                        notify("spotify", latest_info.running)
                    notify("track", latest_info)
                    link.send_status(latest_info)

                    if latest_info.error and latest_info.running:
                        # A slow Apple Event is not evidence that the app
                        # closed; preserve the last good track/art and retry.
                        if current_track_id is None:
                            link.send(PKT_HOST_TITLE, calc_text("Spotify info unavailable"))
                            link.send(PKT_HOST_ARTIST, calc_text("Check Mac automation"))
                            current_track_id = "__spotify_status_unavailable__"
                        next_spotify_poll = now + poll_interval
                        continue

                    new_track_id = latest_info.track_id if latest_info.running else ""
                    if new_track_id != current_track_id:
                        current_track_id = new_track_id
                        link.send_track_text(latest_info)
                        link.clear_art()

                        if latest_info.running and not no_art:
                            latest_info.artwork_url = get_spotify_artwork_url()
                        if latest_info.running and latest_info.artwork_url and not no_art:
                            try:
                                print(f"\n[track] {latest_info.title} — {latest_info.artist}")
                                palette, pixels = art_cache.get(latest_info.artwork_url)
                                link.send_art(palette, pixels)
                            except Exception as exc:
                                print(f"[art] skipped: {exc}")
                                notify("error", f"Artwork: {exc}")
                        elif latest_info.running:
                            print(f"\n[track] {latest_info.title} — {latest_info.artist}")

                    next_spotify_poll = now + poll_interval

            except (OSError, RuntimeError) as exc:
                print(f"\n[serial] disconnected: {exc}")
                notify("error", f"USB serial: {exc}")
                link.disconnect()
                current_track_id = None
                last_usb_state = False
                notify("usb", False)
                next_connect = time.monotonic() + 0.5

            stop_event.wait(0.01)

    except KeyboardInterrupt:
        pass
    finally:
        link.disconnect()
        notify("usb", False)
        notify("bridge", False)
        print("\nStopping SpotCE bridge.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="SpotCE Mac bridge (no Spotify API)")
    parser.add_argument("--no-art", action="store_true", help="disable album-art transfer")
    parser.add_argument("--poll", type=float, default=0.5, help="Spotify poll interval in seconds")
    parser.add_argument("--verbose", action="store_true")
    parser.add_argument("--spotify-test", action="store_true", help="test AppleScript only and exit")
    args = parser.parse_args()

    if args.spotify_test:
        return print_spotify_test()

    return run_bridge(
        no_art=args.no_art,
        poll_interval=args.poll,
        verbose=args.verbose,
    )


if __name__ == "__main__":
    raise SystemExit(main())
