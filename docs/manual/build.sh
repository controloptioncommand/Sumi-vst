#!/usr/bin/env bash
# Пересобирает docs/Sumi-manual.pdf и картинки README: свежие скриншоты и спектрограммы из теста → нарезка → печать HTML в PDF.
# Нужны: Python 3 с Pillow, Node.js с Playwright. На Linux тест запускается через xvfb-run.
set -euo pipefail
cd "$(dirname "$0")/../.."

TEST=./build/SumiTest_artefacts/Release/SumiTest
SHOTS=$(mktemp -d)
trap 'rm -rf "$SHOTS"' EXIT

[ -d build ] || cmake -B build -DSUMI_BUILD_TESTS=ON
cmake --build build --target SumiTest
RUN=()
if [[ "$(uname)" == "Linux" ]]; then RUN=(xvfb-run -a); fi
"${RUN[@]}" "$TEST" "$SHOTS" | tail -1

python3 docs/manual/crop.py "$SHOTS" docs/manual/img docs/assets
NODE_PATH="${NODE_PATH:-$(npm root -g)}" node docs/manual/print.js docs/manual/Sumi-manual.html docs/Sumi-manual.pdf
echo "docs/Sumi-manual.pdf: $(python3 -c 'import os; print(os.path.getsize("docs/Sumi-manual.pdf") // 1024)') KB"
