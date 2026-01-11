#!/usr/bin/env bash
set -euo pipefail

VENV_DIR="${VENV_DIR:-xsql_venv}"

if [[ ! -d "${VENV_DIR}" ]]; then
  python3 -m venv "${VENV_DIR}"
fi

source "${VENV_DIR}/bin/activate"
python -m pip install -U pip
python -m pip install -e .[test]
