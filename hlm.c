/*
This specification is related to a Linux device driver implementing low and high priority flows of data. 
Through an open session to the device file a thread can read/write data segments. 
The data delivery follows a First-in-First-out policy along each of the two different data flows (low and high priority). 
r read operations, the read data disappear from the flow. Also, the high priority data flow must offer synchronous write operations while the low priority data flow must offer an asynchronous execution (based on delayed work) of write operations, while still keeping the interface able to synchronously notify the outcome. Read operations are all executed synchronously. The device driver should support 128 devices corresponding to the same amount of minor numbers.

The device driver should implement the support for the ioctl(..) service in order to manage the I/O session as follows:

    - setup of the priority level (high or low) for the operations V
    - blocking vs non-blocking read and write operations (wait_queue)
    - setup of a timeout regulating the awake of blocking operations (wait_interruptible with timer) 

A a few Linux module parameters and functions should be implemented in order to enable or disable the device file, in terms of a specific minor number. 
If it is disabled, any attempt to open a session should fail (but already open sessions will be still managed). 
Further additional parameters exposed via VFS should provide a picture of the current state of the device according to the following information:

    - enabled or disabled (use driver-concurrency.c example with banned minor numbers) V da provare
    - number of bytes currently present in the two flows (high vs low priority) (add) V+-
    - number of threads currently waiting for data along the two flows (high vs low priority) (atomic_inc??) 
SETUP OBJECT STATE WITH HI-LO, timeout, block non-block
*
*/


#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>	
#include <linux/pid.h>
#include <linux/version.h>
#include <linux/workqueue.h>
#include <linux/syscalls.h>
#include "lib/include/scth.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniele Ferrarelli");


#define MODNAME "HLM"
#define LINE_SIZE 30

static int hlm_open(struct inode *, struct file *);
static int hlm_release(struct inode *, struct file *);
static ssize_t hlm_write(struct file *, const char *, size_t, loff_t *);
static ssize_t hlm_read(struct file *filp, char *buff, size_t len, loff_t *off);

unsigned long bytes_hi __attribute__((aligned(8)));
module_param(bytes_hi,ulong,0660);

unsigned long bytes_lo __attribute__((aligned(8)));
module_param(bytes_lo,ulong,0660);

int major_number;
module_param(major_number,int,0660);

int asleep_hi;
module_param(asleep_hi,int,0660);

int asleep_lo;
module_param(asleep_lo,int,0660);

#define DEVICE_NAME "hlm"  /* Device file name in /dev/ - not mandatory  */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)      MAJOR(session->f_inode->i_rdev)
#define get_minor(session)      MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)      MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)      MINOR(session->f_dentry->d_inode->i_rdev)
#endif

DEFINE_SPINLOCK(list_spinlock_hi);
DEFINE_SPINLOCK(list_spinlock_lo);

DECLARE_WAIT_QUEUE_HEAD(wait_queue_hi);
DECLARE_WAIT_QUEUE_HEAD(wait_queue_lo);

static int Major;            /* Major number assigned to broadcast device driver */
struct workqueue_struct *wq;	// Workqueue for async add

#define MINORS 120

typedef struct _object_state{
	int priority;
	unsigned long timeout;
	int block;
	int enabled;
} object_state;


object_state objects[MINORS];

struct element {
	struct element *next;
	char data[30];
} list_node;

struct work_data {
    struct work_struct work;
    char data[30];
};

struct element *head_hi = NULL;
struct element *tail_hi = NULL;

struct element *head_lo = NULL;
struct element *tail_lo = NULL;

int data_aval_hi = 0;
int data_aval_lo = 0;

static void work_handler(struct work_struct *work){
    struct work_data * data = (struct work_data *)work;

    struct element *node = kmalloc(sizeof(struct element), GFP_KERNEL);

    //TODO temp
    memcpy(node->data, container_of((void*)data,struct work_data, work)->data, 30);
    node->next = NULL;

    spin_lock(&list_spinlock_lo);

    //atomic_inc((atomic_t*)&bytes);
    bytes_lo += strlen(node->data);

    if(head_lo == NULL) {
    	head_lo = node;
    	tail_lo = node;

    	data_aval_lo = 1;
    } else {
    	tail_lo->next = node;
    	tail_lo = node;
    }

    wake_up(&wait_queue_lo);
    spin_unlock(&list_spinlock_lo);

    printk(KERN_INFO "Added element\n");
    kfree(data);
}

