"""Exercise actual native stream-selection methods with a deterministic mpv seam."""
import pathlib
import re
import sys
source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
methods = []
for signature in (r"QVariantList PlayerComponent::findStreamsForURL\(const QString &url\)", r"void PlayerComponent::reselectStream\(const QString &streamSelection, MediaType target\)"):
    found = re.findall(r"^" + signature + r"\s*\n\{.*?^\}", source, re.M | re.S)
    if len(found) != 1:
        raise SystemExit("Expected one production stream method")
    methods += found
if source.count("m_externalSubtitleRequest.nextFile()") != 3:
    raise SystemExit("START_FILE, END_FILE and stop must invalidate subtitle additions")
pathlib.Path(sys.argv[2]).write_text("\n\n".join(methods) + "\n", encoding="utf-8", newline="\n")
