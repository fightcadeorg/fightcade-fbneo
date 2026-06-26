#!/usr/bin/env python3
import ninja_syntax
from pathlib import Path

ninja = ninja_syntax.Writer(open("build.ninja", "w"), width=4)

ninja.rule("generate_ninja", command="python3 $in", generator=True)
ninja.build("build.ninja", "generate_ninja", __file__)
cc_win = "zig cc -target x86-windows-gnu"
cc = "clang"
ninja.rule("cfile", "$cc -g -std=c11 $flags -fno-sanitize=undefined -c $in -o $out")
ninja.rule("exe", "$cc -g $flags -fno-sanitize=undefined $in -o $out")
ninja.rule("dll", "$cc -shared -fno-sanitize=undefined -lWs2_32 -Wl,--out-implib,$libname.lib $in -o $out")

ggpo_source = [
	"ggpo_api.c",
	"ggpo_buffers.c",
	"ggpo_buffers_test.c",
	"ggpo_client_to_server.c",
	"ggpo_input.c",
	"ggpo_input_test.c",
	"ggpo_logging.c",
	"ggpo_network.c",
	"ggpo_network_test.c",
	"ggpo_peer_to_peer.c",
	"ggpo_peer_to_peer_test.c",
]

objects = {}
objects_win = {}
for source in ggpo_source:
	build = Path("build")
	obj_path = (build / Path(source)).with_suffix(".o")
	obj_path_win = obj_path.with_stem(f"{obj_path.stem}_win")
	obj_path = str(obj_path)
	obj_path_win = str(obj_path_win)
	ninja.build([obj_path], "cfile", [source], variables={"flags": "-Iinclude -I../libs/zlib", "cc": cc})
	ninja.build([obj_path_win], "cfile", [source], variables={"flags": "-Iinclude -I../libs/zlib", "cc": cc_win})
	objects[source] = obj_path
	objects_win[source] = obj_path_win

zlib_source = [
	"../libs/zlib/adler32.c",
	"../libs/zlib/compress.c",
	"../libs/zlib/crc32.c",
	"../libs/zlib/deflate.c",
	"../libs/zlib/gzclose.c",
	"../libs/zlib/gzlib.c",
	"../libs/zlib/gzread.c",
	"../libs/zlib/gzwrite.c",
	"../libs/zlib/infback.c",
	"../libs/zlib/inffast.c",
	"../libs/zlib/inflate.c",
	"../libs/zlib/inftrees.c",
	"../libs/zlib/trees.c",
	"../libs/zlib/uncompr.c",
	"../libs/zlib/zutil.c",
]

for source in zlib_source:
	build = Path("build")
	obj_path = (build / Path(*Path(source).parts[1:])).with_suffix(".o")
	obj_path_win = obj_path.with_stem(f"{obj_path.stem}_win")
	obj_path = str(obj_path)
	obj_path_win = str(obj_path_win)
	ninja.build([obj_path], "cfile", [source], variables={"flags": "-Werror -Wno-deprecated-non-prototype -iquote ../libs/zlib -I../libs/zlib -Iinclude", "cc": cc})
	ninja.build([obj_path_win], "cfile", [source], variables={"flags": "-Werror -Wno-deprecated-non-prototype -iquote ../libs/zlib -I../libs/zlib -Iinclude", "cc": cc_win})
	objects[source] = obj_path
	objects_win[source] = obj_path_win

dll_source = [
	"ggpo_shim.c",
]
for source in dll_source:
	build = Path("build")
	source = Path(source)
	obj_path_win = (build / source).with_stem(f"{source.stem}_win").with_suffix(".o")
	obj_path_win = str(obj_path_win)
	source = str(source)
	ninja.build([obj_path_win], "cfile", [source], variables={"flags": "-Iinclude", "cc": cc_win})
	objects_win[source] = obj_path_win

def exe(name: str, source: list[str]):
	ninja.build([f"build/{name}.exe"], "exe",
		[objects_win[s] for s in source],
		variables={"cc":cc_win, "flags": "-lWs2_32 -lwinmm"}
	)
	ninja.build([f"build/{name}"], "exe",
		[objects[s] for s in source if s in objects],
		variables={"cc": cc, "flags": "-lz"}
	)

exe("ggpo_buffers_test", [
	"ggpo_buffers.c",
	"ggpo_buffers_test.c",
])

exe("ggpo_input_test", [
	"ggpo_input.c",
	"ggpo_logging.c",
	"ggpo_input_test.c",
])

exe("ggpo_peer_to_peer_test", [
	"ggpo_network.c",
	"ggpo_input.c",
	"ggpo_logging.c",
	"ggpo_peer_to_peer.c",
	"ggpo_peer_to_peer_test.c",
])

def dll(name: str, source: list[str]):
	ninja.build([f"build/{name}.dll"], "exe",
		[objects_win[s] for s in source],
		variables={"cc": cc_win, "flags": f"-shared -lWs2_32 -lwinmm -Wl,-implib,{name}.lib"}
	)

dll("shim", [
	"ggpo_shim.c",
])

dll("ggponet", [
	"ggpo_api.c",
	"ggpo_buffers.c",
	"ggpo_client_to_server.c",
	"ggpo_input.c",
	"ggpo_logging.c",
	"ggpo_network.c",
	"ggpo_peer_to_peer.c",
	*zlib_source,
])
