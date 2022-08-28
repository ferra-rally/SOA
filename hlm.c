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
#define MAX_BYTES 50

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

static int Major;            /* Major number assigned to broadcast device driver */
struct workqueue_struct *wq;	// Workqueue for async add

#define MINORS 128

struct element {
	struct element *next;
	int r_pos;
	int lenght;
	char *data;
} list_node;

struct work_data {
    struct work_struct work;
    char *data;
};

typedef struct _object_state{
	int priority;
	unsigned long timeout;
	int block;
	int r_pos;
	int enabled;
	struct kobject *kobj;
	int valid;
	struct element *head[2];
	struct element *tail[2];
	//struct mutex mux_lock_hi;
	//struct mutex mux_lock_lo;
	struct mutex mux_lock[2];
	wait_queue_head_t wq_w[2];
	wait_queue_head_t wq_r[2];
} object_state;

object_state objects[MINORS];

static void work_handler(struct work_struct *work){
    return;
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
  	return 0;
}


static int hlm_release(struct inode *inode, struct file *file) {

	printk("%s: hlm dev closed\n",MODNAME);
   	return 0;

}

static ssize_t hlm_write(struct file *filp, const char *buff, size_t len, loff_t *off) {
	int ret;
	int prt;
	int rem;
	int timeout;
	int block;
	struct element **head;
	struct element **tail; 
	struct element *node;

	int minor = get_minor(filp);
	object_state *obj = objects + minor;

	prt = obj->priority;
	head = &(obj->head[prt]);
	tail = &(obj->tail[prt]);
	timeout = obj->timeout;
	block = obj->block;

	
	printk("%s: priority %d write called\n", MODNAME, prt);

	if(prt) {
		mutex_lock(&(obj->mux_lock[prt]));
		
		if(*off >= MAX_BYTES) {
			mutex_unlock(&(obj->mux_lock[prt]));
			return -ENOSPC;
		} 

		/*if(*off > obj->valid) {
			mutex_unlock(&(obj->mux_lock_hi));
			return -ENOSR;
		} */
		
		/*
		if(obj->valid + len + *off > MAX_BYTES) {
			mutex_unlock(&(obj->mux_lock_hi));
			return -ENOSPC;
		}
		*/

		
		if(obj->valid + len > MAX_BYTES) {
			if(block) {
				mutex_unlock(&(obj->mux_lock[prt]));

				atomic_inc((atomic_t*)&asleep_hi);
				wait_event_interruptible_timeout(obj->wq_w[prt], obj->valid + len <= MAX_BYTES, timeout);
				atomic_dec((atomic_t*)&asleep_hi);

				mutex_lock(&(obj->mux_lock[prt]));
				if(obj->valid + len > MAX_BYTES) {
					return -ENOSPC;
				}
			} else {
				mutex_unlock(&(obj->mux_lock[prt]));
				return -ENOSPC;
			}
		}

		node = kmalloc(sizeof(struct element), GFP_KERNEL);
		node->next = NULL;
		node->data = kmalloc(len, GFP_KERNEL);
		node->r_pos = 0;
		node->lenght = len;

		ret = copy_from_user(node->data, buff, len);
		if(*tail == NULL) {
			*head = node;
			*tail = node;
			obj->r_pos = 0;

		} else {
			(*tail)->next = node;
			*tail = node;
		}

		obj->valid += len;
		wake_up(&(obj->wq_r[prt]));
		
		mutex_unlock(&(obj->mux_lock[prt]));

		return len - ret;
	} else {

	}

	return 0;

}

