#!/usr/bin/env python3
"""Check that a round's advisor-gate record is complete and honest about its order.

What this proves, and what it does not:

  proves   the record names three gates (method / data / conclusion), each with a
           verdict; that the verdicts that must pass did pass; that the
           pre-registration block was frozen *before* the results (the recorded
           hash still matches the plan file's block, so a criterion edited after
           the fact is caught); and that every question/answer file the record
           cites exists with the sha256 the record claims.
  does not prove the advisor was right, or that the round's numbers are true. It
           is an audit of process, not of physics.

Usage:
    check-codex-gate.py runs/<date>-<topic>/CODEX-GATES.md [--repo <path>]
    check-codex-gate.py --selftest

Exit 0 = the record is complete, 1 = a finding, 2 = the input could not be read.

Deliberately NOT wired into `make gates` or in front of `run-gate2.sh`: Gate1/Gate2
are regression gates and the board never waits on an advisor (AGENTS.md, board-first).
This runs at wrap-up and at merge time.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import tempfile
from pathlib import Path

GATES = ("G-A", "G-B", "G-C")
VERDICT_RE = re.compile(r"^-\s*(?:\*\*)?判词(?:\*\*)?[：:]\s*(.+?)\s*$", re.MULTILINE)
FROZEN_RE = re.compile(r"`?GATE_A_FROZEN\s+([0-9a-f]{64})`?")
FILE_RE = re.compile(
    r"^-\s*(?:\*\*)?(问题|答案)(?:\*\*)?[：:]\s*`([^`]+)`\s*sha256=`([0-9a-f]{64})`",
    re.MULTILINE)
PLAN_RE = re.compile(
    r"^-\s*(?:\*\*)?方案（冻结对象）(?:\*\*)?[：:]\s*`([^`]+)`", re.MULTILINE)
PREREG_HEAD_RE = re.compile(r"^##\s.*预注册", re.MULTILINE)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def prereg_block(plan: Path) -> str | None:
    """The pre-registration section: from its heading to the next top-level heading.

    This is the object the gate freezes. It is a section rather than the whole file
    because a ledger legitimately grows after execution (results, wrap-up), while the
    criteria must not.
    """
    text = plan.read_text(encoding="utf-8", errors="replace")
    m = PREREG_HEAD_RE.search(text)
    if not m:
        return None
    start = m.start()
    nxt = re.search(r"^##\s", text[m.end():], re.MULTILINE)
    end = m.end() + nxt.start() if nxt else len(text)
    return text[start:end]


def sections(text: str) -> dict[str, str]:
    out: dict[str, str] = {}
    parts = re.split(r"^##\s+(.*)$", text, flags=re.MULTILINE)
    for i in range(1, len(parts) - 1, 2):
        out[parts[i].strip()] = parts[i + 1]
    return out


def check(record: Path, repo: Path) -> tuple[list[str], list[str]]:
    """Return (malformed, not_approved).

    The two are different answers.  "Malformed" means the record cannot be
    trusted as evidence of anything: a gate is missing, a cited file is absent or
    its hash moved, the pre-registration was edited after it was frozen.
    "Not approved" means the record is complete and honest and says the advisor
    did not accept this round - a legitimate outcome (the round must not run, or
    must not claim direction), not a defect in the record.
    """
    malformed: list[str] = []
    not_approved: list[str] = []
    if not record.is_file():
        return ["record missing: %s" % record], []
    text = record.read_text(encoding="utf-8", errors="replace")
    secs = sections(text)

    found = {}
    for name, body in secs.items():
        for gate in GATES:
            if name.startswith(gate):
                found[gate] = body
    for gate in GATES:
        if gate not in found:
            malformed.append("gate section %s missing" % gate)
    if malformed:
        return malformed, not_approved

    # every gate carries a verdict; G-A and G-B must be a passing verdict
    verdicts = {}
    for gate in GATES:
        m = VERDICT_RE.search(found[gate])
        if not m:
            malformed.append("%s: no 判词 line" % gate)
            continue
        verdicts[gate] = m.group(1)
    if "G-A" in verdicts and not verdicts["G-A"].startswith("ACCEPTED"):
        not_approved.append("G-A: the method was not registered as accepted (%s)" % verdicts["G-A"])
    if "G-B" in verdicts and not verdicts["G-B"].startswith("ACCEPTED"):
        if verdicts["G-B"].startswith(("未跑", "SKIPPED")):
            not_approved.append("G-B: not run (%s)" % verdicts["G-B"])
        else:
            not_approved.append("G-B: the data was not accepted (%s)" % verdicts["G-B"])

    # the frozen pre-registration block must still be the one that was approved
    m = PLAN_RE.search(text)
    if not m:
        malformed.append("no 方案（冻结对象） line")
    else:
        plan = (repo / m.group(1)).resolve()
        if not plan.is_file():
            malformed.append("plan file missing: %s" % m.group(1))
        else:
            block = prereg_block(plan)
            if block is None:
                malformed.append("plan has no 预注册 section to freeze: %s" % m.group(1))
            else:
                want = hashlib.sha256(block.encode("utf-8")).hexdigest()
                fz = FROZEN_RE.search(text)
                if not fz:
                    malformed.append("no GATE_A_FROZEN line")
                elif fz.group(1) != want:
                    malformed.append(
                        "GATE_A_FROZEN does not match the plan's pre-registration block "
                        "(recorded %s, current %s) - a criterion was edited after it was frozen"
                        % (fz.group(1), want)
                    )

    # every cited question/answer must exist with the hash the record claims
    for gate in GATES:
        for kind, path_s, claimed in FILE_RE.findall(found[gate]):
            p = (repo / path_s).resolve()
            if not p.is_file():
                malformed.append("%s: %s file missing: %s" % (gate, kind, path_s))
                continue
            actual = sha256_file(p)
            if actual != claimed:
                malformed.append(
                    "%s: %s %s hash mismatch (recorded %s, actual %s)"
                    % (gate, kind, path_s, claimed[:12], actual[:12])
                )
    return malformed, not_approved


SELFTEST_PLAN = """# RUN: selftest

