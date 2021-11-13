#!/bin/bash

if [[ ! -f "BigBuckBunny.mp4" ]]; then
   curl "http://commondatastorage.googleapis.com/gtv-videos-bucket/sample/BigBuckBunny.mp4" --output BigBuckBunny.mp4 
fi

gcc main.c `pkg-config --libs --cflags glib-2.0 raylib` -ldl -lm -lvlc -lpthread -o raylib-libvlc-example
