"""Real urllib request boundaries for the shared GSM web test client."""
import io
import unittest
from unittest.mock import Mock, patch

from oracle import WebDevice


class Response(io.BytesIO):
    status = 200


class WebClientTests(unittest.TestCase):
    def setUp(self):
        self.web = WebDevice("192.0.2.1")
        self.web.opener = Mock()
        self.web.opener.open.return_value = Response(b'{"status":"ok"}')

    def request(self):
        return self.web.opener.open.call_args.args[0]

    def test_binary_firmware_chunk_keeps_every_octet(self):
        chunk = bytes(range(256)) * 16
        self.web.request("/c?o=0&cs=123", chunk)
        request = self.request()
        self.assertEqual(request.data, chunk)
        self.assertEqual(request.get_method(), "POST")
        self.assertEqual(request.get_header("Content-type"), "application/octet-stream")

    def test_empty_binary_body_is_still_post(self):
        self.web.request("/c", b"")
        self.assertEqual(self.request().get_method(), "POST")
        self.assertEqual(self.request().data, b"")

    def test_json_encoding_and_query_token_are_preserved(self):
        self.web.token = "test&token"
        self.web.request("/s?probe=1", {"s": 4096, "h": 123})
        request = self.request()
        self.assertEqual(request.data, b'{"s":4096,"h":123}')
        self.assertEqual(request.get_header("Content-type"), "application/json")
        self.assertEqual(request.full_url, "http://192.0.2.1/s?probe=1&t=test%26token")

    def test_read_only_request_remains_get(self):
        self.web.request("/fw_status")
        self.assertEqual(self.request().get_method(), "GET")
        self.assertIsNone(self.request().data)

    @patch("oracle.time.sleep")
    def test_listener_reopen_connection_failure_retries_same_binary_body(self, sleep):
        self.web.opener.open.side_effect = [ConnectionError(), Response(b"{}")]
        chunk = b"\x00\xff\r\nOK\r\n"
        self.web.request("/c", chunk)
        self.assertEqual(self.web.opener.open.call_count, 2)
        for call in self.web.opener.open.call_args_list:
            self.assertEqual(call.args[0].data, chunk)
        sleep.assert_called_once()


if __name__ == "__main__":
    unittest.main()
