"""Decode pokeyellow text (text/*.asm, data/text/*.asm, scripts/*.asm, engine/, home/) into plain strings.

Rules (from macros/scripts/text.asm and constants/charmap.asm):
  text "..."            starts or continues text
  line / cont / next    new line, written as \\n
  para / page           new box, written as \\f
  done / prompt / dex / text_end / "@"   end
  text_far X            the text at label X, inline
  text_ram wX           {str:wX}
  text_decimal / text_bcd wX, ...   {num:wX}
  text_dots n           n ellipses
  <PLAYER> <RIVAL>      kept as tokens; "#" becomes POKé; other charmap tokens become their printed glyphs
"""
import os
import re

import asmlib

# How the charmap control/glyph tokens print (constants/charmap.asm comments and gfx).
TOKENS = {
    "#": "POKé", "<PKMN>": "PKMN", "<PK>": "PK", "<MN>": "MN", "<PC>": "PC", "<TM>": "TM",
    "<TRAINER>": "TRAINER", "<ROCKET>": "ROCKET", "<……>": "……", "<COLON>": ":", "<DOT>": ".",
    "<ED>": "ED", "<LV>": ":L", "<ID>": "ID", "<to>": "to", "<BOLD_V>": "V", "<BOLD_S>": "S", "<BOLD_P>": "P",
    "<PLAYER>": "<PLAYER>", "<RIVAL>": "<RIVAL>", "<TARGET>": "<TARGET>", "<USER>": "<USER>",
    "<NEXT>": "\n", "<LINE>": "\n", "<CONT>": "\n", "<_CONT>": "\n", "<PARA>": "\f", "<PAGE>": "\f",
    "<SCROLL>": "\n", "<NULL>": "",
}
END_TOKENS = ("@", "<DONE>", "<PROMPT>", "<DEXEND>")
PREFIX = {"text": "", "line": "\n", "cont": "\n", "next": "\n", "para": "\f", "page": "\f", "db": ""}
IGNORED = {"vc_patch", "vc_hook", "text_start", "text_promptbutton", "text_waitbutton", "text_pause", "text_scroll", "text_low",
           "sound_get_item_1", "sound_level_up", "sound_get_item_2", "sound_get_key_item", "sound_caught_mon",
           "sound_dex_page_added", "sound_cry_pikachu", "sound_cry_pidgeot", "sound_cry_dewgong",
           "sound_pokedex_rating", "sound_get_item_1_duplicate"}
SPECIAL = {"script_pokecenter_nurse", "script_mart", "script_bills_pc", "script_players_pc", "script_pokecenter_pc",
           "script_prize_vendor", "script_cable_club_receptionist", "script_vending_machine"}


class Charmap:
    def __init__(self, root):
        self.chars = set()
        for line in asmlib.read_lines(os.path.join(root, "constants", "charmap.asm")):
            m = re.match(r'\s*charmap\s+"(.*)",', line)
            if m:
                self.chars.add(m.group(1))
        self.multi = sorted((c for c in self.chars if len(c) > 1), key=len, reverse=True)

    def decode(self, s, where):
        """Decode one source string. Returns (text, ended)."""
        out = []
        i = 0
        while i < len(s):
            tok = None
            if s[i] == "<":
                j = s.index(">", i) + 1
                tok = s[i:j]
            else:
                for m in self.multi:
                    if not m.startswith("<") and s.startswith(m, i):
                        tok = m
                        break
                if tok is None:
                    tok = s[i]
            if tok not in self.chars:
                raise ValueError("%s: %r is not in constants/charmap.asm" % (where, tok))
            i += len(tok)
            if tok in END_TOKENS:
                if tok == "<DEXEND>":
                    out.append(".")  # home/text.asm PlaceDexEnd
                return "".join(out), True
            out.append(TOKENS.get(tok, tok))
        return "".join(out), False


def strings_in(args):
    out = []
    for a in args:
        a = a.strip()
        if a.startswith('"') and a.endswith('"'):
            out.append(a[1:-1].replace('\\"', '"'))
    return out


def drop_conditionals(lines):
    """Keep the retail branch of IF DEF(_YELLOW_VC) / IF DEF(_DEBUG) blocks (blank the rest, keep line numbers)."""
    out, stack = [], []
    for line in lines:
        s = line.strip()
        m = re.match(r"IF\s+(!?)DEF\((\w+)\)", s)
        if m:
            stack.append(bool(m.group(1)))  # IF DEF(x) with x undefined -> false; IF !DEF(x) -> true
            out.append("")
            continue
        if s == "ELSE" and stack:
            stack[-1] = not stack[-1]
            out.append("")
            continue
        if s == "ENDC" and stack:
            stack.pop()
            out.append("")
            continue
        out.append(line if all(stack) else "")
    return out


