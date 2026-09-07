"""Run the Windows Core automatic checks without touching the user's server.

python scripts/check_core.py --engine-root "B:/Epic Games/UE_5.6"

All generated logs/reports stay under runtime_logs. A passing automatic report
is NOT packaged performance, manual driving, art, or 30-minute acceptance.
"""

import argparse
from dataclasses import dataclass
from datetime import datetime, timezone
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time
import uuid
import xml.etree.ElementTree as ET


ROOT = Path(__file__).resolve().parents[1]
MANUAL_GATES = [
    "PIE handling, handbrake, curb/wall/rollover and dynamic collision appearance",
    "PIE HUD, traffic, sensor/ghost presentation and reconnect",
    "Packaged target-PC 1920x1080 uncapped frame-time and input-latency measurement",
    "30-minute continuous manual driving stability",
    "Final art/license review and replay video capture",
]


@dataclass(frozen=True)
class Step:
    name: str
    command: list
    cwd: Path
    timeout: int = 600
    verify: str = ""


def child_environment():
    # MSBuild rejects duplicate Path/PATH variables inherited from some shells.
    environment = dict(os.environ)
    if os.name == "nt":
        path_value = next((value for key, value in environment.items()
                           if key.lower() == "path"), "")
        environment = {key: value for key, value in environment.items()
                       if key.lower() != "path"}
        environment["Path"] = path_value
        environment["MSBUILDDISABLENODEREUSE"] = "1"
    return environment


def unreal_tools(engine_root):
    engine = engine_root.resolve() / "Engine"
    path_script = engine / "Build/BatchFiles/GetDotnetPath.bat"
    version = re.search(r"(?im)^set UE_DOTNET_VERSION=([\d.]+)\s*$",
                        path_script.read_text(encoding="utf-8"))
    if not version:
        raise ValueError("Cannot identify the engine's bundled .NET version")
    return (
        engine / f"Binaries/ThirdParty/DotNet/{version[1]}/win-x64/dotnet.exe",
        engine / "Binaries/DotNET/UnrealBuildTool/UnrealBuildTool.dll",
        engine / "Binaries/Win64/UnrealEditor-Cmd.exe",
    )


def make_steps(engine_root, output, skip_build=False, skip_unreal=False):
    host_root = ROOT / "cpp/host"
    project = ROOT / "unreal/DriveIntegration/DriveIntegration.uproject"
    python = sys.executable
    steps = []
    if not skip_build:
        steps += [
            Step("cpp-configure", ["cmake", "--preset", "release"], host_root, 1200),
            Step("cpp-build", ["cmake", "--build", "--preset", "release"], host_root, 1800),
        ]
    steps += [
        Step("ctest", ["ctest", "--test-dir", str(host_root / "build"),
                       "-C", "Release", "--output-on-failure", "--no-tests=error",
                       "--output-junit", str(output / "ctest.xml")], ROOT, 600, "ctest"),
        Step("generated-proto", [python, "-m", "unittest", "-v",
                                 "python.relay_server.tests.test_generated_proto"], ROOT),
        Step("python-tools", [python, "-m", "unittest", "discover", "-s", "scripts",
                              "-p", "test_*.py", "-v"], ROOT),
        Step("launcher", ["powershell.exe", "-NoProfile", "-NonInteractive",
                          "-ExecutionPolicy", "Bypass", "-File",
                          str(ROOT / "scripts/test_server_launcher.ps1")], ROOT),
        Step("package-plan", ["powershell.exe", "-NoProfile", "-NonInteractive",
                              "-ExecutionPolicy", "Bypass", "-File",
                              str(ROOT / "scripts/test_package_windows.ps1")], ROOT),
        Step("signal-city-wire", [python, str(ROOT / "scripts/smoke_signal_city.py")], ROOT, 120),
        Step("physics-replay-wire", [python, str(ROOT / "scripts/smoke_physics_replay.py")],
             ROOT, 180),
    ]
    if not skip_unreal:
        dotnet, ubt, editor = unreal_tools(engine_root)
        if not skip_build:
            for target in ("DriveIntegrationEditor", "DriveIntegration"):
                steps.append(Step("ue-build-" + target,
                                  [str(dotnet), str(ubt), target, "Win64", "Development",
                                   "-Project=" + str(project), "-WaitMutex", "-NoHotReloadFromIDE"],
                                  engine_root / "Engine/Source", 1800))
        common = [str(editor), str(project), "-unattended", "-nop4", "-NullRHI",
                  "-NoSound", "-NoSplash", "-stdout", "-FullStdOutLogOutput"]
        steps.append(Step("ue-automation", common + [
            "-ExecCmds=Automation RunTests DriveIntegration",
            "-TestExit=Automation Test Queue Empty",
            "-ReportExportPath=" + str(output / "ue-automation"),
            "-abslog=" + str(output / "ue-automation-engine.log")],
            ROOT, 600, "automation"))
        for profile in ("virtual", "signal"):
            for commandlet in ("BuildVirtualCity", "ExportVirtualCityTraffic"):
                name = "ue-" + profile + "-" + commandlet
                extra = ["-SignalCity"] if profile == "signal" else []
                if commandlet == "ExportVirtualCityTraffic":
                    extra += ["-nowrite"]
                steps.append(Step(name, common + ["-run=" + commandlet, "-ValidateOnly"]
                                  + extra + ["-abslog=" + str(output / (name + "-engine.log"))],
                                  ROOT))
    return steps


