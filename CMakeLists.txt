cmake_minimum_required(VERSION 3.23)
project(git)


find_package(BISON REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(QDBM REQUIRED qdbm)

set(PARSER_DIR "${CMAKE_CURRENT_BINARY_DIR}")

bison_target(PARSER src/grammar.y
        ${PARSER_DIR}/grammar.c
        DEFINES_FILE ${PARSER_DIR}/x.tab.h)
include_directories(src)

add_executable(coldmud
    ${BISON_PARSER_OUTPUTS}
    ./src/loc.c
    ./src/object.c
    ./src/syntaxop.c
    ./src/list.c
    ./src/util.c
    ./src/net.c
    ./src/bufferop.c
    ./src/db.c
    ./src/ident.c
    ./src/log.c
    ./src/dump.c
    ./src/errorop.c
    ./src/stringop.c
    ./src/data.c
    ./src/execute.c
    ./src/objectop.c
    ./src/buffer.c
    ./src/io.c
    ./src/dictop.c
    ./src/listop.c
    ./src/miscop.c
    ./src/opcodes.c
    ./src/memory.c
    ./src/match.c
    ./src/ioop.c
    ./src/string.c
    ./src/methodop.c
    ./src/decode.c
    ./src/codegen.c
    ./src/regexp.c
    ./src/lookup.c
    ./src/adminop.c
    ./src/dict.c
    ./src/arithop.c
    ./src/cache.c
    ./src/main.c
    ./src/sig.c
    ./src/dataop.c
    ./src/token.c
    ./src/dbpack.c
)
target_include_directories(coldmud PRIVATE ${PARSER_DIR} ${QDBM_INCLUDE_DIRS})
target_link_libraries(coldmud ${QDBM_LIBRARIES} crypt)