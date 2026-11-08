#!/usr/bin/env python3
"""Turn the ninja link command of libdux-qt-base.so into a pkg-config file that
links the same objects and archives statically into a cgo binary."""
import hashlib, os, shlex, subprocess, sys

build = os.path.abspath(sys.argv[1])
out = sys.argv[2]
version = open(os.path.join(build, "CMakeCache.txt")).read()
version = [l.split("=")[1] for l in version.splitlines() if l.startswith("CMAKE_PROJECT_VERSION:")][0]
cmd = subprocess.check_output(
    ["ninja", "-C", build, "-t", "commands", f"src/libdux-qt-base.so.{version}"], text=True
).strip().splitlines()[-1]
if "&&" in cmd:
    cmd = [p for p in cmd.split("&&") if "-o " in p][0]
toks = shlex.split(cmd)[1:]

drop_prefix = ("-Wl,-soname", "-Wl,--dependency-file", "-Wl,-rpath", "-Wl,--version-script")
flags = []
libdirs = []
skip = False
for t in toks:
    if skip:
        skip = False
        continue
    if t == "-o":
        skip = True
        continue
    if t in ("-shared", "-fPIC", "-fPIE", "-pie") or t.startswith(("-O", "-D", "-g", "-std=")):
        continue
    if t.startswith(drop_prefix):
        continue
    if t.endswith((".o", ".a")) and not os.path.isabs(t):
        t = os.path.join(build, t)
    if ".so" in t and os.path.isabs(t):
        # cgo's flag allowlist rejects versioned .so paths, -l<name> is accepted
        name = os.path.basename(t).split(".so")[0]
        libdir = os.path.dirname(t)
        if libdir not in libdirs:
            libdirs.append(libdir)
        t = "-l" + name[3:]
    flags.append(t)
flags = ["-L" + d for d in libdirs] + flags + ["-lstdc++", "-lm"]

src = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
os.makedirs(os.path.dirname(out), exist_ok=True)
libs = " ".join(shlex.quote(x) for x in flags)
# go's build cache ignores pkg-config link flags, a define keyed on them busts it
link_id = hashlib.sha1(libs.encode()).hexdigest()[:12]
with open(out, "w") as f:
    f.write(f"""Name: dux-qt-base
Description: quickshell runtime linked statically from the in-tree build
Version: {version}
Cflags: -I{src}/include -DDQB_LINK_ID_{link_id}
Libs: {libs}
""")
print(f"{out}: {sum(x.endswith('.a') for x in flags)} archives, {sum(x.endswith('.o') for x in flags)} objects")
