#!/bin/bash
make
./buddhabrot
convert mandelbrot.tiff mandelbrot.png
open mandelbrot.png