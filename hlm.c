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
#define BLOCK_MAX_SIZE 10

static int hlm_open(struct inode *, struct file *);
static int hlm_release(struct inode *, struct file *);
static ssize_t hlm_write(struct file *, const char *, size_t, loff_t *);
static ssize_t hlm_read(struct file *filp, char *buff, size_t len, loff_t *off);

int major_number;
module_param(major_number,int,0660);

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
	int len;
	char *data;
} list_node;

struct work_data {
    struct work_struct work;
    int minor;
    int len;
    char *data;
};

typedef struct _object_state{
	int priority;
	int asleep[2];
	unsigned long timeout;
	int block;
	int r_pos[2];
	int enabled;
	struct kobject *kobj;
	unsigned long valid[2];
	unsigned long pending;
	struct element *head[2];
	struct element *tail[2];
	struct workqueue_struct *work_queue;
	struct mutex mux_lock[2];
	wait_queue_head_t wq_w[2];
	wait_queue_head_t wq_r[2];
} object_state;

object_state objects[MINORS];

void enqueue(object_state *obj, int ptr, struct element *node) {
	struct element **head;
	struct element **tail; 

	head = &(obj->head[0]);
	tail = &(obj->tail[0]);

	if(*head == NULL) {
		*head = node;
		*tail = node;
		obj->r_pos[ptr] = 0;
	} else {
		(*tail)->next = node;
		*tail = node;
	}
}

int minimum(int a, int b) {
	if(a < b) {
		return a;
	} else {
		return b;
	}
}

