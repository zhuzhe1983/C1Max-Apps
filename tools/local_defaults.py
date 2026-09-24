"""Validate private device defaults; never put them in a release archive."""
import json
from urllib.parse import urlsplit


def load_defaults(path):
    raw = path.read_bytes()
    if len(raw) > 16384:
        raise ValueError("Default server configuration exceeds 16 KiB")
    try:
        value = json.loads(raw)
    except (ValueError, UnicodeError):
        raise ValueError("Invalid default server JSON") from None
    if not isinstance(value, dict) or value.get("schema") != 1:
        raise ValueError("Default server configuration needs schema 1")
    if set(value) - {"schema", "streamplayer", "crosspoint"}:
        raise ValueError("Unexpected default server section")
    fields = {"streamplayer": {"base", "username", "password", "type"},
              "crosspoint": {"url", "name", "user", "password"}}
    for app, allowed in fields.items():
        section = value.get(app, {})
        if not isinstance(section, dict) or set(section) - allowed:
            raise ValueError("Unexpected fields in " + app + " defaults")
        for text in section.values():
            if not isinstance(text, str) or len(text.encode("utf-8")) > 2048 or any(c in text for c in "\x00\r\n"):
                raise ValueError("Invalid value in " + app + " defaults")
        url = section.get("base" if app == "streamplayer" else "url", "")
        if url:
            try:
                parsed = urlsplit(url)
                valid = parsed.scheme in {"http", "https"} and parsed.hostname and not parsed.username and not parsed.password
                valid = valid and not any(c.isspace() for c in url) and not parsed.fragment
                if app == "streamplayer": valid = valid and not parsed.query
                parsed.port
            except ValueError:
                valid = False
            if not valid: raise ValueError("Invalid server URL in " + app + " defaults")
        if app == "streamplayer" and section.get("type", "Emby") not in {"Emby", "Jellyfin"}:
            raise ValueError("StreamPlayer type must be Emby or Jellyfin")
    return value
