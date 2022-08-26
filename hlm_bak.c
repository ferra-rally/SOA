//////TODO setup correct writing and reading


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
#include <linux/kobject.h> 
#include <linux/sysfs.h> 
#include<linux/proc_fs.h>
#include "lib/ioctl.h"

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniele Ferrarelli");


#define MODNAME "HLM"
#define B_SIZE 5
#define MAX_BLOCKS 3
#define MAX_BYTES B_SIZE * MAX_BLOCKS

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

int asleep_hi __attribute__((aligned(8)));
module_param(asleep_hi,int,0660);

int asleep_lo __attribute__((aligned(8)));
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

#define MINORS 128

typedef struct _object_state{
	int priority;
	unsigned long timeout;
	int block;
	int enabled;
	struct kobject *kobj;
} object_state;


object_state objects[MINORS];

struct element {
	struct element *next;
	char data[B_SIZE];
	int valid;
} list_node;

struct work_data {
    struct work_struct work;
    char data[B_SIZE];
};

struct element *head_hi = NULL;
struct element *tail_hi = NULL;

struct element *head_lo = NULL;
struct element *tail_lo = NULL;

int data_aval_hi = 0;
int data_aval_lo = 0;

int avail_space_hi = MAX_BYTES;
int alloc_blocks_hi = 0;

int w_pos = 0;

static void work_handler(struct work_struct *work){
    struct work_data * data = (struct work_data *)work;

    struct element *node = kmalloc(sizeof(struct element), GFP_KERNEL);

    memcpy(node->data, container_of((void*)data,struct work_data, work)->data, B_SIZE);
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
	char buffer[B_SIZE];
	int ret;
	int rem;
	int pending;
	struct element *node;
	int priority = objects[get_minor(filp)].priority;
	
	/*
	if(*off >= B_SIZE || len >= B_SIZE) {
		return -ENOSPC;
  	} 
  	*/
	
	//ret = copy_from_user(buffer,buff,len);

	if(priority) {
		spin_lock(&list_spinlock_hi);

		
		rem = B_SIZE - w_pos;
		pending = len;
		printk("%s: remaining space in block %d writing %d\n", MODNAME, rem, pending);
		//Enough space to write
		ret = copy_from_user((tail_hi->data + w_pos),buff,rem);
		w_pos += rem;
		pending -= rem;

		printk("%s: w_pos %d pending %d rem %d\n", MODNAME, w_pos, pending, rem);

		while(pending > 0) {
			//Not enough space for data
			printk("%s: allocating new block\n", MODNAME);
			node = kmalloc(sizeof(struct element), GFP_KERNEL);
			node -> next = NULL;

			//Add element
			tail_hi -> next = node;
			tail_hi = node;
			w_pos = 0;
			rem = B_SIZE;

			if(pending < rem) {
				rem = pending;
			}

			ret = copy_from_user(tail_hi->data + w_pos,buff,rem);
			pending -= rem;
			w_pos += rem;
			printk("%s: w_pos %d pending %d rem %d\n", MODNAME, w_pos, pending, rem);
		}

		data_aval_hi += len;

		return len;
		

		/*
		ret = copy_from_user(buffer, buff, len);
		memcpy(node->data, buffer, len);

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
	    */

	    wake_up(&wait_queue_hi);

	    spin_unlock(&list_spinlock_hi);
	} else {
		struct work_data * data;
		data = kmalloc(sizeof(struct work_data), GFP_KERNEL);
		memcpy(data->data, buffer, len);

		INIT_WORK(&data->work, work_handler);
		queue_work(wq, &data->work);
	}

	return len;
}

