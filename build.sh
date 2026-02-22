#!/bin/sh

gcc main.c -o cecup $(pkg-config --cflags --libs gtk+-3.0) -lpthread
./cecup
