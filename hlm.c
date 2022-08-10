#define EXPORT_SYMTAB
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/sched.h>	
#include <linux/pid.h>
#include <linux/version.h>
#include <linux/workqueue.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Daniele Ferrarelli");


#define MODNAME "HLM"


static ssize_t print_stream_everywhere(const char *, size_t );

static int hlm_open(struct inode *, struct file *);
static int hlm_release(struct inode *, struct file *);
static ssize_t hlm_write(struct file *, const char *, size_t, loff_t *);

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

struct element {
	struct element *next;
	int data;
};

struct work_data {
    struct work_struct work;
    int data;
};

struct element *head = NULL;
struct element *tail = NULL;

static void work_handler(struct work_struct *work){
    struct work_data * data = (struct work_data *)work;

    struct element *node = kmalloc(sizeof(struct element), GFP_KERNEL);
    node->data = 2;

    if(head == NULL) {
    	head = node;
    	tail = node;
    } else {
    	tail->next = node;
    	tail = node;
    }

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

   struct work_data * data;
   data = kmalloc(sizeof(struct work_data), GFP_KERNEL);
   INIT_WORK(&data->work, work_handler);
   queue_work(wq, &data->work);

   return print_stream_everywhere(buff, len);
}

static ssize_t hlm_read(struct file *filp, char *buff, size_t len, loff_t *off) {
	printk(KERN_INFO "HLM output: %d\n", head->data);
	struct element *tmp = head;
	head = head->next;

	kfree(tmp);
	//ret = copy_to_user(buff,&(the_object->stream_content[*off]),len);
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



int init_module(void)
{

	Major = register_chrdev(0, DEVICE_NAME, &fops);

	if (Major < 0) {
	  printk("Registering noiser device failed\n");
	  return Major;
	}

	printk(KERN_INFO "Hlm device registered, it is assigned major number %d\n", Major);

	wq = create_singlethread_workqueue("hlm_wq");
	if(wq == 0) {
		printk(KERN_ERR "Work queue creation failed\n");
	}

	return 0;
}

void cleanup_module(void)
{

	unregister_chrdev(Major, DEVICE_NAME);
	printk(KERN_INFO "Hlm device unregistered, it was assigned major number %d\n", Major);

	flush_workqueue(wq);
    destroy_workqueue(wq);
    printk(KERN_INFO "Work queue destroied\n");
}