static ssize_t hlm_read(struct file *filp, char *buff, size_t len, loff_t *off) {
	int ret = 0;
	int lenght;

	int minor = get_minor(filp);
	int priority = objects[minor].priority;
	unsigned long timeout = objects[minor].timeout;
	int block = objects[minor].block;

	struct element **head;
	struct element *tmp;

	if(priority) {
		head = &head_hi;
	} else {
		head = &head_lo;
	}

	/*
	if(priority && block) {
		atomic_inc((atomic_t*)&asleep_hi);
		wait_event_interruptible_timeout(wait_queue_hi, data_aval_hi == 1, timeout);
        atomic_dec((atomic_t*)&asleep_hi);
        if(!data_aval_hi) {
	        printk("%s: %d exiting sleep for signal\n",MODNAME, current->pid);
	        return 0;
	    }
	} else if(!priority && block) {
		//Redundant condition but easier to read

		atomic_inc((atomic_t*)&asleep_lo);
		wait_event_interruptible_timeout(wait_queue_lo, data_aval_lo == 1, timeout);
        atomic_dec((atomic_t*)&asleep_lo);
        if(!data_aval_lo) {
	        printk("%s: %d exiting sleep for signal\n",MODNAME, current->pid);
	        return 0;
	    }
	}
	*/
	if(priority) {
		spin_lock(&list_spinlock_hi);
	} else {
		spin_lock(&list_spinlock_lo);
	}

	int to_read;

	if(bytes_hi > len) {
		printk("%s: Enough bytes\n", MODNAME);
		to_read = len;
	} else {
		printk("%s: Not Enough bytes\n", MODNAME);
		to_read = bytes_hi;
	}

	tmp = *head;

	if(priority) {
		bytes_hi -= to_read;
	} else {
		bytes_lo -= to_read;
	}


	printk("%s: reading %s\n"MODNAME, tmp->data);
	/*
	//Remaining in the current block
	int rem = B_SIZE - r_pos;
	int r;

	printk("%s: reading %d bytes\n", MODNAME, to_read);
	ret = copy_to_user(buff,tmp->data + r_pos, rem);
	to_read -= rem;
	r_pos += rem;

	printk("%s: to_read %d\n", MODNAME, to_read);

	
	while(to_read > 0) {
		// Remaining space in the block
		rem = B_SIZE - r_pos;
		if(to_read > rem) {
			// Data in the next block
			r = rem;
			ret = copy_to_user(buff,tmp->data + r_pos, rem);

			// Go to the next block
			tmp = tmp->next;
			if(tmp == NULL) {
				printk("%s: finished data\n", MODNAME);
				break;
			}

			r_pos = 0;
		} else {
			r = to_read;
			ret = copy_to_user(buff,tmp->data + r_pos, r);
			r_pos += r;
		}

		to_read -= r;
	}
	*/

	if(priority) {
		spin_unlock(&list_spinlock_hi);
	} else {
		spin_unlock(&list_spinlock_lo);
	}

	return len;
	/*

	//printk(KERN_INFO "HLM output: %s\n", (*head)->data);
	tmp = *head;
	*head = (*head)->next;

	if(*head == NULL) {
		if(priority) {
			data_aval_hi = 0;
		} else {
			data_aval_lo = 0;
		}
	}
	

	if(priority) {
		spin_unlock(&list_spinlock_hi);
	} else {
		spin_unlock(&list_spinlock_lo);
	}

	int l;
	if(len >= lenght) {
		l = lenght;
	} else {
		l = len;
	}
	
	lenght = strlen(tmp->data);
	printk("HLM:reading %s:%d\n", tmp->data, lenght);
	ret = copy_to_user(buff,tmp->data,l);
	kfree(tmp);
	return l;
	*/
	
	
	return 0;
}