def verify_ctest(path):
    document = ET.parse(path)
    cases = document.findall(".//testcase")
    if not cases or any(case.find(tag) is not None for case in cases
                        for tag in ("failure", "error", "skipped")):
        raise ValueError("CTest report is empty, failed or skipped")
    return {"passed": len(cases)}


def verify_automation(log):
    declared = re.findall(r"Found (\d+) automation tests based on 'DriveIntegration'", log)
    completed = re.findall(r"Test Completed\. Result=\{([^}]+)\}.*?Path=\{([^}]+)\}", log)
    if not declared or int(declared[-1]) < 1:
        raise ValueError("UE did not discover any DriveIntegration automation tests")
    expected = int(declared[-1])
    if len(completed) != expected or len({path for _, path in completed}) != expected:
        raise ValueError("UE test completion count does not match discovery")
    if any(result != "Success" for result, _ in completed):
        raise ValueError("UE automation contains failed or skipped tests")
    return {"passed": expected}


def terminate_child(process):
    if process.poll() is not None:
        return
    if os.name == "nt":
        # Only this just-created process tree, never by executable name/port.
        try:
            subprocess.run(["taskkill.exe", "/PID", str(process.pid), "/T", "/F"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           timeout=15, creationflags=subprocess.CREATE_NO_WINDOW)
        except (OSError, subprocess.SubprocessError):
            # Keep the direct-child fallback even if tree cleanup is unavailable.
            pass
    else:
        process.terminate()
    try:
        process.wait(timeout=10)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=10)


def git_metadata():
    result = {"git_commit": "unavailable", "worktree_dirty": None}
    for args, key in ((["rev-parse", "HEAD"], "git_commit"),
                      (["status", "--porcelain"], "worktree_dirty")):
        try:
            query = subprocess.run(["git"] + args, cwd=ROOT, capture_output=True,
                                   text=True, timeout=10)
            if query.returncode == 0:
                result[key] = query.stdout.strip() if key == "git_commit" else bool(query.stdout.strip())
        except (OSError, subprocess.SubprocessError):
            pass
    return result


