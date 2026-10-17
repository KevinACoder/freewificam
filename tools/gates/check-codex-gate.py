#!/usr/bin/env python3
"""Check that a round's advisor record shows the advisor was actually answered.

This is a *soft* constraint's checker. The advisor is a standing reviewer, not a
gate: it does not have to approve anything. What the record must prove is that
it was consulted at each of the three nodes (method before the work, data before
the conclusion, conclusion before the write-up), that its answer is on file with
the hash the record claims, that every node carries a disposition, and that any
"not taken" carries a reason. A round may overrule the advisor - it may not
silently skip it.

It also checks one fact the eye cannot see: the pre-registration block is still
the one that was frozen before the results (`PLAN_FROZEN`), so a criterion edited
after the fact is caught.

Usage:
    check-codex-gate.py runs/<date>-<topic>/CODEX-GATES.md [--repo <path>]
    check-codex-gate.py --selftest

Exit 0 = complete, 1 = a finding, 2 = the input could not be read.

Deliberately not wired into `make gates` or in front of `run-gate2.sh`: Gate1/Gate2
are regression gates, and the board never waits on an advisor. This runs at
wrap-up and at merge time.
"""

from __future__ import annotations

import argparse
import hashlib
import re
import sys
import tempfile
from pathlib import Path

NODES = ("一", "二", "三")
NODE_NAMES = {"一": "方法", "二": "数据", "三": "结论"}
DISPOSITIONS = ("采纳", "部分采纳", "不采纳", "未走")

DISP_RE = re.compile(r"^-\s*(?:\*\*)?处置(?:\*\*)?[：:]\s*(\S+)", re.MULTILINE)
REASON_RE = re.compile(r"^-\s*(?:\*\*)?理由与改动(?:\*\*)?[：:]\s*(.*)$", re.MULTILINE)
FROZEN_RE = re.compile(r"`?PLAN_FROZEN\s+([0-9a-f]{64})`?")
FILE_RE = re.compile(
    r"^-\s*(?:\*\*)?(问题|答案)(?:\*\*)?[：:]\s*`([^`]+)`\s*sha256=`([0-9a-f]{64})`",
    re.MULTILINE)
PLAN_RE = re.compile(
    r"^-\s*(?:\*\*)?方案（冻结对象）(?:\*\*)?[：:]\s*`([^`]+)`", re.MULTILINE)
METHOD_ONLY_RE = re.compile(r"^-\s*(?:\*\*)?只审方法(?:\*\*)?[：:]\s*(是|否)", re.MULTILINE)
PREREG_HEAD_RE = re.compile(r"^##\s.*预注册", re.MULTILINE)


