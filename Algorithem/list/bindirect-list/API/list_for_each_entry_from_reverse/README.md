list_for_each_entry_from_reverse [中文教程](https://biscuitos.github.io/blog/LIST_list_for_each_entry_from_reverse/)
----------------------------------

```
bidirect-list

+-----------+<--o    +-----------+<--o    +-----------+<--o    +-----------+
|           |   |    |           |   |    |           |   |    |           |
|      prev |   o----| prev      |   o----| prev      |   o----| prev      |
| list_head |        | list_head |        | list_head |        | list_head |
|      next |---o    |      next |---o    |      next |---o    |      next |
|           |   |    |           |   |    |           |   |    |           |
+-----------+   o--->+-----------+   o--->+-----------+   o--->+-----------+
```

Iterate backwards over list of given type from the current point.

Context:

* Driver Files: list.c

## Usage

Copy Driver Files into `/drivers/xxx/`, and modify Makefile on current 
directory, as follow:

```
obj-$(CONFIG_XX) += list.o
```

Then, compile driver and dts. Details :

```
make
```

## Running

Packing image and runing on the target board.
