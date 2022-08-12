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

unsigned long the_syscall_table = 0x0;
module_param(the_syscall_table, ulong, 0660);

unsigned long the_ni_syscall;

unsigned long new_sys_call_array[] = {0x0};//please set to sys_put_work at startup
#define HACKED_ENTRIES (int)(sizeof(new_sys_call_array)/sizeof(unsigned long))
int restore[HACKED_ENTRIES] = {[0 ... (HACKED_ENTRIES-1)] -1};

static ssize_t print_stream_everywhere(const char *, size_t );

static int hlm_open(struct inode *, struct file *);
static int hlm_release(struct inode *, struct file *);
static ssize_t hlm_write(struct file *, const char *, size_t, loff_t *);
static ssize_t hlm_read(struct file *filp, char *buff, size_t len, loff_t *off);

#define DEVICE_NAME "hlm"  /* Device file name in /dev/ - not mandatory  */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(4, 0, 0)
#define get_major(session)      MAJOR(session->f_inode->i_rdev)
#define get_minor(session)      MINOR(session->f_inode->i_rdev)
#else
#define get_major(session)      MAJOR(session->f_dentry->d_inode->i_rdev)
#define get_minor(session)      MINOR(session->f_dentry->d_inode->i_rdev)
#endif

DEFINE_SPINLOCK(list_spinlock);

static int Major;            /* Major number assigned to broadcast device driver */
struct workqueue_struct *wq;	// Workqueue for async add

struct element {
	struct element *next;
	char data[30];
};

struct work_data {
    struct work_struct work;
    char data[30];
};

struct element *head = NULL;
struct element *tail = NULL;

static void work_handler(struct work_struct *work){
    struct work_data * data = (struct work_data *)work;

    struct element *node = kmalloc(sizeof(struct element), GFP_KERNEL);

    //TODO temp
    memcpy(node->data, container_of((void*)data,struct work_data, work)->data, 30);

    spin_lock(&list_spinlock);
    if(head == NULL) {
    	head = node;
    	tail = node;
    } else {
    	tail->next = node;
    	tail = node;
    }
    spin_unlock(&list_spinlock);

    printk(KERN_INFO "Added element\n");
    kfree(data);
}

static int hlm_open(struct inode *inode, struct file *file) {

	printk("%s: hlm dev closed\n",MODNAME);
	//device opened by a default nop
  	 return 0;
}


static int hlm_release(struct inode *inode, struct file *file) {

	printk("%s: hlm dev closed\n",MODNAME);
	//device closed by default nop
   	return 0;

}


static ssize_t hlm_write(struct file *filp, const char *buff, size_t len, loff_t *off) {
	printk("%s: hlm called with [major,minor] number [%d,%d]\n",MODNAME,get_major(filp),get_minor(filp));

	if(len >= LINE_SIZE) return -1;

	struct work_data * data;
	data = kmalloc(sizeof(struct work_data), GFP_KERNEL);
	//ret = 
	copy_from_user(data->data,buff,len);

	INIT_WORK(&data->work, work_handler);
	queue_work(wq, &data->work);

	return print_stream_everywhere(buff, len);
}

static ssize_t hlm_read(struct file *filp, char *buff, size_t len, loff_t *off) {
	int ret = 0;

	if(head != NULL) {
		spin_lock(&list_spinlock);
		printk(KERN_INFO "HLM output: %s\n", head->data);
		struct element *tmp = head;
		head = head->next;
		spin_unlock(&list_spinlock);

		ret = copy_to_user(buff,tmp->data,30);
		kfree(tmp);
		return len - ret;
	}
	
	return 0;
}

static struct file_operations fops = {
  .owner = THIS_MODULE,
  .write = hlm_write,
  .read = hlm_read,
  .open =  hlm_open,
  .release = hlm_release
};


static ssize_t print_stream_everywhere(const char *stream, size_t size ) {
	printk("%s: print stream function of broadcast dev called\n",MODNAME);

	return size;
}


#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
__SYSCALL_DEFINEx(1, _ioctl, int, request_code){
#else
asmlinkage long sys_ioctl(int request_code){
#endif
        
    printk("%s: work with request code: %d\n",MODNAME,request_code);
        
	return 0;
}



#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,17,0)
long sys_ioctl = (unsigned long) __x64_sys_ioctl;       
#else
#endif


int init_module(void)
{
	int ret;
	int i;

	Major = register_chrdev(0, DEVICE_NAME, &fops);

	if (Major < 0) {
	  printk("Registering hlm device failed\n");
	  return Major;
	}

	printk(KERN_INFO "Hlm device registered, it is assigned major number %d\n", Major);

	printk("%s: received sys_call_table address %px\n",MODNAME,(void*)the_syscall_table);

	new_sys_call_array[0] = (unsigned long)sys_ioctl;

	ret = get_entries(restore,HACKED_ENTRIES,(unsigned long*)the_syscall_table,&the_ni_syscall);


    if (ret != HACKED_ENTRIES){
        printk("%s: could not hack %d entries (just %d)\n",MODNAME,HACKED_ENTRIES,ret);
        return -1;
    }

    unprotect_memory();

    for(i=0;i<HACKED_ENTRIES;i++){
    	((unsigned long *)the_syscall_table)[restore[i]] = (unsigned long)new_sys_call_array[i];
    }

    protect_memory();

    printk("%s: all new system-calls correctly installed on sys-call table\n",MODNAME);

    printk("%s: hack %d entries (just %d)\n",MODNAME,HACKED_ENTRIES,ret);

	wq = create_singlethread_workqueue("hlm_wq");
	if(wq == 0) {
		printk(KERN_ERR "Work queue creation failed\n");
	}

	return 0;
}

void cleanup_module(void)
{
	int i;

	unregister_chrdev(Major, DEVICE_NAME);
	printk(KERN_INFO "Hlm device unregistered, it was assigned major number %d\n", Major);

	flush_workqueue(wq);

	// Empty queue
	while(head != NULL) {
		struct element *tmp = head;
		head = head->next;
		kfree(tmp);
	}

    destroy_workqueue(wq);
    printk(KERN_INFO "Work queue destroyied\n");

    unprotect_memory();

    for(i=0;i<HACKED_ENTRIES;i++){
            ((unsigned long *)the_syscall_table)[restore[i]] = the_ni_syscall;
    }
    protect_memory();
    printk("%s: sys-call table restored to its original content\n",MODNAME);
}

