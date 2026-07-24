#!/usr/bin/env python3
"""
flowdiff - structural diff of two exported flow files (Serializer output).

Compares two {class,name,props,children} trees by STRUCTURE, not names:
instance names, containers, and the specific target paths are minted fresh on
every import, so a raw text diff is all noise. This counts what actually
matters - how many of each class, the containment shape, and whether each
alias still resolves to something INSIDE the file - so a round-trip regression
(a dropped clone, an alias left dangling, a whole missing subtree) shows up as
a real difference and a re-export of the same graph shows up as identical.

    python3 testharness/flowdiff.py saved/View_1.flow saved/View_2.flow
"""

import json
import sys
from collections import Counter


def load(path):
    with open(path) as f:
        return json.load(f)


def walk(node):
    """yield every node in the tree, root first."""
    yield node
    for c in node.get("children", []):
        yield from walk(c)


def class_counts(root):
    return Counter(n.get("class", "?") for n in walk(root))


def fingerprint(node):
    """a canonical structural signature: class + the multiset of child
       signatures, sorted so sibling order and names never matter."""
    kids = sorted(fingerprint(c) for c in node.get("children", []))
    return (node.get("class", "?"), tuple(kids))


def leaf_class_map(root):
    """leaf Name -> class for every node. Internal links are stored RELATIVE to
       the export root (Serializer's RelTo), so their last segment names a node
       inside this file; names are unique within a file, so the leaf resolves."""
    m = {}
    for n in walk(root):
        nm = n.get("props", {}).get("Name")
        if nm:
            m[nm] = n.get("class", "?")
    return m


def classify_target(target, names):
    """A link target is INTERNAL if it is relative (no leading '/') - it points
       inside the exported view and its leaf resolves to a node here. It is
       EXTERNAL if absolute - it points outside the view on purpose. A relative
       target whose leaf is unknown is a broken internal link."""
    if not target:
        return ("external", "(empty)")
    if target.startswith("/"):
        return ("external", target)
    leaf = target.rsplit("/", 1)[-1]
    if leaf in names:
        return ("internal", names[leaf])
    return ("broken", target)


def alias_links(root):
    """for every Alias: internal (relative, resolves), external (absolute,
       points outside), or broken (relative but names nothing here)."""
    names = leaf_class_map(root)
    internal = Counter()
    external, broken = [], []
    for n in walk(root):
        if n.get("class") != "Alias":
            continue
        prop = n.get("props", {}).get("TargetProp", "?")
        kind, info = classify_target(n.get("props", {}).get("Target", ""), names)
        if kind == "internal":
            internal[f"Alias.{prop} -> {info}"] += 1
        elif kind == "external":
            external.append(info)
        else:
            broken.append(info)
    return internal, external, broken


def wire_links(root):
    """every outgoing wire, classified like an alias target: internal (relative
       sink, resolves inside the view), external (absolute, on purpose), or
       broken (relative but names nothing here - a dropped connection)."""
    names = leaf_class_map(root)
    internal = Counter()
    external, broken = [], []
    total = 0
    for n in walk(root):
        src = n.get("class", "?")
        for w in n.get("wires", []):
            total += 1
            kind, info = classify_target(w.get("to", ""), names)
            edge = f"{src}.{w.get('from','?')}"
            if kind == "internal":
                internal[f"{edge} -> {info}.{w.get('port','?')}"] += 1
            elif kind == "external":
                external.append(f"{edge} -> {info}")
            else:
                broken.append(f"{edge} -> {info}")
    return total, internal, external, broken


def structural_multiset(root):
    """the bag of every node's fingerprint - lets us report exactly which
       structural pieces one side has that the other does not."""
    return Counter(fingerprint(n) for n in walk(root))


def fp_label(fp):
    cls, kids = fp
    if not kids:
        return cls
    inner = Counter(k[0] for k in kids)
    parts = ", ".join(f"{v}x{k}" for k, v in sorted(inner.items()))
    return f"{cls}[{parts}]"


def main():
    if len(sys.argv) >= 3:
        pa, pb = sys.argv[1], sys.argv[2]
    else:
        pa, pb = "saved/View_1.flow", "saved/View_2.flow"

    a, b = load(pa), load(pb)
    ca, cb = class_counts(a), class_counts(b)

    print(f"flowdiff  A={pa}  B={pb}\n")

    print(f"{'class':<16}{'A':>5}{'B':>5}{'  Δ':>6}")
    diffs = 0
    for cls in sorted(set(ca) | set(cb)):
        d = cb[cls] - ca[cls]
        if d:
            diffs += 1
        mark = "" if d == 0 else f"  {'+' if d>0 else ''}{d}"
        print(f"{cls:<16}{ca[cls]:>5}{cb[cls]:>5}{mark:>6}")
    print(f"{'-'*16}")
    print(f"{'total':<16}{sum(ca.values()):>5}{sum(cb.values()):>5}")
    print()

    # structural shape, names ignored
    if fingerprint(a) == fingerprint(b):
        print("structure (names ignored):  IDENTICAL")
    else:
        print("structure (names ignored):  DIFFERENT")
        ma, mb = structural_multiset(a), structural_multiset(b)
        only_a = ma - mb
        only_b = mb - ma
        for fp, n in only_a.items():
            print(f"    only in A ({n}x):  {fp_label(fp)}")
        for fp, n in only_b.items():
            print(f"    only in B ({n}x):  {fp_label(fp)}")
    print()

    # alias links - internal (relative, self-contained) vs external vs broken
    ra, ea, ba = alias_links(a)
    rb, eb, bb = alias_links(b)
    print("alias links (internal = relative & self-contained):")
    for side, res, ext, brk in (("A", ra, ea, ba), ("B", rb, eb, bb)):
        edges = ", ".join(f"{k} (x{v})" for k, v in sorted(res.items())) or "none"
        print(f"    {side}: {edges}   [{sum(res.values())} internal, {len(ext)} external, {len(brk)} broken]")
        for t in ext:
            print(f"        external target: {t}")
        for t in brk:
            print(f"        BROKEN target: {t}")
    print()

    # connections (wires) - same classification
    ta, wa, ea2, ba2 = wire_links(a)
    tb, wb, eb2, bb2 = wire_links(b)
    print("connections (wires, internal = relative & self-contained):")
    for side, tot, res, ext, brk in (("A", ta, wa, ea2, ba2), ("B", tb, wb, eb2, bb2)):
        edges = ", ".join(f"{k} (x{v})" for k, v in sorted(res.items())) or "none"
        print(f"    {side}: {edges}   [{tot} total, {sum(res.values())} internal, {len(ext)} external, {len(brk)} broken]")
        for t in ext:
            print(f"        external wire: {t}")
        for t in brk:
            print(f"        BROKEN wire: {t}")


if __name__ == "__main__":
    main()
