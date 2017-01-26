hellomake: main.c
	gcc -Wall main.c -o ../Linux/streamer $(shell pkg-config --cflags --libs gstreamer-1.0 gio-2.0) -lm 


