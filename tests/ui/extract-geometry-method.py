"""Extract the actual production first-launch/saved geometry selection method."""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
methods = re.findall(r"^QRect KonvergoWindow::loadGeometryRect\(\)\s*\n\{.*?^\}", source, re.M | re.S)
if len(methods) != 1:
    raise SystemExit("Expected one production loadGeometryRect method")
pathlib.Path(sys.argv[2]).write_text(methods[0] + "\n", encoding="utf-8", newline="\n")
