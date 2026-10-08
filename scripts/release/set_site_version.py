#!/usr/bin/env python3
"""Set the version in site/public/config.js (the one `version: "x.y.z",` line).

  python3 scripts/release/set_site_version.py 0.3.3 [site/public/config.js]

Fails unless exactly one version line is there. Prints "unchanged" when it already says that version.
"""

import re
import sys

if len(sys.argv) not in (2, 3) or not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", sys.argv[1]):
    print(__doc__)
    sys.exit(2)
version = sys.argv[1]
path = sys.argv[2] if len(sys.argv) == 3 else "site/public/config.js"
with open(path, encoding="utf-8") as f:
    text = f.read()
line = re.compile(r'^(\s*version:\s*")([^"]*)(",)', re.M)
found = line.findall(text)
if len(found) != 1:
    sys.exit("expected one version line in %s, found %d" % (path, len(found)))
if found[0][1] == version:
    print("unchanged: " + version)
    sys.exit(0)
with open(path, "w", encoding="utf-8") as f:
    f.write(line.sub(lambda m: m.group(1) + version + m.group(3), text))
print("%s -> %s" % (found[0][1], version))
