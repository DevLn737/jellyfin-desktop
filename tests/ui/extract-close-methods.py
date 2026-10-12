"""Compile the actual close methods without starting a window or native components."""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
connection = re.findall(
    r"connect\(qApp,\s*&QCoreApplication::aboutToQuit,\s*this,\s*&KonvergoWindow::(\w+)\);",
    source,
)
if connection != ["saveStateBeforeQuit"]:
    raise SystemExit("aboutToQuit must invoke the save-only production method exactly once")
methods = []
for name in ("closingWindow", "saveStateBeforeQuit"):
    matches = re.findall(
        rf"^void KonvergoWindow::{name}\(\)\s*\n\{{.*?^\}}", source, re.M | re.S
    )
    if len(matches) != 1:
        raise SystemExit(f"Expected exactly one production method: {name}")
    methods.append(matches[0])
pathlib.Path(sys.argv[2]).write_text("\n\n".join(methods) + "\n", encoding="utf-8", newline="\n")
