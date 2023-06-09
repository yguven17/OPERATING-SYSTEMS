#include <linux/init.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/pid.h>
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/unistd.h>
#include <linux/proc_fs.h>


// Meta Information
MODULE_LICENSE("GPL");
MODULE_AUTHOR("bisey");
MODULE_DESCRIPTION("A simple module to print the process tree of a given PID"); // Description of the module

int pid; // PID of the process to be monitored
module_param(pid, int, S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH); // Set the PID of the process to be monitored
MODULE_PARM_DESC(pid, "PID of the process"); // Description of the PID parameter


void print_dots(int depth){ // Print the dots
	int i; // Loop variable
	for(i = 0; i < depth; i++){ // Print the depth of the process
		printk(KERN_CONT "*****");
		if (i == depth-1 ){	// Print the arrow
			printk(KERN_CONT "---->");
		}
	}
}
void print_process_tree(struct task_struct* ts, int depth){ // Traverse the process tree
	struct list_head *list_; // List of children
	struct task_struct *child; // Child process
	print_dots(depth); // Print the dots
	printk(KERN_CONT "%d - PID: %d (start time: %lld)\n", depth, ts->pid, ts->start_time); // Print the PID and start time of the process

	list_for_each(list, &ts->children){ // Traverse the children of the process
		child = list_entry(list, struct task_struct, sibling); // Get the child process
		print_process_tree(child, depth+1); // Traverse the child process
	}

}


int psvis_start(void){ // Start the module
	struct task_struct *ts; // Task struct of the process to be monitored
	ts = get_pid_task(find_get_pid(pid), PIDTYPE_PID); // Get the task struct of the process to be monitored
	if (ts != NULL){ // If the process exists
		print_process_tree(ts, 0); // Traverse the process tree
	}
	return 0; // Return 0
}

void psvis_finish(void){ // Finish the module
	pid = 0; // Reset the PID value
	printk("FINISHED psvis\n", ); // Print the finish message
}



module_init(psvis_start);
module_exit(psvis_finish);