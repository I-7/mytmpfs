build:
	gcc -Wall src/mytmpfs.c src/stat_tree.c -I include `pkg-config fuse3 --cflags --libs` -o mytmpfs

build-debug:
	gcc -Wall src/mytmpfs.c src/stat_tree.c -I include -D DEBUG `pkg-config fuse3 --cflags --libs` -o mytmpfs

mount:
	./mytmpfs dir

umount:
	fusermount -u dir
