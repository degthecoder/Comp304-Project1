obj-m += ps_traverse.o file_list.o
all:
		make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
clean:
		make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean