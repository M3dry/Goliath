#!/usr/bin/env bash

shopt -s globstar

OUT=$1
if [ "$OUT" = "" ]; then
    OUT="./build/debug"
fi

echo "OUT directory: " "$OUT"

for SRC in ./shaders/compute/**/*.glsl; do
    NAME=$(echo "$SRC" | sed "s/\.\/shaders\/compute\///; s/\//_/g; s/\.glsl$/\.spv/")

    echo "Compiling " "$NAME" ", stage: compute"
    glslc -g -I./shaders -fshader-stage=compute "$SRC" -o "$OUT/$NAME"
done

for SRC in ./shaders/vertex/**/*.glsl; do
    NAME=$(echo "$SRC" | sed "s/\.\/shaders\/vertex\///; s/\//_/g; s/\.glsl$/\.spv/")

    echo "Compiling " "$NAME" ", stage: vertex"
    glslc -I./shaders -fshader-stage=vertex "$SRC" -o "$OUT/$NAME"
done

for SRC in ./shaders/fragment/**/*.glsl; do
    NAME=$(echo "$SRC" | sed "s/\.\/shaders\/fragment\///; s/\//_/g; s/\.glsl$/\.spv/")

    echo "Compiling " "$NAME" ", stage: fragment"
    glslc -I./shaders -fshader-stage=fragment "$SRC" -o "$OUT/$NAME"
done
