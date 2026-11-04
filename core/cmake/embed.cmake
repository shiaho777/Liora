# Converts a data file into a C++ source with a byte array.
# Usage: cmake -DINPUT=<file> -DOUTPUT=<file.cpp> -DSYMBOL=<name> -P embed.cmake

file(READ "${INPUT}" content HEX)
string(LENGTH "${content}" hex_length)
math(EXPR size "${hex_length} / 2")
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")

file(WRITE "${OUTPUT}.tmp"
  "// Generated from ${INPUT}. Do not edit.\n"
  "#include <cstddef>\n"
  "namespace liora::embedded {\n"
  "extern const unsigned char ${SYMBOL}[] = {${bytes}0x00};\n"
  "extern const std::size_t ${SYMBOL}_size = ${size};\n"
  "}  // namespace liora::embedded\n")
file(COPY_FILE "${OUTPUT}.tmp" "${OUTPUT}" ONLY_IF_DIFFERENT)
file(REMOVE "${OUTPUT}.tmp")