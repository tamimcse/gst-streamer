hellomake: main.c
	gcc -Wall main.c -o streamer $(shell pkg-config --cflags --libs gstreamer-1.0)


