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

static int hlm_open(struct inode *, struct file *);
static int hlm_release(struct inode *, struct file *);
static ssize_t hlm_write(struct file *, const char *, size_t, loff_t *);
static ssize_t hlm_read(struct file *filp, char *buff, size_t len, loff_t *off);

int major_number;
module_param(major_number,int,0660);

unsigned long max_bytes = 500;
module_param(max_bytes,ulong,0660);

int block_max_size = 50;
module_param(block_max_size,int,0660);

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

//Linked list node
struct element {
	struct element *next;
	int len;
	char *data;
} list_node;

//Structs that stores a series of nodes
struct fragmented_data {
	struct element *head;
	struct element *tail;
};

struct work_data {
    struct work_struct work;
    int minor;
    int len;
    struct fragmented_data *data;
};


//Struct that stores the state of the device
typedef struct _object_state{
	//Current priority
	int priority;
	//Number of thread sleeping
	int asleep[2];
	//Current timeout
	unsigned long timeout;
	//If reading/writing can block
	int block;
	//Current read position in the head block
	int r_pos[2];
	//If the object is enabled
	int enabled;
	//Stores the kernel object that displays statistics
	struct kobject *kobj;
	//Number of valid bytes in the system
	unsigned long valid[2];
	//Number of bytes pending write in the work queue
	unsigned long pending;
	//Stores the head of the 2 flows
	struct element *head[2];
	//Stores the tail of the 2 flows
	struct element *tail[2];
	//Single thraed work queue for every device
	struct workqueue_struct *work_queue;
	//Lock used for syncronization, 1 for each priority
	struct mutex mux_lock[2];
	//Wait queues for writing and reading threads
	wait_queue_head_t wq_w;
	wait_queue_head_t wq_r;
} object_state;

object_state objects[MINORS];

int minimum(int a, int b) {
	if(a < b) {
		return a;
	} else {
		return b;
	}
}

//Function that add a series of nodes to the queue
void enqueue(object_state *obj, int ptr, struct fragmented_data *data) {
	struct element **head;
	struct element **tail; 

	head = &(obj->head[ptr]);
	tail = &(obj->tail[ptr]);

	if(*head == NULL) {
		//If the queue was empty reset reading position
		*head = data->head;
		*tail = data->tail;
		obj->r_pos[ptr] = 0;
	} else {
		(*tail)->next = data->head;
		*tail = data->tail;
	}
}

//Function that is called to do the delayed work
static void work_handler(struct work_struct *work_elem){
	int timeout;
	int len;
	struct work_data *wd = container_of((void*)work_elem,struct work_data, work);
	int minor = wd->minor;
	
	object_state *obj = objects + minor;

	timeout = obj->timeout;

	//Lenght of the fragmented data
	len = wd->len;

	//Critical section
	mutex_lock(&(obj->mux_lock[0]));

	//Update valid and pending blocks
	obj->valid[0] += len;
	obj->pending -= len;
	enqueue(obj, 0, wd->data);

	mutex_unlock(&(obj->mux_lock[0]));
	wake_up(&(obj->wq_r));

	kfree(wd->data);
	kfree(work_elem);
    return;
}

int space_occupied(object_state *obj, int prt) {
	if(prt) return obj->valid[prt];
	else return obj->valid[prt] + obj->pending;

	return 0;
}

// Function that checks if there is enough space to write
int can_write(object_state *obj, int prt, int len) {
	mutex_lock(&(obj->mux_lock[prt]));
	
	if(space_occupied(obj, prt) + len <= max_bytes) return 1;

	mutex_unlock(&(obj->mux_lock[prt]));
	return 0;
}

// Function that frees the queue
void free_queue(struct element *head) {
	struct element *curr = head;
	struct element *tmp;

	while(curr != NULL) {
		tmp = curr->next;
		kfree(curr->data);
		kfree(curr);

		curr = tmp;
	}
}


static ssize_t hlm_write(struct file *filp, const char *buff, size_t len, loff_t *off) {
	int ret;
	int prt;
	int timeout;
	int block;
	int min;
	int to_write;
	struct work_data * data;
	struct fragmented_data * frag_data;
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

	if(len > max_bytes) {
		return -ENOSPC;
	}
	
	to_write = len;

	//Fragment data and store in the fragmented_data struct
	frag_data = kmalloc(sizeof(struct fragmented_data), GFP_KERNEL);
	if(frag_data == NULL) {
		printk("%s: problem when allocating memory\n", MODNAME);
		return -ENOMEM;
	}

	frag_data->head = NULL;
	while(to_write > 0) {
		//Find the lenght of the block to write
		min = minimum(to_write, block_max_size);
		node = kmalloc(sizeof(struct element), GFP_KERNEL);
		if(node == NULL) {
			printk("%s: problem when allocating memory\n", MODNAME);
			free_queue(frag_data->head);
			kfree(frag_data);
			return -ENOMEM;
		}

		node->next = NULL;
		node->len = min;
		node->data = kmalloc(min, GFP_KERNEL);
		if(node->data == NULL) {
			printk("%s: problem when allocating memory\n", MODNAME);
			free_queue(frag_data->head);
			kfree(frag_data);
			kfree(node);
			return -ENOMEM;
		}

		ret += copy_from_user(node->data, buff + (len - to_write), min);
		if(ret != 0) {
			node->len -= ret;
			to_write = 0;
		} else {
			to_write -= min;
		}

		if(frag_data->head == NULL) {
			frag_data->head = node;
			frag_data->tail = node;
		} else {
			frag_data->tail->next = node;
			frag_data->tail = node;
		}
	}

	if(block) {
		atomic_inc((atomic_t*)&(obj->asleep[prt]));
		wait_event_interruptible_timeout(obj->wq_w, can_write(obj, prt, len), timeout);
		atomic_dec((atomic_t*)&(obj->asleep[prt]));

		if(space_occupied(obj, prt) + (len - ret) > max_bytes) {
			mutex_unlock(&(obj->mux_lock[prt]));
			free_queue(frag_data->head);
			kfree(frag_data);
			return -ENOSPC;
		}
	} else {
		mutex_lock(&(obj->mux_lock[prt]));

		if(space_occupied(obj, prt) + (len - ret) > max_bytes) {
			mutex_unlock(&(obj->mux_lock[prt]));
			free_queue(frag_data->head);
			kfree(frag_data);
			return -ENOSPC;
		}
	}

	if(prt) {
		enqueue(obj, 1, frag_data);
		obj->valid[prt] += len - ret;
	} else {
		//Prepare work data
		data = kmalloc(sizeof(struct work_data), GFP_KERNEL);
		if(data == NULL) {
			mutex_unlock(&(obj->mux_lock[prt]));
			free_queue(frag_data->head);
			kfree(frag_data);
			return -ENOMEM;
		}

		data->data = frag_data;
		data->minor = minor;
		data->len = len - ret;

		INIT_WORK(&data->work, work_handler);
		obj->pending += len - ret;
		queue_work(wq, &data->work);
	}

	wake_up(&(obj->wq_r));
	mutex_unlock(&(obj->mux_lock[prt]));

	return len - ret;
}