static long hlm_ioctl(struct file *filp, unsigned int command, unsigned long param) {
  	int32_t value;
  	int ret;
  	int minor = get_minor(filp);
  	object_state *obj = objects + minor;

  	ret = copy_from_user(&value ,(int32_t*) param, sizeof(value));
  	if(ret != 0) {
  		printk("%s: error in ioctl\n", MODNAME);
  	}

  	printk("%s: ioctl called on minor %d  with command %d\n",MODNAME,get_minor(filp),command);

  	switch(command) {
        case CHG_PRT:
	        if(value != 0 && value != 1) {
		    	printk("%s: invalid priority %d\n",MODNAME,value);
		    	return -1;
		    } else {
		    	printk("%s: changed priority to %d\n",MODNAME,value);
		    	obj->priority = value;
		    }
		    break;

		case CHG_ENB_DIS:
			if(value != 0 && value != 1) {
		    	printk("%s: invalid value %d\n",MODNAME,value);
		    	return -1;
		    } else {
		    	
				obj->enabled = value;
				printk("%s: changing state to %d\n", MODNAME, value);
		    }
		    break;

		 case CHG_TIMEOUT:
		 	if(value <= 0) {
		 		printk("%s: invalid timeout value %d\n",MODNAME,value);
		 		return -1;
		 	} else {
		 		printk("%s: changing timeout to %d\n", MODNAME, value);
		 		obj->timeout = value;
		 	}
		 	break;

		 case CHG_BLK:
		 	if(value != 0 && value != 1) {
		 		printk("%s: invalid value %d\n",MODNAME,value);
		 		return -1;
		 	} else {
		 		printk("%s: changing blocking behaviour to %d\n", MODNAME, value);
		 		obj->block = value;
		 	}
		 	break;

        default:
            printk("%s: invalid ioctl command\n",MODNAME);
            return -1;
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

static ssize_t sysfs_show_statistics(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
	unsigned long out;

	if(!strcmp(attr->attr.name, "bytes_lo")) {
		out = bytes_lo;
	} else if(!strcmp(attr->attr.name, "bytes_hi")) {
		out = bytes_hi;
	} else if(!strcmp(attr->attr.name, "asleep_lo")) {
		out = asleep_lo;
	} else if(!strcmp(attr->attr.name, "asleep_hi")) {
		out = asleep_hi;
	}

    return sprintf(buf, "%lu", out);
}

static ssize_t sysfs_show(struct kobject *kobj, struct kobj_attribute *attr, char *buf) {
	unsigned long out;
	long num;
	object_state *obj;

	if(kstrtol(kobj->name, 10, &num)) {
		return sprintf(buf, "error");
	}

	obj = objects + num;
	
	if(!strcmp(attr->attr.name, "enabled")) {
		out = obj->enabled;
	} else if(!strcmp(attr->attr.name, "block")) {
		out = obj->block;
	} else if(!strcmp(attr->attr.name, "timeout")) {
		out = obj->timeout;
	} else if(!strcmp(attr->attr.name, "priority")) {
		out = obj->priority;
	}

	return sprintf(buf, "%lu", out);
}

static ssize_t sysfs_store(struct kobject *kobj, struct kobj_attribute *attr,const char *buf, size_t count) {
	unsigned long in;
    long num;
	object_state *obj;

	if(kstrtol(kobj->name, 10, &num)) {
		return 0;
	}

	obj = objects + num;
	
	sscanf(buf,"%lu",&in);

	if(!strcmp(attr->attr.name, "enabled")) {
		obj->enabled = in;
	} else if(!strcmp(attr->attr.name, "block")) {
		obj->block = in;
	} else if(!strcmp(attr->attr.name, "timeout")) {
		obj->timeout = in;
	} else if(!strcmp(attr->attr.name, "priority")) {
		obj->priority = in;
	}
    return count;
}

struct kobject *hlm_kobject;
struct kobject *devices_kobject;
struct kobj_attribute bytes_lo_attr = __ATTR(bytes_lo, 0660, sysfs_show_statistics, NULL);
struct kobj_attribute bytes_hi_attr = __ATTR(bytes_hi, 0660, sysfs_show_statistics, NULL);
struct kobj_attribute asleep_lo_attr = __ATTR(asleep_hi, 0660, sysfs_show_statistics, NULL);
struct kobj_attribute asleep_hi_attr = __ATTR(asleep_lo, 0660, sysfs_show_statistics, NULL);

struct kobj_attribute katr_enabled = __ATTR(enabled, 0660, sysfs_show, sysfs_store);
struct kobj_attribute katr_timeout = __ATTR(timeout, 0660, sysfs_show, sysfs_store);
struct kobj_attribute katr_block = __ATTR(block, 0660, sysfs_show, sysfs_store);
struct kobj_attribute katr_priority = __ATTR(priority, 0660, sysfs_show, sysfs_store);

int init_module(void) {
	int i;
	struct element *node;
	char name[5];

	printk("%s: Inserting module HLM\n", MODNAME);

	//Init flows
	node = kmalloc(sizeof(struct element), GFP_KERNEL);
	node->next = NULL;
	node->valid = 0;

	head_hi = node;
	tail_hi = node;

	node = kmalloc(sizeof(struct element), GFP_KERNEL);
	node->next = NULL;
	node->valid = 0;

	head_lo = node;
	tail_lo = node;


	// Create root kobject
	hlm_kobject = kobject_create_and_add("hlm",NULL);
 
	if(sysfs_create_file(hlm_kobject,&bytes_lo_attr.attr) ||
		sysfs_create_file(hlm_kobject,&bytes_hi_attr.attr) ||
		sysfs_create_file(hlm_kobject,&asleep_lo_attr.attr) ||
		sysfs_create_file(hlm_kobject,&asleep_hi_attr.attr)) {

		goto remove_sys;
	}

	devices_kobject = kobject_create_and_add("devices",hlm_kobject);
	
	//Enable all minors
	for(i=0;i<MINORS;i++){
		object_state *obj = objects + i;

		obj->enabled = 1;
		obj->timeout = 1000;
		obj->block = 1;
		obj->priority = 1; //TODO move to 1

		sprintf(name, "%d", i);

		obj->kobj = kobject_create_and_add(name, devices_kobject);
			if(sysfs_create_file(obj->kobj,&katr_enabled.attr) ||
			sysfs_create_file(obj->kobj,&katr_timeout.attr) ||
			sysfs_create_file(obj->kobj,&katr_priority.attr) ||
			sysfs_create_file(obj->kobj,&katr_block.attr)) {
			
		    goto remove_sys;
		}
	}

	Major = __register_chrdev(0, 0, 120, DEVICE_NAME, &fops);
	major_number = Major;

	if (Major < 0) {
	  printk("Registering hlm device failed\n");
	  goto remove_sys;
	}

	printk(KERN_INFO "Hlm device registered, it is assigned major number %d\n", Major);

	wq = create_singlethread_workqueue("hlm_wq");
	if(wq == 0) {
		printk(KERN_ERR "Work queue creation failed\n");
		goto remove_dev;
	}

    printk("%s: started\n",MODNAME);
	return 0;

remove_dev:
	unregister_chrdev(Major, DEVICE_NAME);
remove_sys:
	
	kobject_put(hlm_kobject);
	kobject_put(devices_kobject);
    printk(KERN_INFO"Cannot create sysfs files\n");
    sysfs_remove_file(hlm_kobject, &bytes_lo_attr.attr);
    sysfs_remove_file(hlm_kobject, &bytes_hi_attr.attr);
    sysfs_remove_file(hlm_kobject, &asleep_lo_attr.attr);
    sysfs_remove_file(hlm_kobject, &asleep_hi_attr.attr);
    for(i=0;i<MINORS;i++){
    	object_state *obj = objects + i;

    	kobject_put(obj->kobj);
    	sysfs_remove_file(obj->kobj,&katr_enabled.attr);
		sysfs_remove_file(obj->kobj,&katr_timeout.attr);
		sysfs_remove_file(obj->kobj,&katr_priority.attr);
		sysfs_remove_file(obj->kobj,&katr_block.attr);
    }

    return -1;
}

void cleanup_module(void) {

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

    kobject_put(hlm_kobject);
    kobject_put(devices_kobject);
    sysfs_remove_file(hlm_kobject, &bytes_lo_attr.attr);
    sysfs_remove_file(hlm_kobject, &bytes_hi_attr.attr);
    sysfs_remove_file(hlm_kobject, &asleep_lo_attr.attr);
    sysfs_remove_file(hlm_kobject, &asleep_hi_attr.attr);
    for(int i=0;i<MINORS;i++){
    	object_state *obj = objects + i;

    	kobject_put(obj->kobj);
    	sysfs_remove_file(obj->kobj,&katr_enabled.attr);
		sysfs_remove_file(obj->kobj,&katr_timeout.attr);
		sysfs_remove_file(obj->kobj,&katr_priority.attr);
		sysfs_remove_file(obj->kobj,&katr_block.attr);
    }

    printk("%s: Work queue destroyed\n", MODNAME);
}

