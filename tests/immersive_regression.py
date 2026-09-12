#!/usr/bin/env python3
"""Exercise the six immersive-player features offscreen with disposable profiles."""
import argparse
import hashlib
import html
import json
import os
from pathlib import Path
import re
import shutil
import signal
import subprocess
import sys
import time

FEATURES = {
    "layouts": "Saved layouts and artwork fallback",
    "idle-controls": "Opt-in control fading and visible cursor",
    "queue": "Queue selection, removal, undo and sheet lifetime",
    "shortcut-hud": "Keyboard feedback and text-input guards",
    "collection-navigation": "Artist/album navigation and return",
    "mini-lyrics": "Mini-player lyrics, gaps and transitions",
}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True, type=Path, help="SUNG_DIAGNOSTICS build")
    parser.add_argument("--output", required=True, type=Path, help="New private report directory")
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    source = args.binary.resolve(strict=True)
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False, mode=0o700)
    (out / "bin").mkdir()
    binary = out / "bin/sung"
    # Test a fixed artifact even if another build replaces the original executable.
    shutil.copy2(source, binary)
    env = {k: v for k, v in os.environ.items() if not k.startswith("SUNG_")}
    for key in ("DISPLAY", "WAYLAND_DISPLAY", "HYPRLAND_INSTANCE_SIGNATURE"):
        env.pop(key, None)
    env.update(QT_QPA_PLATFORM="offscreen", QT_QPA_PLATFORMTHEME="generic",
               QT_QUICK_BACKEND="software", QSG_RENDER_LOOP="basic",
               QT_FORCE_STDERR_LOGGING="1", QT_SCALE_FACTOR="1",
               QT_FFMPEG_DECODING_HW_DEVICE_TYPES=",", QT_FFMPEG_ENCODING_HW_DEVICE_TYPES=",",
               SUNG_HELPER=str(root / "tests/catalog_fixture.py"), SUNG_PYTHON=sys.executable)
    if shutil.which("fc-match"):
        font = subprocess.check_output(["fc-match", "Google Sans Flex", "-f", "%{file}"], text=True).strip()
        config = out / "fonts.conf"
        config.write_text('<fontconfig><include>/etc/fonts/fonts.conf</include><dir>'
                          + html.escape(str(Path(font).parent)) + '</dir></fontconfig>')
        env["FONTCONFIG_FILE"] = str(config)

    stages = [
        ("happy-path", "--immersive-polish-test", "happy-path", "1", None),
        ("edge-cases", "--immersive-edges-test", "edge-cases", "1", None),
        ("edge-cases-hidpi", "--immersive-edges-test", "edge-cases-hidpi", "1.6", None),
        ("preferences-seed", "--immersive-preferences-test", "preferences", "1", "seed"),
        ("preferences-restore", "--immersive-preferences-test", "preferences", "1", "restore"),
        ("preferences-reset", "--immersive-preferences-test", "preferences", "1", "reset"),
    ]
    results = []
    for name, flag, profile, scale, phase in stages:
        print(f"Running {name}", flush=True)
        e = env.copy()
        for key in ("CONFIG", "DATA", "CACHE", "STATE"):
            e[f"XDG_{key}_HOME"] = str(out / "profiles" / profile / key.lower())
        artifacts = out / name
        artifacts.mkdir()
        e.update(SUNG_TEST_OUTPUT=str(artifacts), QT_SCALE_FACTOR=scale)
        if phase:
            e["SUNG_PREFERENCE_PHASE"] = phase
        log_path = out / f"{name}.log"
        start = time.monotonic()
        error = None
        with log_path.open("w") as log:
            try:
                proc = subprocess.Popen([str(binary), "--isolated", flag], cwd=root, env=e,
                                        stdout=log, stderr=subprocess.STDOUT, start_new_session=True)
                try:
                    code = proc.wait(timeout=120)
                except subprocess.TimeoutExpired:
                    error = "Test exceeded 120 seconds"
                    os.killpg(proc.pid, signal.SIGTERM)
                    try:
                        proc.wait(timeout=5)
                    except subprocess.TimeoutExpired:
                        os.killpg(proc.pid, signal.SIGKILL)
                        proc.wait()
                    code = proc.returncode
            except OSError as exc:
                error, code = str(exc), -1
        text = log_path.read_text(errors="replace")
        problems = re.findall(r"(?:^FAIL|ReferenceError|TypeError|Binding loop|Unable to assign|Cannot assign)[^\n]*", text, re.M)
        markers = dict(re.findall(r"^FEATURE (\S+) (PASS|FAIL)$", text, re.M))
        if flag == "--immersive-edges-test":
            problems += [f"Missing or failed feature: {key}" for key in FEATURES if markers.get(key) != "PASS"]
        if "RESULT 0 failures" not in text:
            problems.append("Missing successful completion marker")
        if error:
            problems.append(error)
        passed = code == 0 and not problems
        result = dict(stage=name, status="pass" if passed else "fail", exit_code=code,
                      checks=len(re.findall(r"^PASS ", text, re.M)),
                      seconds=round(time.monotonic() - start, 2), problems=problems,
                      features=markers, log=log_path.name)
        results.append(result)
        print(f"{'PASS' if passed else 'FAIL'} {name}: {result['checks']} checks ({result['seconds']}s)", flush=True)
    report = dict(binary_sha256=hashlib.sha256(binary.read_bytes()).hexdigest(), stages=results,
                  scope="Offscreen software rendering, silent local fixtures, fresh profiles; no live service or native GPU validation.")
    (out / "report.json").write_text(json.dumps(report, indent=2) + "\n")
    table = "".join(f'<tr><td>{html.escape(r["stage"])}</td><td>{r["status"]}</td><td>{r["checks"]}</td>'
                    f'<td>{r["seconds"]}s</td><td><a href="{r["log"]}">Log</a></td></tr>' for r in results)
    features = "".join(f"<li>{html.escape(label)}</li>" for label in FEATURES.values())
    (out / "report.html").write_text('<!doctype html><html lang="en"><meta charset="utf-8"><title>Immersive regression tests</title>'
        '<style>body{font:16px system-ui;max-width:950px;margin:48px auto;padding:0 24px;background:#11191d;color:#dce6ea}'
        'table{border-collapse:collapse;width:100%}td,th{text-align:left;padding:12px;border-bottom:1px solid #40535c}a{color:#88cee7}</style>'
        '<h1>Immersive regression tests</h1><p>' + html.escape(report["scope"]) + '</p><ul>' + features + '</ul>'
        '<table><tr><th>Stage</th><th>Result</th><th>Checks</th><th>Time</th><th>Evidence</th></tr>' + table + '</table></html>')
    return 0 if all(r["status"] == "pass" for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
