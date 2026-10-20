# GNU Go 3.8 from its release source, for go_engine's image (#1668).
#
# The release's autotools build is replaced here rather than wrapped:
# rules_foreign_cc's configure checks fail against the hermetic toolchain's
# sysroot. config.h is what that release's ./configure writes on Linux
# x86-64 with --without-readline --without-curses --disable-color, less
# HAVE_CURSES_H and HAVE_TERM_H (read only under TERMINFO, which is unset,
# and the hermetic sysroot has neither header); the pattern sources it generates are generated below by its own tools, with
# the arguments its patterns/Makefile.am gives them.
#
# GPL-3: go_engine runs the binary as a child process, and nothing links it.

load("@rules_cc//cc:defs.bzl", "cc_binary", "cc_library")

exports_files(["COPYING"])

# 3.8 predates C99's strict defaults: its globals are tentative
# definitions (-fcommon), and it leans on overflow wrapping, which a current
# clang at -O2 does not keep (-fno-strict-overflow; without it the first
# genmove trips board.c's ON_BOARD1 assertion).
COPTS = [
    "-DHAVE_CONFIG_H",
    "-fno-strict-overflow",
    "-fcommon",
    "-w",
]

genrule(
    name = "config_h",
    outs = ["config.h"],
    cmd = """cat > $@ <<'EOF'
#define ALTERNATE_CONNECTIONS 1
#define CHINESE_RULES 0
#define COSMIC_GNUGO 0
#define DEFAULT_LEVEL 10
#define DEFAULT_MEMORY -1
#define ENABLE_SOCKET_SUPPORT 1
#define EXPERIMENTAL_CONNECTIONS 1
#define EXPERIMENTAL_OWL_EXT 0
#define GNU_PACKAGE "GNU gnugo"
#define GRID_OPT 1
#define HAVE_GETTIMEOFDAY 1
#define HAVE_INTTYPES_H 1
#define HAVE_MEMORY_H 1
#define HAVE_STDINT_H 1
#define HAVE_STDLIB_H 1
#define HAVE_STRINGS_H 1
#define HAVE_STRING_H 1
#define HAVE_SYS_STAT_H 1
#define HAVE_SYS_TIMES_H 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYS_TYPES_H 1
#define HAVE_TIMES 1
#define HAVE_UNISTD_H 1
#define HAVE_USLEEP 1
#define HAVE_VARIADIC_DEFINE 1
#define HAVE_VSNPRINTF 1
#define LARGE_SCALE 0
#define ORACLE 0
#define OWL_NODE_LIMIT 1000
#define OWL_THREATS 0
#define PACKAGE "gnugo"
#define PACKAGE_BUGREPORT ""
#define PACKAGE_NAME "gnugo"
#define PACKAGE_STRING "gnugo 3.8"
#define PACKAGE_TARNAME "gnugo"
#define PACKAGE_VERSION "3.8"
#define READLINE 0
#define RESIGNATION_ALLOWED 1
#define SEMEAI_NODE_LIMIT 500
#define SIZEOF_LONG 8
#define STDC_HEADERS 1
#define TIME_WITH_SYS_TIME 1
#define USE_BREAK_IN 1
#define USE_VALGRIND 0
#define VERSION "3.8"
EOF""",
)

cc_library(
    name = "headers",
    hdrs = [":config_h"] + glob([
        "engine/*.h",
        "interface/*.h",
        "patterns/*.h",
        "sgf/*.h",
        "utils/*.h",
    ]),
    includes = [
        ".",
        "engine",
        "interface",
        "patterns",
        "sgf",
        "utils",
    ],
)

cc_library(
    name = "utils",
    srcs = [
        "utils/getopt.c",
        "utils/getopt1.c",
        "utils/gg_utils.c",
        "utils/random.c",
        "utils/winsocket.c",
    ],
    copts = COPTS,
    deps = [":headers"],
)

cc_library(
    name = "sgf",
    srcs = [
        "sgf/sgf_utils.c",
        "sgf/sgfnode.c",
        "sgf/sgftree.c",
    ],
    copts = COPTS,
    deps = [
        ":headers",
        ":utils",
    ],
)

