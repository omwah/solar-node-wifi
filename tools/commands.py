"""Command definitions shared by configure.py (serial) and remote.py (MQTT).

Mirrors the firmware's command set (lib/cli): the serial console takes a text line,
MQTT takes {"id", "cmd", "args"} with named arguments.
"""

import json
import shlex

# command -> names of its positional arguments, in order
COMMANDS = {
    "help": [],
    "status": [],
    "get": ["key"],
    "show": [],
    "set": ["key", "value"],
    "commit": [],
    "poll-now": [],
    "session": ["minutes"],
    "log": ["lines"],
    "ota": ["url", "sha256"],
    "reboot": [],
    "factory-reset": ["confirm"],
}


class UsageError(ValueError):
    pass


def check(cmd, args):
    if cmd not in COMMANDS:
        raise UsageError(f"unknown command {cmd!r}; one of: {', '.join(COMMANDS)}")
    names = COMMANDS[cmd]
    if len(args) > len(names):
        raise UsageError(f"{cmd} takes at most {len(names)} argument(s): {' '.join(names)}")


def to_line(cmd, args):
    """Serial console form, quoting arguments that need it."""
    check(cmd, args)
    parts = [cmd] + [_quote(a) for a in args]
    return " ".join(parts)


def to_json(cmd, args, request_id):
    """MQTT form with named arguments."""
    check(cmd, args)
    named = {}
    if cmd == "session" and args == ["end"]:
        named["end"] = True
    else:
        for name, value in zip(COMMANDS[cmd], args):
            named[name] = int(value) if name in ("minutes", "lines") else value
    message = {"id": request_id, "cmd": cmd}
    if named:
        message["args"] = named
    return json.dumps(message)


def parse_reply(line):
    """Returns the reply object for a JSON reply line, or None for log output."""
    line = line.strip()
    if not line.startswith("{"):
        return None
    try:
        obj = json.loads(line)
    except json.JSONDecodeError:
        return None
    return obj if isinstance(obj, dict) and "ok" in obj else None


def _quote(arg):
    if arg == "" or any(c.isspace() or c in '"\\' for c in arg):
        return '"' + arg.replace("\\", "\\\\").replace('"', '\\"') + '"'
    return arg


def split(text):
    """Splits a typed shell line the way the firmware does."""
    return shlex.split(text)