## 0 预注册（跑前写死）

- 靶子：把 X 从 1 提到 2。
- 判据：≥+15% 正；≤+5% 负。

## 1 结果

- 实测 1.5。
"""

SELFTEST_RECORD = """# CODEX-GATES: selftest

- **轮次目录**：`runs/selftest/`
- **方案（冻结对象）**：`{plan}`
- **轮数**：1
- **最终判词**：ACCEPTED

## G-A 方法备案（执行前）

- 时间：now
- 通道：codex-0:0.0
- 模型 / 档位：gpt-6.1-sol / high
- 问题：`{q}` sha256=`{qh}`
- 答案：`{a}` sha256=`{ah}`
- 判词：ACCEPTED
- 判词原文：

  > ACCEPTED - the criteria separate the two live hypotheses.

- 证伪条件：below +5%.
- 这份证据不能得出的结论：anything about the uplink.
- 我方据此改动：none.
- `GATE_A_FROZEN {frozen}`

## G-B 数据验收（第 1 轮，下结论前）

- 时间：now
- 通道：codex-0:0.0
- 问题：`{q}` sha256=`{qh}`
- 答案：`{a}` sha256=`{ah}`
- 判词：ACCEPTED（数据可用）
- 判词原文：

  > ACCEPTED - the numbers are internally consistent.

- 它点名的矛盾/缺口：none.
- 我方据此改动：none.

## G-C 结论验收（第 1 轮）

- 时间：now
- 通道：codex-0:0.0
- 问题：`{q}` sha256=`{qh}`
- 答案：`{a}` sha256=`{ah}`
- 判词：ACCEPTED
- 判词原文：

  > ACCEPTED - the claim follows from the registered criterion.