static ssize_t hlm_read(struct file *filp, char *buff, size_t len, loff_t *off) {
	int ret;
	int prt;
	int to_read;
	int x;
	int lenght;
	int block;
	int timeout;
	struct element **head;
	struct element **tail; 
	struct element *tmp;
	object_state *obj;
	int minor = get_minor(filp);

  	obj = objects + minor;
	prt = obj->priority;

	head = &(obj->head[prt]);
	tail = &(obj->tail[prt]);
	to_read = len;

	block = obj->block;
	timeout = obj->timeout;

	printk("HLM: req read of %ld with %d and timeout %d\n", len, block, timeout);

	mutex_lock(&(obj->mux_lock[prt]));

	//atomic_inc((atomic_t*)&asleep_hi);
	//wait_event_interruptible_timeout(wq_r[prt], data_aval_hi == 1, timeout);
    //atomic_dec((atomic_t*)&asleep_hi);

    if(to_read + *off > obj->valid && block) {
		printk("HLM: Not enough data\n");

		mutex_unlock(&(obj->mux_lock[prt]));

		atomic_inc((atomic_t*)&asleep_hi);
		wait_event_interruptible_timeout(obj->wq_r[prt], to_read + *off <= obj->valid, timeout);
		atomic_dec((atomic_t*)&asleep_hi);

		
		if(to_read + *off > obj->valid) {
			printk("HLM: waken up from signal, not enough data\n");
			//to_read = obj->valid;
			//return -1;
		}

		mutex_lock(&(obj->mux_lock[prt]));
	}

	//of = *off;
	obj->r_pos += *off;

	while(to_read > 0 && *head != NULL) {
		tmp = *head;

		//Available data in the block
		lenght = strlen(tmp->data) - obj->r_pos;
		printk("HLM: grr reading off: %lld len:%d r_pos:%d x:%d to_read: %d %s\n", *off, lenght, obj->r_pos ,x, to_read, tmp->data + obj->r_pos);
		/*
		if(obj->r_pos > lenght + obj->r_pos) {
			printk("HLM: skipped block\n");
			*head = tmp->next;
			obj->r_pos -= lenght;
			kfree(tmp->data);
			kfree(tmp);
		} else */if(to_read >= lenght) {
			printk("HLM: block read going to the next\n");
			x = lenght;

			*head = tmp->next;

			ret = copy_to_user(buff + (len - to_read), tmp->data + obj->r_pos, x);
			/*if(ret != x) {
				//Not all bytes were delivered
				printk("HLM: not all bytes delivered\n");
			}*/
			kfree(tmp->data);
			kfree(tmp);
			obj->r_pos = 0;
		} else {
			//Partial read
			x = to_read;
			ret = copy_to_user(buff + (len - to_read), tmp->data + obj->r_pos, x);
			/*
			if(ret != x) {
				//Not all bytes were delivered
				printk("HLM: not all bytes delivered\n");
			}*/

			obj->r_pos += x;
			printk("HLM: partial reading remaining data %s\n", tmp->data + obj->r_pos);
		}
		
		to_read -= x;
		obj->valid -= x;
	}

	// If queue is emptied reset tail
	if(*head == NULL) {
		printk("HLM: all data consumed\n");
		*tail = NULL;
	}

	mutex_unlock(&(obj->mux_lock[prt]));
	wake_up(&(obj->wq_w[prt]));

	return len - to_read;
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

	hlm_kobject = kobject_create_and_add("hlm",NULL);
	devices_kobject = kobject_create_and_add("devices",hlm_kobject);
	
	//Enable all minors
	for(i=0;i<MINORS;i++) {
		object_state *obj = objects + i;

		obj->enabled = 1;
		obj->timeout = 1000;
		obj->block = 1;
		obj->priority = 1; //TODO move to 0
		obj->valid = 0;
		obj->r_pos = 0;
		
		obj->head[0] = NULL;
		obj->head[1] = NULL;
		obj->tail[0] = NULL;
		obj->tail[1] = NULL;
		//mutex_init(&(obj->mux_lock_hi));
		//mutex_init(&(obj->mux_lock_lo));
		mutex_init(&(obj->mux_lock[0]));
		mutex_init(&(obj->mux_lock[1]));

		init_waitqueue_head(&(obj->wq_w[0]));
		init_waitqueue_head(&(obj->wq_w[1]));
		init_waitqueue_head(&(obj->wq_r[0]));
		init_waitqueue_head(&(obj->wq_r[1]));

		sprintf(name, "%d", i);

		obj->kobj = kobject_create_and_add(name, devices_kobject);
			if(sysfs_create_file(obj->kobj,&katr_enabled.attr) ||
			sysfs_create_file(obj->kobj,&katr_timeout.attr) ||
			sysfs_create_file(obj->kobj,&katr_priority.attr) ||
			sysfs_create_file(obj->kobj,&katr_block.attr)) {
			
		    goto remove_sys;
		}
	}

	Major = __register_chrdev(0, 0, MINORS, DEVICE_NAME, &fops);
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

	/*
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
	*/

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