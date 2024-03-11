# mytmpfs

Create a directory for mounting:

```
mkdir dir
```

Build the project:

```
make build
```

To mount using directory named `dir`, use

```
make mount
```

or use

```
./mytmpfs <directory_name>
```

for the other mounting point.

To unmount using directory named `dir`, use

```
make umount
```

or use

```
fusermount -u <directory_name>
```

for the other mounting point.