- 它判为「数字支撑不了」的主张：none.
- 它指的下一刀：measure the uplink.
- 我方据此改动：none.
"""


def selftest() -> int:
    tmp = Path(tempfile.mkdtemp(prefix="codex-gate-"))
    plan = tmp / "RUN.md"
    plan.write_text(SELFTEST_PLAN, encoding="utf-8")
    q = tmp / "q.txt"
    q.write_text("question\n", encoding="utf-8")
    a = tmp / "a.md"
    a.write_text("answer\nWROTE %s\n" % a, encoding="utf-8")
    block = prereg_block(plan)
    assert block is not None
    frozen = hashlib.sha256(block.encode("utf-8")).hexdigest()
    fields = dict(plan=plan.name, q=q.name, a=a.name,
                  qh=sha256_file(q), ah=sha256_file(a), frozen=frozen)
    record = tmp / "CODEX-GATES.md"
    record.write_text(SELFTEST_RECORD.format(**fields), encoding="utf-8")

    failures = 0
    errs, notok = check(record, tmp)
    if errs or notok:
        print("selftest: base record judged FAIL, expected pass: %s" % (errs or notok)[0])
        failures += 1
    else:
        print("selftest: base record judged pass")

    def mutate(name: str, text: str, want: str) -> None:
        nonlocal failures
        p = tmp / ("m-" + name.replace(" ", "-") + ".md")
        p.write_text(text, encoding="utf-8")
        errs2, notok2 = check(p, tmp)
        got = errs2 + notok2
        ok = any(want in e for e in got)
        print("selftest: %-34s %s" % (name, "ok" if ok else "MISMATCH got=%s" % got[:1]))
        if not ok:
            failures += 1

    base = record.read_text(encoding="utf-8")
    mutate("frozen hash wrong", base.replace(frozen, "0" * 64), "does not match")
    # a plan whose pre-registration block changed after the freeze
    plan.write_text(SELFTEST_PLAN.replace("≥+15% 正", "≥+5% 正"), encoding="utf-8")
    mutate("prereg edited after the fact", base, "does not match")
    plan.write_text(SELFTEST_PLAN, encoding="utf-8")
    mutate("G-B missing", re.sub(r"## G-B.*?(?=## G-C)", "", base, flags=re.S), "G-B missing")
    mutate("G-A rejected", base.replace("- 判词：ACCEPTED\n- 判词原文：\n\n  > ACCEPTED - the criteria",
                                        "- 判词：REJECTED\n- 判词原文：\n\n  > ACCEPTED - the criteria"),
           "not registered as accepted")
    mutate("answer hash wrong", base.replace(fields["ah"], "1" * 64), "hash mismatch")
    mutate("answer file missing", base.replace(fields["a"], "nope.md"), "file missing")

    print("codex-gate-selftest: %s" % ("PASS" if failures == 0 else "FAIL (%d)" % failures))
    return 0 if failures == 0 else 1


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("record", nargs="?", help="runs/<date>-<topic>/CODEX-GATES.md")
    ap.add_argument("--repo", default=".", help="workspace root the record's paths are relative to")
    ap.add_argument("--selftest", action="store_true")
    args = ap.parse_args(argv)

    if args.selftest:
        return selftest()
    if not args.record:
        ap.error("give a record path or --selftest")

    record = Path(args.record)
    repo = Path(args.repo).resolve()
    try:
        malformed, not_approved = check(record, repo)
    except OSError as exc:
        print("check-codex-gate: cannot read: %s" % exc)
        return 2

    if malformed:
        print("check-codex-gate: FAIL (the record cannot be trusted)")
        for e in malformed:
            print("  - %s" % e)
        return 1
    if not_approved:
        print("check-codex-gate: NOT APPROVED (the record is complete and honest; "
              "the advisor did not accept this round)")
        for e in not_approved:
            print("  - %s" % e)
        return 3
    print("check-codex-gate: PASS (three gates, verdicts in place, pre-registration frozen, "
          "cited files match their hashes)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
