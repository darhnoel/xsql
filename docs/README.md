# Docs: Syntax Diagrams

This folder contains auto-generated railroad diagrams for the XSQL grammar.

How to generate:
1. Install the docs dependencies:
   `python3 -m pip install -r docs/requirements.txt`
2. Run the generator:
   `python3 docs/generate_diagrams.py`
   (or `./docs/build_diagrams.sh`)

Output:
- SVGs are written to `docs/diagrams/`.
- The top-level README links to these SVGs.
