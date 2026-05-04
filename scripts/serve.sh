#!/bin/bash
cd "$(dirname "$0")/../website/build"
python3 -m http.server 8080