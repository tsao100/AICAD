#!/bin/bash
# Resize all PNGs in the current folder to 32x32 and save them to the parent folder

mkdir -p ../

for f in ./*.png; do
    [ -e "$f" ] || continue  # skip if no files
    base=$(basename "$f")
    convert "$f" -resize 32x32\! "../$base"
done

echo "✅ All PNGs resized to 32x32 and saved to ../"
