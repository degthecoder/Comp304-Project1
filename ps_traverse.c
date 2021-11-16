#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/proc_fs.h>

//Initalize flag and pid module parameters
static char *flag;
int pid = 0;
module_param(pid,int,0);
module_param(flag,charp,0);

//DFS function
void DFS(struct task_struct *task)
{   
    struct task_struct *child;
    struct list_head *list;

    printk("Name: %s, PID: %d\n", task->comm, task->pid);
    //Iterate through the list
    list_for_each(list, &task->children) {
        if(list_empty(&(task->children))){
            printk(KERN_INFO "Error");
        }else{
            //get the child in the list entry
            child = list_entry(list, struct task_struct, sibling);
            //make recursive callback to the function
            DFS(child);
        }
        
    }
}

//BFS function
void BFS(struct task_struct *task)
{   
    struct task_struct *child;
    struct list_head *list;

    printk("Name: %s, PID: %d\n", task->comm, task->pid);
    list_for_each(list, &task->children) {
            if(list_empty(&(task->children))){
                printk(KERN_INFO "Error");
            }else{
                //make recursive callback to the function
                BFS(child);
                child = list_entry(list, struct task_struct, sibling);
            }
            
    }
}

int my_driver_init(void)
{
    //Print parameters and loading information
    printk(KERN_INFO "Loading PS Traverse Module...\n");
    printk(KERN_INFO "Root PID: %d\n",pid);
    printk(KERN_INFO "Flag: %s\n",flag);
    struct pid *pid_struct;
	struct task_struct *task;
	
    //get task by using root pid
	pid_struct = find_get_pid(pid);
	task = pid_task(pid_struct, PIDTYPE_PID);
    if(task == NULL) {
			printk(KERN_ERR "PID doesn't exist\n");
	}else{
        //checking the flag
        if(strcmp(flag,"-d")==0){
            DFS(task);
        }else if(strcmp(flag,"-b")==0){
            BFS(task);
        }
    }
    
    return 0;
}


void my_driver_exit(void)
{
    printk(KERN_INFO "Removing PS Traverse Module...\n");
}


module_init(my_driver_init);
module_exit(my_driver_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("BURAK YILDIRIM - DUHA EMIR GANIOGLU");
MODULE_DESCRIPTION("The driver");