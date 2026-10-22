"""Extract fullscreen transition methods for the headless Qt Core regression."""
import pathlib
import re
import sys
source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
methods = []
for name in ("updateForcedScreen", "updateWindowState", "onScreenAdded", "loadGeometry"):
    matches = re.findall(rf"^(?:void|QRect) KonvergoWindow::{name}\([^\n]*\)\s*\n\{{.*?^\}}", source, re.M | re.S)
    if len(matches) != 1:
        raise SystemExit(f"Expected one production method: {name}")
    methods.append(matches[0])
pathlib.Path(sys.argv[2]).write_text("\n\n".join(methods) + "\n", encoding="utf-8")
