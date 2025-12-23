#!/bin/bash

sudo bash -c 'source .venv/bin/activate; exec ./scripts/runtime/run.py "$@"' bash "$@"
