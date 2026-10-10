#!/bin/sh
# Move any auto-delivered update packages out of the stock flash path.
# The only way a package may reach /storage/update/update.zip is our
# deliberate, reviewed placement.
Q=/storage/apps/data/ota-quarantine
mkdir -p "$Q"
for f in /storage/mtp/update.zip /storage/update/update.zip /storage/mtp/update_app.tar.gz; do
    if [ -e "$f" ]; then
        mv -f "$f" "$Q/$(basename "$f").$(date +%s 2>/dev/null || echo $$)"
    fi
done
exit 0
