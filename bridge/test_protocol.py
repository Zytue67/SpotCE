#!/usr/bin/env python3
import unittest
from unittest.mock import patch

from PIL import Image

from spotce_bridge import (
    ART_H,
    ART_COLORS,
    ART_PALETTE_OFFSET,
    ART_PIXELS,
    ART_W,
    CMD_NEXT,
    PACKET_SIZE,
    PLAYER_ERROR,
    PLAYER_PLAYING,
    PKT_HOST_TITLE,
    QUERY_SCRIPT,
    Protocol,
    PacketStream,
    convert_art_image,
    get_spotify_info,
    run_spotify_command,
)


class ProtocolTests(unittest.TestCase):
    def test_packet_is_64_bytes(self):
        proto = Protocol()
        packet = proto.pack(PKT_HOST_TITLE, b"Hello")
        self.assertEqual(len(packet), PACKET_SIZE)

    def test_round_trip(self):
        proto = Protocol()
        packet = proto.pack(PKT_HOST_TITLE, b"Hello")
        unpacked = Protocol.unpack(packet)
        self.assertIsNotNone(unpacked)
        packet_type, _seq, payload = unpacked
        self.assertEqual(packet_type, PKT_HOST_TITLE)
        self.assertEqual(payload, b"Hello")

    def test_art_conversion(self):
        image = Image.new("RGB", (160, 160), (20, 140, 220))
        palette, pixels = convert_art_image(image)
        self.assertEqual(len(palette), ART_COLORS)
        self.assertEqual(len(pixels), ART_PIXELS)
        self.assertGreaterEqual(min(pixels), ART_PALETTE_OFFSET)
        self.assertLessEqual(max(pixels), 255)
        self.assertEqual(ART_W, 80)
        self.assertEqual(ART_H, 80)

    def test_stream_reassembles_split_packets(self):
        packet = Protocol().pack(PKT_HOST_TITLE, b"A title")
        stream = PacketStream()
        self.assertEqual(stream.feed(packet[:13]), [])
        decoded = stream.feed(packet[13:])
        self.assertEqual(decoded[0][0], PKT_HOST_TITLE)
        self.assertEqual(decoded[0][2], b"A title")

    def test_stream_recovers_after_noise_and_coalesces_packets(self):
        proto = Protocol()
        first = proto.pack(PKT_HOST_TITLE, b"one")
        second = proto.pack(PKT_HOST_TITLE, b"two")
        stream = PacketStream()
        decoded = stream.feed(b"noise" + first + second)
        self.assertEqual([packet[2] for packet in decoded], [b"one", b"two"])

    @patch("spotce_bridge.spotify_process_running", return_value=True)
    @patch("spotce_bridge.applescript", side_effect=TimeoutError("AppleScript timed out"))
    def test_spotify_timeout_is_not_reported_as_closed(self, _script, _running):
        info = get_spotify_info()
        self.assertTrue(info.running)
        self.assertEqual(info.state, PLAYER_ERROR)
        self.assertIn("timed out", info.error)

    @patch("spotce_bridge.applescript")
    def test_transport_command_ignores_slow_spotify_reply(self, script):
        run_spotify_command(CMD_NEXT)
        sent_script = script.call_args.args[0]
        self.assertIn("ignoring application responses", sent_script)
        self.assertIn("next track", sent_script)

    @patch("spotce_bridge.applescript", return_value=(
        "1\x1eplaying\x1eTrack Name\x1eArtist Name\x1espotify:track:abc\x1e180000\x1e12.5\x1e42"
    ))
    def test_status_query_parses_track_without_artwork_lookup(self, _script):
        info = get_spotify_info()
        self.assertTrue(info.running)
        self.assertEqual(info.state, PLAYER_PLAYING)
        self.assertEqual(info.title, "Track Name")
        self.assertEqual(info.artist, "Artist Name")
        self.assertEqual(info.position_ms, 12500)
        self.assertEqual(info.volume, 42)
        self.assertNotIn("artwork url", QUERY_SCRIPT.lower())


if __name__ == "__main__":
    unittest.main()