class Texts:
    def __init__(self, root, dirs=("text", "data", "scripts", "engine", "home", "maps.asm", "text.asm")):
        self.root = root
        self.cm = Charmap(root)
        self.files = {}
        self.labels = {}
        for d in ("text", "data", "scripts", "engine", "home"):
            for path in asmlib.files(root, d):
                rel = os.path.relpath(path, root).replace(os.sep, "/")
                lines = drop_conditionals(asmlib.read_lines(path))
                self.files[rel] = lines
                parent = None
                for i, line in enumerate(lines):
                    lab = asmlib.label_of(line)
                    if not lab:
                        continue
                    if lab.startswith("."):
                        if parent:
                            self.labels.setdefault(parent + lab, (rel, i))
                    else:
                        parent = lab
                        self.labels.setdefault(lab, (rel, i))
        self._cache = {}

    def parent_of(self, rel, i):
        for j in range(i, -1, -1):
            lab = asmlib.label_of(self.files[rel][j])
            if lab and not lab.startswith("."):
                return lab
        return None

    def qualify(self, name, rel, i):
        if name.startswith("."):
            return (self.parent_of(rel, i) or "") + name
        return name

    def body(self, label):
        """Decoded text for a label, or None if the label isn't text. ('asm' texts return None.)"""
        if label in self._cache:
            return self._cache[label]
        self._cache[label] = None  # recursion guard
        res = self._interpret(label)
        self._cache[label] = res
        return res

    def kind(self, label):
        """'text', 'asm' (text_asm handler), 'special:<macro>' or None."""
        if label not in self.labels:
            return None
        rel, i = self.labels[label]
        for line in self.files[rel][i + 1:]:
            if asmlib.label_of(line):
                continue
            op, args = asmlib.macro(line)
            if op is None:
                continue
            if op == "text_asm":
                return "asm"
            if op in SPECIAL:
                return "special:" + op
            if op == "db":
                return "text" if strings_in(args) else None
            if op == "text_end":
                return "text"
            if op in PREFIX or op in ("text_far", "text_ram", "text_decimal", "text_bcd", "text_dots") or op in IGNORED:
                return "text"
            return None
        return None

    def _interpret(self, label):
        if label not in self.labels:
            return None
        rel, i = self.labels[label]
        out = []
        started = False
        at_sign = False  # a text command's string ended with '@': the next command goes on (text_ram...)
        for j in range(i + 1, len(self.files[rel])):
            line = self.files[rel][j]
            if asmlib.label_of(line):
                if at_sign:
                    return "".join(out)
                continue
            op, args = asmlib.macro(line)
            if op is None:
                continue
            where = "%s:%d" % (rel, j + 1)
            if at_sign and op not in PREFIX and op not in ("text_far", "text_ram", "text_decimal", "text_bcd",
                                                          "text_dots", "done", "prompt", "dex", "text_end"):
                return "".join(out)
            at_sign = False
            if op in PREFIX:
                strs = strings_in(args)
                if op == "db" and not strs:
                    return "".join(out) if started else None
                if op != "db" and not strs and not args:
                    strs = [""]
                if started or op not in ("text", "db"):
                    out.append(PREFIX[op])
                started = True
                for s in strs:
                    t, ended = self.cm.decode(s, where)
                    out.append(t)
                    if ended:
                        if op == "db":
                            return "".join(out)
                        at_sign = True  # '@' ends this string, not the text (home/text.asm TextCommandProcessor)
                        break
            elif op in ("done", "prompt", "dex", "text_end"):
                if op == "dex":
                    out.append(".")  # home/text.asm PlaceDexEnd prints a period
                return "".join(out)
            elif op == "text_far":
                sub = self.body(self.qualify(args[0], rel, j))
                if sub is None:
                    raise ValueError("%s: text_far %s is not text" % (where, args[0]))
                out.append(sub)
                started = True
            elif op == "text_ram":
                out.append("{str:%s}" % args[0])
                started = True
            elif op in ("text_decimal", "text_bcd"):
                out.append("{num:%s}" % args[0])
                started = True
            elif op == "text_dots":
                out.append("…" * asmlib.num(args[0]))
                started = True
            elif op in IGNORED:
                started = True
            else:
                # text_asm, code or data: the text part (if any) ends here
                return "".join(out) if started and out else None
        return "".join(out) if started else None

    def region(self, label):
        """Lines of a handler: from its label to the next global label (local labels stay inside)."""
        rel, i = self.labels[label]
        lines = self.files[rel]
        j = i + 1
        while j < len(lines):
            lab = asmlib.label_of(lines[j])
            if lab and not lab.startswith("."):
                break
            j += 1
        return rel, i, j

    def refs(self, label, _seen=None):
        """Text labels (and trainer header labels) a text_asm handler uses, in source order.
        Follows farcall/callfar into other files and call/jp to other routines in the same file."""
        seen = _seen if _seen is not None else set()
        seen.add(label)
        rel, i, j = self.region(label)
        found = []

        def add(n):
            if n not in found:
                found.append(n)

        for k in range(i + 1, j):
            line = self.files[rel][k]
            lab = asmlib.label_of(line)
            if lab and lab.startswith("."):
                q = self.qualify(lab, rel, k)
                if self.kind(q) == "text":
                    add(q)
                continue
            for m in re.finditer(r"\b(?:ld\s+(?:hl|de|bc)\s*,\s*|text_far\s+|dw\s+)(\.?[A-Za-z_][\w.]*)", line):
                q = self.qualify(m.group(1), rel, k)
                if q in self.labels and (self.kind(q) == "text" or "TrainerHeader" in q):
                    add(q)
                elif (q in self.labels and q not in seen and not q.startswith(".") and self.kind(q) is None
                      and self.labels[q][0].startswith("scripts/") and "Text" in q):
                    for r in self.refs(q, seen):  # a routine passed by pointer (ld hl, XPrintText; jp Common)
                        add(r)
            m = re.search(r"^(farcall|callfar|call|jp|jr|farjp)\s+(?:\w+\s*,\s*)?([A-Za-z_]\w*)\s*$", line.strip())
            if m:
                tgt = m.group(2)
                if tgt in self.labels and tgt not in seen and self.kind(tgt) is None:
                    same_file = self.labels[tgt][0] == rel
                    if m.group(1) in ("farcall", "callfar", "farjp") or same_file:
                        for r in self.refs(tgt, seen):
                            add(r)
        return found
