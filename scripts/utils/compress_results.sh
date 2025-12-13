#!/bin/bash

cd $1 || exit 1

for dir in */; do
    dir_name="${dir%/}"
    (
        tar -czf "${dir_name}.tar.gz" "$dir_name"

        if [ $? -eq 0 ]; then
            rm -rf "$dir_name"
            echo "Compressed and removed: $dir_name"
        else
            echo "Failed to compress: $dir_name"
        fi
    ) &
done

wait

echo "All directories have been processed."
