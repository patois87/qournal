#!/bin/sh
# Runs tests/pdfcorpus for every PDF file of a folder, one at a time, and prints one line per file.
# A crash or a hang (more than 60 seconds) is reported for that file.
#
#     tools/check_pdf_corpus.sh <build folder> <folder with PDF files>
BUILD=${1:?build folder}
FOLDER=${2:?folder with PDF files}
for file in "$FOLDER"/*.pdf; do
    output=$(QT_QPA_PLATFORM=offscreen timeout 60 "$BUILD/tests/pdfcorpus" "$file" 2>/dev/null)
    status=$?
    if [ $status -eq 124 ]; then
        printf '%s\tHANG\n' "$file"
    elif [ $status -ne 0 ]; then
        printf '%s\tCRASH(%s)\n' "$file" "$status"
    else
        printf '%s\n' "$output"
    fi
done