static int hlm_open(struct inode *inode, struct file *file) {
	int minor;
	minor = get_minor(file);

	printk("%s: minor %d enabled: %d block: %d priority: %d timeout: %lu\n",MODNAME, minor, objects[minor].enabled, objects[minor].block, objects[minor].priority, objects[minor].timeout);
	if(0 == objects[minor].enabled){
		printk("%s: object with %d is disabled\n",MODNAME, minor);
		return -ENODEV;
	}

	printk("%s: hlm dev opened %d\n",MODNAME, minor);
	//device opened by a default nop
  	return 0;
}


static int hlm_release(struct inode *inode, struct file *file) {

	printk("%s: hlm dev closed\n",MODNAME);
	//device closed by default nop
   	return 0;

}


static ssize_t hlm_write(struct file *filp, const char *buff, size_t len, loff_t *off) {
	char buffer[30];
	int ret;
	int minor = get_minor(filp);
	int priority = objects[get_minor(filp)].priority;

	printk("%s: minor %d enabled: %d block: %d priority: %d timeout: %lu\n",MODNAME, minor, objects[minor].enabled, objects[minor].block, objects[minor].priority, objects[minor].timeout);
	printk("%s: write called with priority %d\n",MODNAME,priority);

	if(len >= LINE_SIZE) return -1;
	
	//ret = 
	ret = copy_from_user(buffer,buff,len);

	if(priority) {
		struct element *node = kmalloc(sizeof(struct element), GFP_KERNEL);

		memcpy(node->data, buffer, 30);
		node->next = NULL;

		spin_lock(&list_spinlock_hi);

		//Atomic?
		//atomic_inc((atomic_t*)&bytes);
		bytes_hi += len;

	    if(head_hi == NULL) {
	    	head_hi = node;
	    	tail_hi = node;

	    	data_aval_hi = 1;
	    } else {
	    	tail_hi->next = node;
	    	tail_hi = node;
	    }

	    wake_up(&wait_queue_hi);

	    spin_unlock(&list_spinlock_hi);
	} else {
		struct work_data * data;
		data = kmalloc(sizeof(struct work_data), GFP_KERNEL);
		memcpy(data->data, buffer, 30);

		INIT_WORK(&data->work, work_handler);
		queue_work(wq, &data->work);
	}

	return len;
}

static ssize_t hlm_read(struct file *filp, char *buff, size_t len, loff_t *off) {
	int ret = 0;

	int minor = get_minor(filp);
	int priority = objects[minor].priority;
	int timeout = objects[minor].timeout;
	int block = objects[minor].block;

	struct element **head;
	struct element *tmp;

	if(priority) {
		head = &head_hi;
	} else {
		head = &head_lo;
	}

	printk("%s: read called with priority %d\n",MODNAME,priority);

	if(priority && block) {
		atomic_inc((atomic_t*)&asleep_hi);
		wait_event_interruptible_timeout(wait_queue_hi, data_aval_hi == 1, timeout);
		if(data_aval_hi == 1){
                printk("%s: thread %d exiting sleep for signal\n",MODNAME, current->pid);
        }
        atomic_dec((atomic_t*)&asleep_hi);
	} else if(!priority && block) {
		//Redundant condition but easier to read

		atomic_inc((atomic_t*)&asleep_lo);
		wait_event_interruptible_timeout(wait_queue_lo, data_aval_lo == 1, timeout);
		if(data_aval_lo == 1){
                printk("%s: thread %d exiting sleep for signal\n",MODNAME, current->pid);
        }
        atomic_dec((atomic_t*)&asleep_lo);
	}

	if(*head != NULL) {
		if(priority) {
			spin_lock(&list_spinlock_hi);
		} else {
			spin_lock(&list_spinlock_lo);
		}

		if(*head != NULL) {
		printk(KERN_INFO "HLM output: %s\n", (*head)->data);
		tmp = *head;
		*head = (*head)->next;

		if(*head == NULL) {
			if(priority) {
				data_aval_hi = 0;
			} else {
				data_aval_lo = 0;
			}

			printk("%s: queue emptied hi: %d, lo: %d\n", MODNAME, data_aval_hi, data_aval_lo);
		}
			//TODO atomic?
			//atomic_dec((atomic_t*)&bytes);
			if(priority) {
				bytes_hi -= strlen(tmp->data);
			} else {
				bytes_lo -= strlen(tmp->data);
			}
		}

		if(priority) {
			spin_unlock(&list_spinlock_hi);
		} else {
			spin_unlock(&list_spinlock_lo);
		}

		ret = copy_to_user(buff,tmp->data,30);
		kfree(tmp);
		return len - ret;
	}
	
	return 0;
}