def execute_step(step, output):
    log_path = output / (step.name + ".log")
    started = time.monotonic()
    result = {"name": step.name, "command": step.command, "log": str(log_path),
              "status": "failed"}
    print("RUN " + step.name, flush=True)
    try:
        with log_path.open("wb") as log:
            process = subprocess.Popen(step.command, cwd=step.cwd, env=child_environment(),
                                       stdout=log, stderr=subprocess.STDOUT,
                                       creationflags=(subprocess.CREATE_NO_WINDOW
                                                      if os.name == "nt" else 0))
            try:
                while True:
                    remaining = step.timeout - (time.monotonic() - started)
                    if remaining <= 0:
                        raise subprocess.TimeoutExpired(step.command, step.timeout)
                    try:
                        result["exit_code"] = process.wait(timeout=min(30, remaining))
                        break
                    except subprocess.TimeoutExpired:
                        print("RUNNING " + step.name, flush=True)
            finally:
                terminate_child(process)
        if result["exit_code"] != 0:
            raise ValueError("Command failed with exit code " + str(result["exit_code"]))
        if step.verify == "ctest":
            result["tests"] = verify_ctest(output / "ctest.xml")
        elif step.verify == "automation":
            result["tests"] = verify_automation(log_path.read_text(encoding="utf-8", errors="replace"))
        result["status"] = "passed"
    except (OSError, ValueError, subprocess.SubprocessError, ET.ParseError) as error:
        result["error"] = str(error)
    result["seconds"] = round(time.monotonic() - started, 3)
    print(result["status"].upper() + " " + step.name + " -> " + str(log_path), flush=True)
    return result


def preflight(engine_root, skip_unreal, skip_build):
    if os.name != "nt":
        raise ValueError("This runner targets the Windows/UE 5.6 development workflow")
    for program in ("cmake", "ctest", "powershell.exe"):
        if not shutil.which(program):
            raise ValueError("Required executable not on PATH: " + program)
    for module in ("grpc_tools", "websockets", "google.protobuf"):
        if importlib.util.find_spec(module) is None:
            raise ValueError("Missing Python dependency: " + module)
    for script in ("smoke_physics_replay.py", "smoke_signal_city.py", "test_server_launcher.ps1",
                   "test_package_windows.ps1"):
        if not (ROOT / "scripts" / script).is_file():
            raise ValueError("Required check missing: " + script)
    if skip_build and not (ROOT / "cpp/host/build/Release/simcore_publisher.exe").is_file():
        raise ValueError("--skip-build requires an already built Release host")
    if not skip_unreal:
        if engine_root is None:
            raise ValueError("Pass --engine-root pointing to the UE_5.6 installation")
        for tool in unreal_tools(engine_root):
            if not tool.is_file():
                raise ValueError("Required UE tool missing: " + str(tool))


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--engine-root", type=Path)
    parser.add_argument("--skip-build", action="store_true", help="Check prebuilt binaries; report PARTIAL")
    parser.add_argument("--skip-unreal", action="store_true", help="Host/tools only; report PARTIAL")
    parser.add_argument("--preflight-only", action="store_true", help="Check tools and print steps without executing")
    args = parser.parse_args()
    try:
        preflight(args.engine_root, args.skip_unreal, args.skip_build)
    except (OSError, ValueError, ImportError) as error:
        parser.exit(2, "Preflight failed: " + str(error) + "\n")
    output = ROOT / "runtime_logs" / ("core-check-" + datetime.now().strftime("%Y%m%d-%H%M%S")
                                      + "-" + uuid.uuid4().hex[:8])
    steps = make_steps(args.engine_root, output, args.skip_build, args.skip_unreal)
    if args.preflight_only:
        print("PREFLIGHT ONLY (no tests executed):\n" + "\n".join(step.name for step in steps))
        return 0
    output.mkdir(parents=True, exist_ok=False)
    report = {"schema_version": 1, "started_utc": datetime.now(timezone.utc).isoformat(),
              "skip_build": args.skip_build, "skip_unreal": args.skip_unreal,
              "status": "running", "manual_gates_not_run": MANUAL_GATES, "steps": []}
    report_path = output / "report.json"
    report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    try:
        report.update(git_metadata())
        for step in steps:
            report["steps"].append(execute_step(step, output))
            report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
            if report["steps"][-1]["status"] != "passed":
                report["status"] = "failed"
                break
        else:
            report["status"] = "partial_pass" if args.skip_build or args.skip_unreal else "automatic_pass"
    except KeyboardInterrupt:
        report["status"] = "interrupted"
    finally:
        report["finished_utc"] = datetime.now(timezone.utc).isoformat()
        report_path.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(report["status"].upper() + " report: " + str(report_path))
    print("Manual/packaged acceptance remains NOT RUN.")
    return 0 if report["status"] in ("automatic_pass", "partial_pass") else 1


if __name__ == "__main__":
    sys.exit(main())
