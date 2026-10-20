"""Exercise the public production method without installing the log handler."""
import pathlib
import re
import sys

source = pathlib.Path(sys.argv[1]).read_text(encoding="utf-8")
methods = re.findall(
    r"^void Log::CensorAuthTokens\(QString& msg\)\s*\n\{.*?^\}", source, re.M | re.S
)
if len(methods) != 1:
    raise SystemExit("Expected exactly one production CensorAuthTokens method")
pathlib.Path(sys.argv[2]).write_text(methods[0] + "\n", encoding="utf-8", newline="\n")