static void work_handler(struct work_struct *work_elem){
	struct element **head;
	struct element **tail; 
	struct element *node;
	int timeout;
	int min;
	int to_write;
	int len;
	struct work_data *wd = container_of((void*)work_elem,struct work_data, work);
	int minor = wd->minor;
	
	object_state *obj = objects + minor;

	timeout = obj->timeout;
	len = wd->len;
	to_write = len;

	printk("HLM: delayed work minor-%d\n", wd->minor);
	mutex_lock(&(obj->mux_lock[0]));

	head = &(obj->head[0]);
	tail = &(obj->tail[0]);

	//Fragment block
	while(to_write > 0) {
		min = minimum(to_write, BLOCK_MAX_SIZE);
		node = kmalloc(sizeof(struct element), GFP_KERNEL);
		node->len = min;
		if(node == NULL) {
			printk("%s: problem when allocating memory\n", MODNAME);
			return;
		}

		node->next = NULL;
		node->data = kmalloc(min, GFP_KERNEL);
		if(node == NULL) {
			printk("%s: problem when allocating memory\n", MODNAME);
			return;
		}

		memcpy(node->data, wd->data + (len - to_write), min);
		enqueue(obj, 0, node);

		to_write -= min;

		obj->valid[0] += min;
		obj->pending -= min;
	}

	wake_up(&(obj->wq_r[0]));
	mutex_unlock(&(obj->mux_lock[0]));

	kfree(wd->data);
	kfree(work_elem);
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
	int timeout;
	int block;
	int min;
	int to_write;
	struct work_data * data;
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
		to_write = len;
		mutex_lock(&(obj->mux_lock[prt]));
		
		if(*off >= MAX_BYTES) {
			mutex_unlock(&(obj->mux_lock[prt]));
			return -ENOSPC;
		} 

		if(block) {
			mutex_unlock(&(obj->mux_lock[prt]));

			atomic_inc((atomic_t*)&(obj->asleep[prt]));
			wait_event_interruptible_timeout(obj->wq_w[prt], obj->valid[prt] + len <= MAX_BYTES, timeout);
			atomic_dec((atomic_t*)&(obj->asleep[prt]));

			mutex_lock(&(obj->mux_lock[prt]));
			if(obj->valid[prt] + len > MAX_BYTES) {
				mutex_unlock(&(obj->mux_lock[prt]));
				return -ENOSPC;
			}
		} else if(obj->valid[prt] + len > MAX_BYTES) {
			printk("HLM: no space %ld-%d\n", obj->valid[prt] + len, MAX_BYTES);
			mutex_unlock(&(obj->mux_lock[prt]));
			return -ENOSPC;
		}

		//ret = add_block(obj, buff, len, 1);

		while(to_write > 0) {
			printk("HLM: adding block\n");
			min = minimum(to_write, BLOCK_MAX_SIZE);
			node = kmalloc(sizeof(struct element), GFP_KERNEL);
			if(node == NULL) {
				printk("%s: problem when allocating memory\n", MODNAME);
				return -ENOMEM;
			}
			node->next = NULL;
			node->data = kmalloc(min, GFP_KERNEL);
			node->len = min;
			if(node == NULL) {
				printk("%s: problem when allocating memory\n", MODNAME);
				return -ENOMEM;
			}

			ret = copy_from_user(node->data, buff + (len - to_write), min);
			enqueue(obj, 1, node);
			to_write -= min;
		}

		obj->valid[prt] += len;
		
		wake_up(&(obj->wq_r[prt]));
		mutex_unlock(&(obj->mux_lock[prt]));

		return len - ret;
	} else {

		mutex_lock(&(obj->mux_lock[prt]));
		if(obj->valid[prt] + obj->pending + len > MAX_BYTES) {
			if(block) {
				mutex_unlock(&(obj->mux_lock[prt]));

				atomic_inc((atomic_t*)&(obj->asleep[prt]));
				wait_event_interruptible_timeout(obj->wq_w[prt], obj->valid[prt] + obj->pending + len <= MAX_BYTES, timeout);
				atomic_dec((atomic_t*)&(obj->asleep[prt]));
				if(obj->valid[prt] + obj->pending + len > MAX_BYTES) {
					return -ENOSPC;
				}

				mutex_lock(&(obj->mux_lock[prt]));
			} else {
				printk("HLM: no space %ld-%d\n", obj->valid[prt] + obj->pending + len, MAX_BYTES);
				mutex_unlock(&(obj->mux_lock[prt]));
				return -ENOSPC;
			}
		}

		data = kmalloc(sizeof(struct work_data), GFP_KERNEL);
		if(data == NULL) {
			printk("%s: problem when allocating memory\n", MODNAME);
			return -ENOMEM;
		}

		data->data = kmalloc(len, GFP_KERNEL);
		data->len = len;
		if(data->data == NULL) {
			printk("%s: problem when allocating memory\n", MODNAME);
			return -ENOMEM;
		}

		ret = copy_from_user(data->data, buff, len);
		if(ret != 0) {
			printk("HLM: failed to write in prt 0\n");
			return -1;
		}

		data->minor = minor;

		INIT_WORK(&data->work, work_handler);
		obj->pending += len;
		queue_work(wq, &data->work);
		mutex_unlock(&(obj->mux_lock[prt]));

		return len - ret;
	}
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
	int read = 0;
	int minor = get_minor(filp);

  	obj = objects + minor;
	prt = obj->priority;

	head = &(obj->head[prt]);
	tail = &(obj->tail[prt]);
	to_read = len;

	block = obj->block;
	timeout = obj->timeout;

	printk("HLM: req read of %ld with %d and timeout %d valid %ld\n", len, block, timeout, obj->valid[prt]);

    //if(to_read + *off > obj->valid[prt] && block) {
	if(block) {
		atomic_inc((atomic_t*)&(obj->asleep[prt]));
		printk("HLM: sleeping\n");
		wait_event_interruptible_timeout(obj->wq_r[prt], to_read + *off <= obj->valid[prt], timeout);
		atomic_dec((atomic_t*)&(obj->asleep[prt]));

		/*
		if(to_read + *off > obj->valid[prt]) {
			printk("HLM: waken up from signal, not enough data\n");
			//to_read = obj->valid;
			//return -1;
		}
		*/
	}

	mutex_lock(&(obj->mux_lock[prt]));

	obj->r_pos[prt] += *off;

	while(to_read > 0 && *head != NULL) {
		tmp = *head;

		//Available data in the block
		lenght = tmp->len - obj->r_pos[prt];
		printk("HLM: grr reading off: %lld len:%d r_pos:%d to_read: %d %s\n", *off, lenght, obj->r_pos[prt] , to_read, tmp->data + obj->r_pos[prt]);
		if(to_read >= lenght) {
			x = lenght;

			*head = tmp->next;

			ret = copy_to_user(buff + (len - to_read), tmp->data + obj->r_pos[prt], x);
			if(ret != 0) {
				//Not all bytes were delivered reset reading position
				obj->r_pos[prt] = lenght - ret;
				break;
			} else {
				//All bytes were read, block can be freed
				kfree(tmp->data);
				kfree(tmp);
				obj->r_pos[prt] = 0;
			}
		} else {
			//Partial read
			x = to_read;
			ret = copy_to_user(buff + (len - to_read), tmp->data + obj->r_pos[prt], x);
			if(ret != 0) {
				//Not all bytes were delivered
				obj->r_pos[prt] -= ret;
				break;
			}

			obj->r_pos[prt] += x;
		}
		
		to_read -= x;
		read += x;
	}

	obj->valid[prt] -= read;

	printk("HLM valid %lu - %d\n", obj->valid[prt],read);

	mutex_unlock(&(obj->mux_lock[prt]));
	wake_up(&(obj->wq_w[prt]));

	printk("HLM: read ret %d\n", read);
	return read;
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
	} else if(!strcmp(attr->attr.name, "asleep_hi")) {
		out = obj->asleep[1];
	} else if(!strcmp(attr->attr.name, "asleep_lo")) {
		out = obj->asleep[0];
	} else if(!strcmp(attr->attr.name, "bytes_hi")) {
		out = obj->valid[1];
	} else if(!strcmp(attr->attr.name, "bytes_lo")) {
		out = obj->valid[0];
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
struct kobj_attribute bytes_lo_attr = __ATTR(bytes_lo, 0660, sysfs_show, NULL);
struct kobj_attribute bytes_hi_attr = __ATTR(bytes_hi, 0660, sysfs_show, NULL);
struct kobj_attribute asleep_lo_attr = __ATTR(asleep_hi, 0660, sysfs_show, NULL);
struct kobj_attribute asleep_hi_attr = __ATTR(asleep_lo, 0660, sysfs_show, NULL);

struct kobj_attribute katr_enabled = __ATTR(enabled, 0660, sysfs_show, sysfs_store);
struct kobj_attribute katr_timeout = __ATTR(timeout, 0660, sysfs_show, sysfs_store);
struct kobj_attribute katr_block = __ATTR(block, 0660, sysfs_show, sysfs_store);
struct kobj_attribute katr_priority = __ATTR(priority, 0660, sysfs_show, sysfs_store);

int init_module(void) {
	int i;
	char name[5];

	printk("%s: Inserting module HLM\n", MODNAME);

	hlm_kobject = kobject_create_and_add("hlm",NULL);
	
	//Enable all minors
	for(i=0;i<MINORS;i++) {
		object_state *obj = objects + i;

		sprintf(name, "%d", i);

		for(int j = 0; j < 2; j++) {
			obj->valid[j] = 0;
			obj->r_pos[j] = 0;
			
			obj->head[j] = NULL;
			obj->tail[j] = NULL;

			mutex_init(&(obj->mux_lock[j]));

			init_waitqueue_head(&(obj->wq_w[j]));
			init_waitqueue_head(&(obj->wq_r[j]));
		}


		obj->pending = 0;
		obj->enabled = 1;
		obj->timeout = 1000;
		obj->block = 0;
		obj->priority = 0;

		obj->work_queue = create_singlethread_workqueue(name);
		if(obj->work_queue == 0) {
			printk(KERN_ERR "Work queue creation failed\n");
			goto remove_dev;
		}

		sprintf(name, "%d", i);

		obj->kobj = kobject_create_and_add(name, hlm_kobject);
		if(sysfs_create_file(obj->kobj,&katr_enabled.attr) ||
			sysfs_create_file(obj->kobj,&katr_timeout.attr) ||
			sysfs_create_file(obj->kobj,&katr_priority.attr) ||
			sysfs_create_file(obj->kobj,&katr_block.attr) ||
			sysfs_create_file(obj->kobj,&bytes_lo_attr.attr) ||
			sysfs_create_file(obj->kobj,&bytes_hi_attr.attr) ||
			sysfs_create_file(obj->kobj,&asleep_hi_attr.attr) ||
			sysfs_create_file(obj->kobj,&asleep_lo_attr.attr)) {
			
			printk("%s: error during creation of sysfs files\n", MODNAME);
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
	struct element *node;
	unregister_chrdev(Major, DEVICE_NAME);
	printk(KERN_INFO "Hlm device unregistered, it was assigned major number %d\n", Major);

	for(int i = 0; i < MINORS; i++) {
		object_state *obj = objects + i;
		flush_workqueue(obj->work_queue);

		// Empty queue
		for(int j = 0; j < 2; j++) {
			node = obj->head[j];
			while(node != NULL) {
				struct element *tmp = node;
				node = node->next;
				kfree(tmp->data);
				kfree(tmp);
			}
		}

		destroy_workqueue(obj->work_queue);
	}

    kobject_put(hlm_kobject);
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