//Check if a reader has enough data to read
int can_read(object_state *obj, int to_read, loff_t *off, int prt) {
	mutex_lock(&(obj->mux_lock[prt]));

	if(to_read + *off <= obj->valid[prt]) {
		return 1;
	}

	mutex_unlock(&(obj->mux_lock[prt]));
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

	//Offset can't be negative because the data is canceled
	if(*off < 0) {
		return -1;
	}

    // Even if there is not enough data, execute a partial read
	if(block) {
		atomic_inc((atomic_t*)&(obj->asleep[prt]));
		wait_event_interruptible_timeout(obj->wq_r, can_read(obj, to_read, off, prt), timeout);
		atomic_dec((atomic_t*)&(obj->asleep[prt]));
	} else {
		mutex_lock(&(obj->mux_lock[prt]));
	}

	//Update reading position with the offset
	obj->r_pos[prt] += *off;

	while(to_read > 0 && obj->head[prt] != NULL) {
		tmp = *head;

		//Available data in the current node
		lenght = tmp->len - obj->r_pos[prt];

		if(to_read >= lenght) {
			//If there isn't enough data in the current node skip to the next one
			x = lenght;

			ret = copy_to_user(buff + (len - to_read), tmp->data + obj->r_pos[prt], x);
			if(ret != 0) {
				//Not all bytes were delivered reset reading position
				obj->r_pos[prt] = x - ret;
				to_read -= x - ret;
				break;
			} else {
				//All bytes were read, block can be freed
				//Set the head to the next block
				*head = tmp->next;
				kfree(tmp->data);
				kfree(tmp);
				obj->r_pos[prt] = 0;
			}
		} else {
			//Partial read of the node
			x = to_read;
			ret = copy_to_user(buff + (len - to_read), tmp->data + obj->r_pos[prt], x);
			if(ret != 0) {
				//Not all bytes were delivered
				obj->r_pos[prt] -= x - ret;
				to_read -= x - ret;
				break;
			}

			//Update reading position with the bytes read
			obj->r_pos[prt] += x;
		}
		
		to_read -= x;
	}

	//Update the valid number of bytes in the flow
	obj->valid[prt] -= len - to_read;

	mutex_unlock(&(obj->mux_lock[prt]));
	wake_up(&(obj->wq_w));

	return len - to_read;
}

static int hlm_open(struct inode *inode, struct file *file) {
	int minor;
	minor = get_minor(file);

	//Check if the minor is enabled
	if(objects[minor].enabled == 0){
		printk("%s: object with %d is disabled\n",MODNAME, minor);
		return -ENODEV;
	}

	printk("%s: hlm dev opened %d\n",MODNAME, minor);
  	return 0;
}

//Used only for debugging purposes
static int hlm_release(struct inode *inode, struct file *file) {
	printk("%s: hlm dev closed\n",MODNAME);
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
		    	printk("%s: invalid enable/disable value %d\n",MODNAME,value);
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
		 		printk("%s: invalid block value %d\n",MODNAME,value);
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
	
	//Setup all devices
	for(i=0;i<MINORS;i++) {
		object_state *obj = objects + i;

		sprintf(name, "%d", i);

		for(int j = 0; j < 2; j++) {
			obj->valid[j] = 0;
			obj->r_pos[j] = 0;
			
			obj->head[j] = NULL;
			obj->tail[j] = NULL;

			mutex_init(&(obj->mux_lock[j]));
		}


		init_waitqueue_head(&(obj->wq_w));
		init_waitqueue_head(&(obj->wq_r));

		obj->pending = 0;
		obj->enabled = 1;
		obj->timeout = 1000;
		obj->block = 0;
		obj->priority = 1;

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
	unregister_chrdev(Major, DEVICE_NAME);
	printk(KERN_INFO "Hlm device unregistered, it was assigned major number %d\n", Major);

	for(int i = 0; i < MINORS; i++) {
		object_state *obj = objects + i;
		flush_workqueue(obj->work_queue);

		// Empty queue
		for(int j = 0; j < 2; j++) {
			free_queue(obj->head[j]);
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