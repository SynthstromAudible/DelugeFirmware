#!/usr/bin/env bash

find src \
  \( -name '*.[ch]pp' -or -name '*.[ch]' \) \
  -exec echo [+] Format {} \; \
  -exec clang-format --style=file -i {} \;