cc_library(
    name = "board",
    srcs = [
        "engine/board.c",
        "engine/boardlib.c",
        "engine/hash.c",
        "engine/printutils.c",
    ],
    copts = COPTS,
    deps = [
        ":headers",
        ":sgf",
        ":utils",
    ],
)

# libengine.a less the board, which :board holds.
cc_library(
    name = "engine",
    srcs = ["engine/" + f for f in [
        "aftermath.c",
        "breakin.c",
        "cache.c",
        "clock.c",
        "combination.c",
        "dragon.c",
        "endgame.c",
        "filllib.c",
        "fuseki.c",
        "genmove.c",
        "globals.c",
        "handicap.c",
        "influence.c",
        "interface.c",
        "matchpat.c",
        "montecarlo.c",
        "move_reasons.c",
        "movelist.c",
        "optics.c",
        "oracle.c",
        "owl.c",
        "persistent.c",
        "readconnect.c",
        "reading.c",
        "semeai.c",
        "sgfdecide.c",
        "sgffile.c",
        "shapes.c",
        "showbord.c",
        "surround.c",
        "unconditional.c",
        "utils.c",
        "value_moves.c",
        "worm.c",
    ]],
    copts = COPTS,
    deps = [
        ":board",
        ":headers",
        ":sgf",
        ":utils",
    ],
)

# The pattern compilers, run at build time.
cc_binary(
    name = "mkpat",
    srcs = [
        "patterns/dfa.c",
        "patterns/mkpat.c",
        "patterns/transform.c",
    ],
    copts = COPTS,
    deps = [
        ":headers",
        ":utils",
    ],
)

cc_binary(
    name = "joseki",
    srcs = ["patterns/joseki.c"],
    copts = COPTS,
    deps = [
        ":board",
        ":headers",
        ":sgf",
        ":utils",
    ],
)

cc_binary(
    name = "mkeyes",
    srcs = ["patterns/mkeyes.c"],
    copts = COPTS,
    deps = [
        ":headers",
        ":utils",
    ],
)

cc_binary(
    name = "uncompress_fuseki",
    srcs = ["patterns/uncompress_fuseki.c"],
    copts = COPTS,
    deps = [
        ":board",
        ":headers",
        ":sgf",
        ":utils",
    ],
)

cc_binary(
    name = "mkmcpat",
    srcs = ["patterns/mkmcpat.c"],
    copts = COPTS,
    deps = [
        ":engine",
        ":headers",
    ],
)

# The joseki databases, from their SGF.
[genrule(
    name = "joseki_" + db,
    srcs = ["patterns/%s.sgf" % db],
    outs = ["patterns/%s.db" % db],
    cmd = "$(location :joseki) %s $< > $@" % prefix,
    tools = [":joseki"],
) for db, prefix in [
    ("gogo", "JG"),
    ("hoshi_keima", "JHK"),
    ("hoshi_other", "JHO"),
    ("komoku", "JK"),
    ("sansan", "JS"),
    ("mokuhazushi", "JM"),
    ("takamoku", "JT"),
]]

JOSEKI_DBS = [
    "gogo",
    "hoshi_keima",
    "hoshi_other",
    "komoku",
    "sansan",
    "mokuhazushi",
    "takamoku",
]

genrule(
    name = "josekidb_c",
    srcs = ["patterns/%s.db" % db for db in JOSEKI_DBS],
    outs = ["patterns/josekidb.c"],
    cmd = "$(location :mkpat) -C joseki " + " ".join([
        "-i $(location patterns/%s.db)" % db
        for db in JOSEKI_DBS
    ]) + " -o $@",
    tools = [":mkpat"],
)

