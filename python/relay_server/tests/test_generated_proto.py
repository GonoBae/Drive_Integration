import subprocess
import sys
import tempfile
import unittest
from pathlib import Path


class GeneratedProtoTest(unittest.TestCase):
    def test_checked_in_python_binding_matches_vehicle_proto(self) -> None:
        repository_dir = Path(__file__).resolve().parents[3]
        protocol_dir = repository_dir / "protocol"
        generated_file = (
            repository_dir
            / "python"
            / "relay_server"
            / "generated"
            / "vehicle_pb2.py"
        )

        with tempfile.TemporaryDirectory() as output_dir:
            subprocess.run(
                [
                    sys.executable,
                    "-m",
                    "grpc_tools.protoc",
                    "-I",
                    str(protocol_dir),
                    f"--python_out={output_dir}",
                    str(protocol_dir / "vehicle.proto"),
                ],
                cwd=repository_dir,
                check=True,
                capture_output=True,
                text=True,
            )
            regenerated = Path(output_dir) / "vehicle_pb2.py"
            self.assertEqual(generated_file.read_bytes(), regenerated.read_bytes())


if __name__ == "__main__":
    unittest.main()
