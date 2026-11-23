"""Run the headless engine suite and verify that the sandbox gate cases actually executed."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import uuid
import xml.etree.ElementTree as ET


def summarize(report):
    document = ET.parse(report)
    cases = document.findall("./TestSuite/TestCase")
    completed = [case for case in cases if case.find("OverallResultsAsserts") is not None]
    totals = document.find("./OverallResultsTestCases")
    assertions = document.find("./OverallResultsAsserts")
    if totals is None or assertions is None or not completed:
        raise ValueError("Missing completed doctest cases or run totals")
    if int(totals.attrib["failures"]) or int(assertions.attrib["failures"]):
        raise ValueError("Doctest recorded failures")
    skipped = [case.attrib["name"] for case in cases if case.get("skipped") == "true"]
    passed_assertions = sum(int(case.find("OverallResultsAsserts").attrib["successes"]) for case in completed)
    if (
        len(completed) != int(totals.attrib["successes"])
        or len(skipped) != int(totals.attrib["skipped"])
        or len(completed) + len(skipped) != len(cases)
        or passed_assertions == 0
        or passed_assertions != int(assertions.attrib["successes"])
    ):
        raise ValueError("Doctest summaries disagree with the completed cases")

    gates = {str(i): [] for i in range(1, 9)}
    for case in completed:
        match = re.search(r"\[Gate([1-8])\]", case.attrib["name"])
        if match:
            counts = case.find("OverallResultsAsserts").attrib
            if int(counts["successes"]) == 0 or int(counts["failures"]):
                raise ValueError(f"Missing passing assertions in {case.attrib['name']}")
            gates[match[1]].append({"case": case.attrib["name"], "assertions": int(counts["successes"])})
    missing = [key for key, entries in gates.items() if not entries]
    if missing:
        raise ValueError(f"Missing executed gate tests: {', '.join(missing)}")
    return {
        "passed": len(completed),
        "failed": int(totals.attrib["failures"]),
        "skipped": len(skipped),
        "skipped_cases": skipped,
        "assertions": passed_assertions,
        "gate_cases": gates,
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("editor", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[2]
    editor = args.editor.resolve(strict=True)
    run = args.output.resolve() / ("suite-" + uuid.uuid4().hex[:10])
    run.mkdir(parents=True)
    print(run, flush=True)
    env = os.environ.copy()
    for key in ("APPDATA", "LOCALAPPDATA", "TEMP", "TMP", "XDG_CACHE_HOME", "XDG_CONFIG_HOME", "XDG_DATA_HOME"):
        folder = run / key
        folder.mkdir()
        env[key] = str(folder)
    report = run / "doctest.xml"
    with (run / "engine.log").open("wb") as stream:
        subprocess.run(
            [str(editor), "--headless", "--test", "--reporters=xml", f"--out={report}"],
            cwd=root,
            env=env,
            stdout=stream,
            stderr=subprocess.STDOUT,
            check=True,
        )
    result = summarize(report)
    with editor.open("rb") as stream:
        result["editor_sha256"] = hashlib.file_digest(stream, "sha256").hexdigest()
    (run / "result.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"{result['passed']} passed, {result['failed']} failed, {result['skipped']} skipped, {result['assertions']} assertions")


if __name__ == "__main__":
    main()
