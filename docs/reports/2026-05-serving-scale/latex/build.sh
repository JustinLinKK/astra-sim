#!/bin/bash
set -e

SCRIPT_DIR=$(dirname "$(realpath "$0")")
cd "${SCRIPT_DIR}"

mkdir -p build

if command -v latexmk >/dev/null 2>&1; then
  latexmk -pdf -interaction=nonstopmode -halt-on-error -output-directory=build main.tex
  exit 0
fi

if command -v pdflatex >/dev/null 2>&1; then
  pdflatex -interaction=nonstopmode -halt-on-error -output-directory=build main.tex
  pdflatex -interaction=nonstopmode -halt-on-error -output-directory=build main.tex
  exit 0
fi

echo "No LaTeX toolchain found. Install latexmk or pdflatex, then rerun:"
echo "  bash ${SCRIPT_DIR}/build.sh"
exit 1
