build:
	g++ -Wall src/mytmpfs.cpp src/stat_tree.cpp -I include `pkg-config fuse3 --cflags --libs` -o mytmpfs

build-debug:
	g++ -Wall src/mytmpfs.cpp src/stat_tree.cpp -I include -DDEBUG `pkg-config fuse3 --cflags --libs` -o mytmpfs

mount:
	./mytmpfs dir

umount:
	fusermount -u dir