def sha256_file(path: Path) -> str:
    h = hashlib.sha256()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def prereg_block(plan: Path) -> str | None:
    """The pre-registration section: its heading to the next top-level heading.

    A section rather than the whole file, because a ledger legitimately grows
    after execution while the criteria must not.
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


def check(record: Path, repo: Path) -> list[str]:
    errors: list[str] = []
    if not record.is_file():
        return ["record missing: %s" % record]
    text = record.read_text(encoding="utf-8", errors="replace")
    secs = sections(text)

    found: dict[str, str] = {}
    for name, body in secs.items():
        for node in NODES:
            if "参谋审查" + node in name:
                found[node] = body
    for node in NODES:
        if node not in found:
            errors.append("node %s (%s) missing" % (node, NODE_NAMES[node]))
    if errors:
        return errors

    m_only = METHOD_ONLY_RE.search(text)
    method_only = m_only is not None and m_only.group(1) == "是"

    for node in NODES:
        body = found[node]
        label = "%s（%s）" % (node, NODE_NAMES[node])

        disp = DISP_RE.search(body)
        if not disp:
            errors.append("%s: no 处置 line" % label)
            continue
        verdict = disp.group(1)
        if not any(verdict.startswith(d) for d in DISPOSITIONS):
            errors.append("%s: 处置 %r is not one of %s"
                          % (label, verdict, "/".join(DISPOSITIONS)))
            continue

        if verdict.startswith("未走"):
            # only legal where the record declares a method-only run, and only
            # for the nodes after the method
            if not method_only or node == "一":
                errors.append("%s: 未走, but the record does not declare a method-only run"
                              % label)
            continue

        reason = REASON_RE.search(body)
        if not reason or reason.group(1).strip() == "":
            errors.append("%s: 处置 %s with no 理由与改动" % (label, verdict))

        files = FILE_RE.findall(body)
        if len(files) < 2:
            errors.append("%s: needs both a 问题 and an 答案 line with their sha256" % label)
        for kind, path_s, claimed in files:
            p = (repo / path_s).resolve()
            if not p.is_file():
                errors.append("%s: %s file missing: %s" % (label, kind, path_s))
                continue
            actual = sha256_file(p)
            if actual != claimed:
                errors.append("%s: %s %s hash mismatch (recorded %s, actual %s)"
                              % (label, kind, path_s, claimed[:12], actual[:12]))

    # the frozen pre-registration block must still be the one that was approved
    m = PLAN_RE.search(text)
    if not m:
        errors.append("no 方案（冻结对象） line")
    else:
        plan = (repo / m.group(1)).resolve()
        if not plan.is_file():
            errors.append("plan file missing: %s" % m.group(1))
        else:
            block = prereg_block(plan)
            if block is None:
                errors.append("plan has no 预注册 section to freeze: %s" % m.group(1))
            else:
                want = hashlib.sha256(block.encode("utf-8")).hexdigest()
                fz = FROZEN_RE.search(text)
                if not fz:
                    errors.append("no PLAN_FROZEN line")
                elif fz.group(1) != want:
                    errors.append(
                        "PLAN_FROZEN does not match the plan's pre-registration block "
                        "(recorded %s, current %s) - a criterion was edited after it was frozen"
                        % (fz.group(1), want)
                    )
    return errors


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
- **只审方法**：否

## 参谋审查一：方法（动手前）

- 时间：now
- 通道：codex-0:0.0
- 问题：`{q}` sha256=`{qh}`
- 答案：`{a}` sha256=`{ah}`
- 参谋指出的问题：

  > the target does not reconcile with the measured 1.5.

- 它说这份证据不能得出什么：anything about the uplink.
- 处置：部分采纳
- 理由与改动：改了靶子的分母，驳回「补测上行」——本轮不测上行，代价记在 notCovered。
- `PLAN_FROZEN {frozen}`

## 参谋审查二：数据（下结论前）

- 时间：now
- 通道：codex-0:0.0
- 问题：`{q}` sha256=`{qh}`
- 答案：`{a}` sha256=`{ah}`
- 参谋指出的问题：

  > the window is unbounded.

- 它说这份证据不能得出什么：steady-state rate.
- 处置：采纳
- 理由与改动：把窗口改成 20 s 有界。

## 参谋审查三：结论（定稿前）

- 时间：now
- 通道：codex-0:0.0
- 问题：`{q}` sha256=`{qh}`
- 答案：`{a}` sha256=`{ah}`
- 参谋指出的问题：

  > the claim states causation from a correlation.

- 它说这份证据不能得出什么：that the layer is the wall.
- 处置：不采纳
- 理由与改动：本轮只报「效果」，不报「机制」；结论文字已改成「效果为正，机制未证」。
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
    errs = check(record, tmp)
    if errs:
        print("selftest: base record judged FAIL, expected pass: %s" % errs[0])
        failures += 1
    else:
        print("selftest: base record judged pass")

    def mutate(name: str, text: str, want: str) -> None:
        nonlocal failures
        p = tmp / ("m-" + name.replace(" ", "-") + ".md")
        p.write_text(text, encoding="utf-8")
        got = check(p, tmp)
        ok = any(want in e for e in got)
        print("selftest: %-36s %s" % (name, "ok" if ok else "MISMATCH got=%s" % got[:1]))
        if not ok:
            failures += 1

    base = record.read_text(encoding="utf-8")
    mutate("frozen hash wrong", base.replace(frozen, "0" * 64), "does not match")
    # a plan whose pre-registration block changed after the freeze
    plan.write_text(SELFTEST_PLAN.replace("≥+15% 正", "≥+5% 正"), encoding="utf-8")
    mutate("prereg edited after the fact", base, "does not match")
    plan.write_text(SELFTEST_PLAN, encoding="utf-8")
    mutate("node missing", re.sub(r"## 参谋审查二.*?(?=## 参谋审查三)", "", base, flags=re.S),
           "node 二")
    mutate("disposition missing", base.replace("- 处置：采纳\n", ""), "no 处置 line")
    mutate("disposition not a known value", base.replace("- 处置：采纳", "- 处置：看情况"),
           "is not one of")
    mutate("reason empty", base.replace(
        "- 理由与改动：把窗口改成 20 s 有界。", "- 理由与改动："), "no 理由与改动")
    mutate("answer hash wrong", base.replace(fields["ah"], "1" * 64), "hash mismatch")
    mutate("answer file missing", base.replace(fields["a"], "nope.md"), "file missing")
    mutate("未走 without method-only", base.replace("- 处置：采纳", "- 处置：未走"),
           "does not declare a method-only run")

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

    try:
        errors = check(Path(args.record), Path(args.repo).resolve())
    except OSError as exc:
        print("check-codex-gate: cannot read: %s" % exc)
        return 2

    if errors:
        print("check-codex-gate: FAIL (the record cannot be trusted)")
        for e in errors:
            print("  - %s" % e)
        return 1
    print("check-codex-gate: PASS (three nodes, advisor answers on file with matching "
          "hashes, every node answered with a disposition, pre-registration frozen)")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
