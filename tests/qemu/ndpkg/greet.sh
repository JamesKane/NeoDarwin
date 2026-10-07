#!/bin/sh
# SPDX-License-Identifier: BSD-2-Clause
# The test package greet (P2-02, tests/qemu/ndpkg): prints the greeting its
# dependency, libgreet, installs.
echo "greet: $(cat /usr/local/share/libgreet/greeting)"
