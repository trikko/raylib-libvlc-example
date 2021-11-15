#!/bin/bash
gcc main.c `pkg-config --libs --cflags glib-2.0 raylib` -ldl -lm -lvlc -lpthread -o raylib-libvlc-example
