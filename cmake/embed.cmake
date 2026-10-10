# Usage: cmake -DIN=<binary> -DOUT=<out.inc> -DSYM=<name> -P embed.cmake
# Writes "const unsigned char <SYM>[] = { ... };" plus "<SYM>_SIZE".
file(READ "${IN}" hex HEX)
string(LENGTH "${hex}" hex_len)
math(EXPR byte_count "${hex_len} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," arr "${hex}")
file(WRITE "${OUT}"
    "extern const unsigned char ${SYM}[];\n"
    "const unsigned char ${SYM}[] = {${arr}};\n"
    "extern const unsigned long ${SYM}_SIZE;\n"
    "const unsigned long ${SYM}_SIZE = ${byte_count};\n")
