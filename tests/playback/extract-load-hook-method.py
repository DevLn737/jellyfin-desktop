"""Compile the production delayed on_load continuation without mpv or a window."""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
methods = re.findall(
    r"^void PlayerComponent::continueLoadHookAfterDelay\(int delayMilliseconds, std::function<void\(\)> continueHook\)\s*\n\{.*?^\}",
    source, re.M | re.S,
)
if len(methods) != 1:
    raise SystemExit("Expected exactly one production delayed load-hook method")
# Both supported mpv hook interfaces must use the guarded implementation.
if source.count("continueLoadHookAfterDelay(pause, continueHook);") != 2:
    raise SystemExit("Both delayed mpv hook paths must use the guarded continuation")
if source.count("continueLoadHookAfterDelay(0, continueHook);") != 2:
    raise SystemExit("Both immediate mpv hook paths must use the guarded continuation")
pathlib.Path(sys.argv[2]).write_text(methods[0] + "\n", encoding="utf-8", newline="\n")
