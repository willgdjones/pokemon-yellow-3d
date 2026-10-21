"""Small helpers for reading the pret/pokeyellow RGBDS sources as data (no ROM build)."""
import os
import re

IDENT = r"[A-Za-z_][\w@#]*"


def strip_comment(line):
    out = []
    quote = False
    for ch in line:
        if ch == '"':
            quote = not quote
        if ch == ";" and not quote:
            break
        out.append(ch)
    return "".join(out).rstrip()


def read_lines(path):
    with open(path, encoding="utf-8") as f:
        return [strip_comment(l.rstrip("\n")) for l in f]


def split_args(text):
    """Split macro arguments on commas outside quotes and parentheses."""
    args, cur, quote, depth = [], [], False, 0
    for ch in text:
        if ch == '"':
            quote = not quote
        if not quote:
            if ch == "(":
                depth += 1
            elif ch == ")":
                depth -= 1
            elif ch == "," and depth == 0:
                args.append("".join(cur).strip())
                cur = []
                continue
        cur.append(ch)
    if "".join(cur).strip():
        args.append("".join(cur).strip())
    return args


def macro(line):
    """'\tfoo a, b' -> ('foo', ['a', 'b']); labels and blank lines -> (None, [])."""
    if not line or not line[0].isspace():
        return None, []
    body = line.strip()
    if not body:
        return None, []
    parts = body.split(None, 1)
    return parts[0], split_args(parts[1]) if len(parts) > 1 else []


def label_of(line):
    """Return the label defined on this line ('Foo', '.bar') or None."""
    m = re.match(r"^(\.?" + IDENT + r")(::?|\s*$)", line)
    if m and not line[0].isspace():
        return m.group(1)
    return None


class Consts(dict):
    """Evaluate RGBDS constant files: const_def/const/const_skip/const_next/DEF EQU and friends."""

    def __init__(self):
        super().__init__()
        self.order = {}

    def eval(self, expr):
        expr = expr.strip()
        if expr in self:
            return self[expr]
        py = re.sub(r"\$([0-9A-Fa-f]+)", lambda m: str(int(m.group(1), 16)), expr)
        py = re.sub(r"%([01]+)", lambda m: str(int(m.group(1), 2)), py)

        def name(m):
            n = m.group(0)
            if n in self:
                return str(self[n])
            raise KeyError("unknown constant %r in %r" % (n, expr))

        py = re.sub(IDENT, name, py)
        py = py.replace("/", "//")
        return int(eval(py, {"__builtins__": {}}))

    def load(self, path, collect=None):
        value, step = 0, 1
        in_macro = False
        for line in read_lines(path):
            s = line.strip()
            if not s:
                continue
            if s.startswith("MACRO"):
                in_macro = True
            if in_macro:
                in_macro = s != "ENDM"
                continue
            m = re.match(r"DEF\s+(" + IDENT + r")\s+(EQU|=)\s+(.+)$", s)
            if m:
                try:
                    self[m.group(1)] = self.eval(m.group(3))
                except (KeyError, SyntaxError, NameError, TypeError):
                    pass
                continue
            op, args = macro(line)
            if op is None:
                continue
            if op == "const_def":
                value = self.eval(args[0]) if args else 0
                step = self.eval(args[1]) if len(args) > 1 else 1
            elif op in ("const", "const_export", "map_const", "music_const", "trainer_const", "add_tm", "add_hm"):
                n = args[0]
                if op == "trainer_const":
                    self["OPP_" + n] = 200 + value
                if op in ("add_tm",):
                    n = "TM_" + args[0]
                if op in ("add_hm",):
                    n = "HM_" + args[0]
                self[n] = value
                self.order[n] = value
                if collect is not None:
                    collect.append((n, value, args))
                self["const_value"] = value + step
                value += step
            elif op == "const_skip":
                value += step * (self.eval(args[0]) if args else 1)
            elif op == "const_next":
                value = self.eval(args[0])
            self["const_value"] = value
        return self


def num(s, consts=None):
    s = s.strip()
    if s.startswith("$"):
        return int(s[1:], 16)
    if s.startswith("%"):
        return int(s[1:], 2)
    if re.match(r"^-?\d+$", s):
        return int(s)
    if consts is not None:
        return consts.eval(s)
    raise ValueError(s)


def files(root, sub, ext=".asm"):
    base = os.path.join(root, sub)
    for dirpath, _, names in os.walk(base):
        for n in sorted(names):
            if n.endswith(ext):
                yield os.path.join(dirpath, n)
