#!/usr/bin/env python3
"""dfdiff - compare two exported flows by what they MEAN, not by their text.

A flow file is a tree of instances, each with a property bag and a list of
wires. Nothing in it is ordered in any way a reader should care about: the
serializer emits properties in whatever order the node's property list
holds, and wires in whatever order the subscriber lists hold. A textual
diff of two flows is therefore mostly noise.

This keys every instance by its path relative to the export root, compares
property bags as sets, and compares wires as an unordered set of
(source, from-port, sink, to-port). Two flows that describe the same thing
compare equal no matter how either file was written.

    dfdiff.py a.flow b.flow
    dfdiff.py a.flow b.flow --ignore Value --ignore W --ignore H
    dfdiff.py a.flow b.flow --all      # include the volatile fields

Exit status is 0 when they match, 1 when they differ, 2 on a bad file.
"""

import argparse
import json
import sys

# fields that differ between two runs of the same thing and would otherwise
# drown the signal: lifecycle state and where a panel happened to be sitting
VOLATILE = ["State", "ReservedViewOpen",
            "ReservedViewPanelX", "ReservedViewPanelY", "_Internals"]


def shown(path):
    """the export root prints as "/" - "" is right in the data, unreadable
    in a report"""
    return path if path else "/"


def flatten(node, path, insts, wires):
    """{path: (class, props)} and {(src, fromPort, sink, toPort)}.

    Paths are relative to the EXPORT ROOT, which is itself "" - exactly
    how RelTo writes a reference to it, so a wire's sink and an instance's
    key are the same string. That is what lets the same view exported
    from two different containers compare equal.

    The path comes from the TREE, not from the Container property - a
    child is under its parent by construction, and Container is only a
    spelling of the same fact."""
    name = node.get("props", {}).get("Name") or node.get("name") or "?"
    here = path + "/" + name if path else name
    insts[here] = (node.get("class"), dict(node.get("props") or {}))

    for w in node.get("wires") or []:
        wires.add((here, w.get("from"), w.get("to"), w.get("port")))

    for kid in node.get("children") or []:
        flatten(kid, here, insts, wires)


def load(fname):
    try:
        with open(fname) as f:
            top = json.load(f)
    except (IOError, ValueError) as e:
        print("dfdiff: %s: %s" % (fname, e), file=sys.stderr)
        sys.exit(2)
    insts, wires = {}, set()
    insts[""] = (top.get("class"), dict(top.get("props") or {}))
    for w in top.get("wires") or []:
        wires.add(("", w.get("from"), w.get("to"), w.get("port")))
    for kid in top.get("children") or []:
        flatten(kid, "", insts, wires)
    return insts, wires


def main():
    ap = argparse.ArgumentParser(description="structural diff of two exported flows")
    ap.add_argument("a")
    ap.add_argument("b")
    ap.add_argument("--ignore", action="append", default=[],
                    metavar="PROP", help="also ignore this property (repeatable)")
    ap.add_argument("--all", action="store_true",
                    help="compare every property, including the volatile ones")
    args = ap.parse_args()

    skip = set() if args.all else set(VOLATILE)
    skip.update(args.ignore)

    ai, aw = load(args.a)
    bi, bw = load(args.b)

    out = []

    only_a = sorted(set(ai) - set(bi))
    only_b = sorted(set(bi) - set(ai))
    if only_a:
        out.append("only in %s:" % args.a)
        out += ["    %s   (%s)" % (shown(p), ai[p][0]) for p in only_a]
    if only_b:
        out.append("only in %s:" % args.b)
        out += ["    %s   (%s)" % (shown(p), bi[p][0]) for p in only_b]

    for p in sorted(set(ai) & set(bi)):
        acls, aprops = ai[p]
        bcls, bprops = bi[p]
        lines = []
        if acls != bcls:
            lines.append("    class: %s -> %s" % (acls, bcls))
        keys = (set(aprops) | set(bprops)) - skip
        for k in sorted(keys):
            av, bv = aprops.get(k), bprops.get(k)
            if av == bv:
                continue
            if av is None:
                lines.append("    + %s = %r" % (k, bv))
            elif bv is None:
                lines.append("    - %s = %r" % (k, av))
            else:
                lines.append("      %s: %r -> %r" % (k, av, bv))
        if lines:
            out.append("%s:" % shown(p))
            out += lines

    gone = sorted(aw - bw)
    added = sorted(bw - aw)
    if gone:
        out.append("wires only in %s:" % args.a)
        out += ["    %s.%s -> %s.%s" % (shown(w[0]), w[1], shown(w[2]), w[3]) for w in gone]
    if added:
        out.append("wires only in %s:" % args.b)
        out += ["    %s.%s -> %s.%s" % (shown(w[0]), w[1], shown(w[2]), w[3]) for w in added]

    if not out:
        n = len(ai)
        print("identical: %d instance%s, %d wire%s"
              % (n, "" if n == 1 else "s", len(aw), "" if len(aw) == 1 else "s"))
        if skip:
            print("(ignored: %s)" % ", ".join(sorted(skip)))
        return 0

    print("\n".join(out))
    if skip:
        print("\n(ignored: %s)" % ", ".join(sorted(skip)))
    return 1


if __name__ == "__main__":
    sys.exit(main())
