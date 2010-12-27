#!/bin/bash
make
./buddhabrot
convert buddhabrot.tiff buddhabrot.png
open buddhabrot.png