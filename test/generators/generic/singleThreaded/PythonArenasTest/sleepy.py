# Copyright (c) 2020 Broadcom. All Rights Reserved.
# The term "Broadcom" refers to Broadcom Inc. and/or its subsidiaries.
# SPDX-License-Identifier: GPL-2.0
# It is interesting to see all the allocations caused by this rather tiny
# test, and how they very between various releases of cpython.
import time

time.sleep(3600)
