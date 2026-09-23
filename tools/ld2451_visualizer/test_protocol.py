import unittest

from ld2451_protocol import FrameStream, build_command, parse_ack, parse_report


class ProtocolTests(unittest.TestCase):
    def test_documented_three_target_example(self):
        frame = bytes.fromhex(
            "F4 F3 F2 F1 11 00 03 01 "
            "8A 28 00 3C 15 8A 1E 01 3C 0F 76 5F 00 3C 0F "
            "F8 F7 F6 F5"
        )
        report = parse_report(frame)
        self.assertTrue(report.alarm)
        self.assertEqual(len(report.targets), 3)
        self.assertEqual((report.targets[0].angle_deg, report.targets[0].distance_m), (10, 40))
        self.assertFalse(report.targets[0].approaching)
        self.assertTrue(report.targets[1].approaching)
        self.assertEqual(report.targets[2].angle_deg, -10)

    def test_fragmented_stream_and_ack(self):
        report = bytes.fromhex("F4 F3 F2 F1 02 00 00 00 F8 F7 F6 F5")
        ack = bytes.fromhex("FD FC FB FA 08 00 12 01 00 00 64 01 05 02 04 03 02 01")
        stream = FrameStream()
        self.assertEqual(stream.feed(b"junk" + report[:7]), [])
        frames = stream.feed(report[7:] + ack)
        self.assertEqual(frames, [report, ack])
        parsed = parse_ack(ack)
        self.assertEqual(parsed.command, 0x12)
        self.assertEqual(parsed.payload, bytes((100, 1, 5, 2)))

    def test_zero_length_report_is_no_target_heartbeat(self):
        frame = bytes.fromhex("F4 F3 F2 F1 00 00 F8 F7 F6 F5")
        report = parse_report(frame)
        self.assertFalse(report.alarm)
        self.assertEqual(report.targets, ())

        stream = FrameStream()
        self.assertEqual(stream.feed(frame[:6]), [])
        self.assertEqual(stream.feed(frame[6:]), [frame])

    def test_live_direction_byte_one_is_approaching(self):
        frame = bytes.fromhex("F4 F3 F2 F1 07 00 01 01 8F 04 01 03 FF F8 F7 F6 F5")
        report = parse_report(frame)
        self.assertTrue(report.alarm)
        self.assertTrue(report.targets[0].approaching)
        self.assertEqual(report.targets[0].speed_kmh, 3)

    def test_build_enable_configuration(self):
        self.assertEqual(
            build_command(0x00FF, b"\x01\x00"),
            bytes.fromhex("FD FC FB FA 04 00 FF 00 01 00 04 03 02 01"),
        )


if __name__ == "__main__":
    unittest.main()
