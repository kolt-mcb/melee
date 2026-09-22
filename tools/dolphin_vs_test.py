#!/usr/bin/env python3
"""End-to-end test of the Dolphin netplay route. Stops at the first mismatch.

Runs the whole route from nothing -- host up, port joined, game started, both
traces captured -- and watches the two traces as they are written. The moment
a match frame disagrees it stops the session and fails, naming the frame and
the fields.

Stopping there is the point. An aggregate count answers "how much of this run
disagreed", which is the wrong question while the run is wrong at all: every
frame after the first bad one is downstream of a simulation that had already
parted, so the only frame that can be reasoned about is the first.

  tools/dolphin_vs_test.py [--cpu 9] [--frames N] [--strict]
"""
import argparse, os, re, signal, subprocess, sys, time

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
OUT = os.path.join(ROOT, "build", "dolphin-vs-test")

# p_aihash hashes bytes the console leaves uninitialised (the DI stick pair --
# see the melee-pc-aihash-causes notes), so it is garbage on both sides by
# design and never agrees. --strict compares it anyway.
DEFAULT_IGNORE = "p_aihash"

results = []


def check(name, ok, detail=""):
    results.append((name, bool(ok), detail))
    print("%-4s %-44s %s" % ("PASS" if ok else "FAIL", name, detail),
          flush=True)
    return ok


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--frames", type=int, default=1200,
                    help="match frames to require before calling it a pass")
    ap.add_argument("--cpu", metavar="LVL", type=int, default=0,
                    help="both slots as CPUs of this level")
    ap.add_argument("--strict", action="store_true",
                    help="compare every field, p_aihash included")
    ap.add_argument("--timeout", type=int, default=240)
    a = ap.parse_args()

    os.makedirs(OUT, exist_ok=True)
    ref = os.path.join(OUT, "ref.trace")
    port = os.path.join(OUT, "port.trace")
    log = os.path.join(OUT, "run.log")
    for f in (ref, port, log):
        if os.path.exists(f):
            os.unlink(f)

    env = dict(os.environ, MELEE_REF_TRACE=ref, MELEE_TRACE=port,
               MELEE_TRACE_AT_PC="8037E23C")
    if a.cpu:
        # MELEE_FORCE_CPU, never MELEE_BOOT_CPU: the latter makes the slots
        # CPUs during the load, which changes the entry sequence, and the two
        # loads are different lengths. Both builds apply MELEE_FORCE_CPU on
        # the same match frames on purpose.
        env["MELEE_FORCE_CPU"] = "%d,%d" % (a.cpu, a.cpu)
    ignore = "" if a.strict else DEFAULT_IGNORE

    print("running the route%s, watching for the first mismatch..."
          % (" with level-%d CPUs" % a.cpu if a.cpu else ""), flush=True)
    fh = open(log, "w")
    run = subprocess.Popen([sys.executable, os.path.join(HERE, "dolphin_vs.py"),
                            "--seconds", str(a.timeout)],
                           env=env, stdout=fh, stderr=subprocess.STDOUT,
                           preexec_fn=os.setsid)

    verdict, reached = None, 0
    deadline = time.time() + a.timeout
    try:
        while time.time() < deadline:
            time.sleep(4)
            if not (os.path.exists(ref) and os.path.exists(port)):
                continue
            r = subprocess.run(
                [sys.executable, os.path.join(HERE, "trace_diff.py"),
                 ref, port, "--first"] + (["--ignore", ignore] if ignore else []),
                capture_output=True, text=True)
            if r.returncode == 1 and "MISMATCH" in r.stdout:
                verdict = r.stdout.strip()
                break
            m = re.search(r"identical over (\d+) match frames", r.stdout)
            if m:
                reached = int(m.group(1))
                if reached >= a.frames:
                    verdict = r.stdout.strip()
                    break
            if run.poll() is not None:
                break
    finally:
        try:
            os.killpg(os.getpgid(run.pid), signal.SIGTERM)
        except Exception:
            pass
        subprocess.run(["pkill", "-x", "melee-pc"], check=False)
        fh.close()

    text = open(log, errors="replace").read()
    check("host came up and hosted", "READY" in text)
    m = re.search(r"\[NETDOL\] joined as player (\d+)", text)
    check("port joined the session", bool(m),
          "as player %s" % m.group(1) if m else "no join line")
    check("host gave it a controller port", "(ours)" in text)
    check("the host started the game",
          "[NETDOL] the host started the game" in text)
    m = re.search(r"rendezvous (-?\d+) reached at port frame (\d+)", text)
    check("both sides met at the rendezvous", bool(m),
          m.group(0) if m else "no rendezvous line")

    if verdict is None:
        check("the two simulate the same match", False,
              "no verdict: the session produced no comparable frames")
    elif verdict.startswith("MISMATCH"):
        first = re.search(r"match frame (\d+)", verdict)
        check("the two simulate the same match", False,
              "diverged at match frame %s" % (first.group(1) if first else "?"))
        print("\n" + verdict, flush=True)
    else:
        check("the two simulate the same match", True, verdict)

    bad = [n for n, ok, _ in results if not ok]
    print("\n%d checks, %d passed, %d failed"
          % (len(results), len(results) - len(bad), len(bad)), flush=True)
    if ignore:
        print("ignored: %s (uninitialised on the console by design; "
              "--strict compares it)" % ignore, flush=True)
    return 1 if bad else 0


sys.exit(main())