# mkpat over one or more databases: (output, flags, name, databases, dtr).
[genrule(
    name = out + "_c",
    srcs = ["patterns/%s.db" % db for db in dbs] + (["patterns/%s.dtr" % dtr] if dtr else []),
    outs = ["patterns/%s.c" % out],
    cmd = "$(location :mkpat) %s %s%s %s -o $@" % (
        flags,
        ("-t $(location patterns/%s.dtr) " % dtr) if dtr else "",
        name,
        " ".join(["-i $(location patterns/%s.db)" % db for db in dbs]),
    ),
    tools = [":mkpat"],
) for out, flags, name, dbs, dtr in [
    (
        "patterns",
        "-b",
        "pat",
        [
            "patterns",
            "patterns2",
        ],
        None,
    ),
    (
        "apatterns",
        "-X",
        "attpat",
        ["attack"],
        None,
    ),
    (
        "dpatterns",
        "",
        "defpat",
        ["defense"],
        None,
    ),
    (
        "conn",
        "-c",
        "conn",
        ["conn"],
        None,
    ),
    (
        "endgame",
        "-b",
        "endpat",
        ["endgame"],
        None,
    ),
    (
        "influence",
        "-c",
        "influencepat",
        ["influence"],
        None,
    ),
    (
        "barriers",
        "-c -b",
        "barrierspat",
        ["barriers"],
        None,
    ),
    (
        "aa_attackpat",
        "-D -m -b",
        "aa_attackpat",
        ["aa_attackpats"],
        "aa_attackpats",
    ),
    (
        "owl_attackpat",
        "-D -m -b",
        "owl_attackpat",
        ["owl_attackpats"],
        "owl_attackpats",
    ),
    (
        "oraclepat",
        "-b",
        "oracle",
        ["oracle"],
        None,
    ),
    (
        "owl_vital_apat",
        "-D -m -b",
        "owl_vital_apat",
        ["owl_vital_apats"],
        "owl_vital_apats",
    ),
    (
        "owl_defendpat",
        "-D -m -b",
        "owl_defendpat",
        ["owl_defendpats"],
        "owl_defendpats",
    ),
    (
        "fusekipat",
        "-b",
        "fusekipat",
        ["fuseki"],
        None,
    ),
    (
        "handipat",
        "-b",
        "handipat",
        ["handicap"],
        None,
    ),
]]

genrule(
    name = "eyes_c",
    srcs = ["patterns/eyes.db"],
    outs = ["patterns/eyes.c"],
    cmd = "$(location :mkeyes) < $< > $@",
    tools = [":mkeyes"],
)

[genrule(
    name = "fuseki%d_c" % size,
    srcs = ["patterns/fuseki%d.dbz" % size],
    outs = ["patterns/fuseki%d.c" % size],
    cmd = "$(location :uncompress_fuseki) %d $< c > $@" % size,
    tools = [":uncompress_fuseki"],
) for size in [
    9,
    13,
    19,
]]

MC_DBS = [
    "mc_montegnu_classic",
    "mc_mogo_classic",
    "mc_uniform",
]

genrule(
    name = "mcpat_c",
    srcs = ["patterns/%s.db" % db for db in MC_DBS],
    outs = ["patterns/mcpat.c"],
    cmd = "$(location :mkmcpat) $(SRCS) > $@",
    tools = [":mkmcpat"],
)

cc_library(
    name = "patterns",
    srcs = [
        "patterns/connections.c",
        "patterns/helpers.c",
        "patterns/transform.c",
    ] + ["patterns/%s.c" % generated for generated in [
        "aa_attackpat",
        "apatterns",
        "barriers",
        "conn",
        "dpatterns",
        "endgame",
        "eyes",
        "fuseki13",
        "fuseki19",
        "fuseki9",
        "fusekipat",
        "handipat",
        "influence",
        "josekidb",
        "mcpat",
        "oraclepat",
        "owl_attackpat",
        "owl_defendpat",
        "owl_vital_apat",
        "patterns",
    ]],
    copts = COPTS,
    deps = [
        ":engine",
        ":headers",
    ],
)

cc_binary(
    name = "gnugo",
    srcs = ["interface/" + f for f in [
        "gmp.c",
        "gtp.c",
        "main.c",
        "play_ascii.c",
        "play_gmp.c",
        "play_gtp.c",
        "play_solo.c",
        "play_test.c",
    ]],
    copts = COPTS,
    linkopts = ["-lm"],
    visibility = ["//visibility:public"],
    deps = [
        ":engine",
        ":headers",
        ":patterns",
        ":sgf",
        ":utils",
    ],
)
