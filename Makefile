build:
	g++ -Wall src/mytmpfs.cpp src/stat_tree.cpp `pkg-config fuse3 --cflags --libs` -I include -o mytmpfs

build-debug:
	g++ -Wall src/mytmpfs.cpp src/stat_tree.cpp -DDEBUG `pkg-config fuse3 --cflags --libs` -I include -o mytmpfs

mount:
	./mytmpfs dir

umount:
	fusermount -u dir