#define CHG_PRT 0
#define CHG_ENB_DIS 1
#define CHG_TO 2
#define CHG_BLK 3

static long hlm_ioctl(struct file *filp, unsigned int command, unsigned long param) {
  	int32_t value;
  	int minor = get_minor(filp);
  	object_state *obj = objects + minor;

  	copy_from_user(&value ,(int32_t*) param, sizeof(value));
  	//object_state *the_object;

  	//the_object = objects + minor;

  	printk("%s: somebody called an ioctl on dev with [major,minor] number [%d,%d] and command %u \n",MODNAME,get_major(filp),get_minor(filp),command);

  	switch(command) {
        case CHG_PRT:
	        if(value != 0 && value != 1) {
		    	printk("%s: invalid priority %d\n",MODNAME,value);
		    } else {
		    	printk("%s: changed priority to %d\n",MODNAME,value);
		    	obj->priority = value;
		    }
		    break;

		case CHG_ENB_DIS:
			if(value != 0 && value != 1) {
		    	printk("%s: invalid value %d\n",MODNAME,value);
		    } else {
		    	
				obj->enabled = value;
				printk("%s: changing state to %d\n", MODNAME, value);
		    }
		    break;

		 case CHG_TO:
		 	if(value <= 0) {
		 		printk("%s: invalid timeout value %d\n",MODNAME,value);
		 	} else {
		 		printk("%s: changing timeout to %d\n", MODNAME, value);
		 		obj->timeout = value;
		 	}
		 	break;

		 case CHG_BLK:
		 	if(value <= 0) {
		 		printk("%s: invalid value %d\n",MODNAME,value);
		 	} else {
		 		printk("%s: changing blocking behaviour to %d\n", MODNAME, value);
		 		obj->block = value;
		 	}
		 	break;

        default:
            printk("%s: invalid command\n",MODNAME);
            break;
     }

  return 0;

}

static struct file_operations fops = {
  .owner = THIS_MODULE,
  .write = hlm_write,
  .read = hlm_read,
  .open =  hlm_open,
  .unlocked_ioctl = hlm_ioctl,
  .release = hlm_release
};

int init_module(void)
{
	int i;

	//Enable all minors
	for(i=0;i<MINORS;i++){
		object_state *obj = objects + i;

		obj->enabled = 1;
		obj->timeout = 1000;
		obj->block = 1;
		obj->priority = 0;
	}

	Major = __register_chrdev(0, 0, 120, DEVICE_NAME, &fops);
	major_number = Major;

	if (Major < 0) {
	  printk("Registering hlm device failed\n");
	  return Major;
	}

	printk(KERN_INFO "Hlm device registered, it is assigned major number %d\n", Major);

	wq = create_singlethread_workqueue("hlm_wq");
	if(wq == 0) {
		printk(KERN_ERR "Work queue creation failed\n");
	}

    printk("%s: started\n",MODNAME);

	return 0;
}

void cleanup_module(void)
{

	unregister_chrdev(Major, DEVICE_NAME);
	printk(KERN_INFO "Hlm device unregistered, it was assigned major number %d\n", Major);

	flush_workqueue(wq);

	// Empty queue
	while(head_hi != NULL) {
		struct element *tmp = head_hi;
		head_hi = head_hi->next;
		kfree(tmp);
	}

	while(head_lo != NULL) {
		struct element *tmp = head_lo;
		head_lo = head_lo->next;
		kfree(tmp);
	}

    destroy_workqueue(wq);
    printk(KERN_INFO "Work queue destroyied\n");
}

