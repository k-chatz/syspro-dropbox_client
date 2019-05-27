#!/usr/bin/env bash

x=$(ps ux | grep 'dropbox_client -d' | awk 'NR==1 {print $2}') && kill -2 $x
