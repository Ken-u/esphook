#!/usr/bin/env bash
source "$(dirname "$0")/lib/ctest.sh"
ctest_run -I main tests/test_http_post.c main/http_post.c
