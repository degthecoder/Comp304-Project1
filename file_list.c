#include<linux/kernel.h>
#include<linux/init.h>
#include<linux/module.h>
#include<linux/kdev_t.h>
#include<linux/fs.h>
#include<linux/cdev.h>
#include<linux/device.h>
#include<linux/slab.h>
#include<linux/uaccess.h>
#include<linux/sched.h>

//This kernel is written with the help of the kernel module in first assignment.

#define SIZE 1024

//setting up the filename as the module parameter
static char *filename;
module_param(filename, charp, 0);


dev_t dev = 0;

static struct class *dev_class;
static struct cdev my_cdev;

static int __init my_driver_init(void);
static void __exit my_driver_exit(void);


static int __init my_driver_init(void)
{

	if((alloc_chrdev_region(&dev, 0, 1, "my_Dev")) < 0) {
		printk(KERN_INFO"Cannot allocate the major number...\n");
	}

	printk(KERN_INFO"Major = %d Minor =  %d..\n", MAJOR(dev),MINOR(dev));

	if((cdev_add(&my_cdev, dev, 1)) < 0) {
		printk(KERN_INFO "Cannot add the device to the system...\n");
		goto r_class;
	}	 

	if((dev_class =  class_create(THIS_MODULE, "my_class")) == NULL) {
		printk(KERN_INFO " cannot create the struct class...\n");
		goto r_class;
	}

	//creates the character device file
	if((device_create(dev_class, NULL, dev, NULL, filename)) == NULL) {
		printk(KERN_INFO " cannot create the device ..\n");
		goto r_device;
	}

	printk(KERN_INFO"Device driver insert...done properly...");
	
	//initialize necessary variables
	const char *currentName = NULL;
	struct dentry *dentryInf;
    struct dentry *currentDentry;
    struct file *file;
    printk(KERN_INFO "File Name kernel...\n");
	//open dev directory to get sibling files
    file = filp_open("/dev/", O_RDONLY, 0);
    dentryInf = file->f_path.dentry;
	//iterate through the directory
    list_for_each_entry(currentDentry, &dentryInf->d_subdirs, d_child) {
		//print the current name of the file
        currentName = currentDentry->d_name.name;
        printk(KERN_INFO "Name of the file: %s \n", currentName);
    }
    filp_close(file, NULL);
	return 0;

r_device: 
	class_destroy(dev_class);

r_class:
	unregister_chrdev_region(dev, 1);
	return -1;
}

void __exit my_driver_exit(void) {
	device_destroy(dev_class, dev);
	class_destroy(dev_class);
	cdev_del(&my_cdev);
	unregister_chrdev_region(dev, 1);
	printk(KERN_INFO "Device driver is removed successfully...\n");
}

module_init(my_driver_init);
module_exit(my_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BURAK YILDIRIM - DUHA EMIR GANIOGLU");
MODULE_DESCRIPTION("The character device driver for file list");

