/* The system call implemented in this file:
 *   m_type:	SYS_VM_MAP
 *
 * The parameters for this system call are:
 *    m4_l1:	Process that requests map
 *    m4_l2:	Map (TRUE) or unmap (FALSE)
 *    m4_l3:	Base address
 *    m4_l4:	Size 
 *    m4_l5:	Memory address 
 */
#include "../system.h"

#include <sys/vm.h>

PRIVATE int vm_needs_init= 1;
PRIVATE u32_t vm_cr3;

FORWARD _PROTOTYPE( void vm_init, (void)				);
FORWARD _PROTOTYPE( void phys_put32, (phys_bytes addr, u32_t value)	);
FORWARD _PROTOTYPE( u32_t phys_get32, (phys_bytes addr)			);
FORWARD _PROTOTYPE( void vm_set_cr3, (u32_t value)			);
FORWARD _PROTOTYPE( void set_cr3, (void)				);
FORWARD _PROTOTYPE( void vm_enable_paging, (void)			);
FORWARD _PROTOTYPE( void map_range, (u32_t base, u32_t size,
							u32_t offset)	);

/*===========================================================================*
 *				do_vm_setbuf				     *
 *===========================================================================*/
PUBLIC int do_vm_map(m_ptr)
message *m_ptr;			/* pointer to request message */
{
	int proc_nr, do_map;
	phys_bytes base, size, offset, p_phys;
	struct proc *pp;

	/* do_serial_debug= 1; */

	kprintf("in do_vm_map\n");

	if (vm_needs_init)
	{
		vm_needs_init= 0;
		vm_init();
	}

	proc_nr= m_ptr->m4_l1;
	do_map= m_ptr->m4_l2;
	base= m_ptr->m4_l3;
	size= m_ptr->m4_l4;
	offset= m_ptr->m4_l5;

	pp= proc_addr(proc_nr);
	p_phys= umap_local(pp, D, base, size);
	if (p_phys == 0)
		return EFAULT;
	kprintf("got 0x%x for 0x%x [D].mem_start = 0x%x\n", 
		p_phys, base, pp->p_memmap[D].mem_phys);

	if (do_map)
	{
		kprintf(
		"do_vm_map: mapping 0x%x @ 0x%x to 0x%x @ proc %d\n",
			size, offset, base, proc_nr);
		pp->p_misc_flags |= MF_VM;

		map_range(p_phys, size, offset);
	}
	else
	{
		map_range(p_phys, size, p_phys);
	}
	vm_set_cr3(vm_cr3);

	return OK;
}

/*===========================================================================*
 *				vm_map_default				     *
 *===========================================================================*/
PUBLIC void vm_map_default(pp)
struct proc *pp;
{
	phys_bytes base_clicks, size_clicks;

	if (vm_needs_init)
		panic("vm_map_default: VM not initialized?", NO_NUM);
	pp->p_misc_flags &= ~MF_VM;
	base_clicks= pp->p_memmap[D].mem_phys;
	size_clicks= pp->p_memmap[S].mem_phys+pp->p_memmap[S].mem_len -
		base_clicks;
	map_range(base_clicks << CLICK_SHIFT, size_clicks << CLICK_SHIFT,
		base_clicks << CLICK_SHIFT);
	vm_set_cr3(vm_cr3);
}

PRIVATE void vm_init(void)
{
	int o;
	phys_bytes p, pt_size;
	phys_bytes vm_dir_base, vm_pt_base, phys_mem;
	u32_t entry;
	unsigned pages;

	kprintf("in vm_init\n");

kprintf("%s, %d\n", __FILE__, __LINE__);
	if (!vm_size)
		panic("vm_init: no space for page tables", NO_NUM);

	/* Align page directory */
	o= (vm_base % PAGE_SIZE);
	if (o != 0)
		o= PAGE_SIZE-o;
	vm_dir_base= vm_base+o;

	/* Page tables start after the page directory */
	vm_pt_base= vm_dir_base+PAGE_SIZE;

	pt_size= (vm_base+vm_size)-vm_pt_base;
	pt_size -= (pt_size % PAGE_SIZE);

	/* Compute the number of pages based on vm_mem_high */
	pages= (vm_mem_high-1)/PAGE_SIZE + 1;

	if (pages * I386_VM_PT_ENT_SIZE > pt_size)
		panic("vm_init: page table too small", NO_NUM);

kprintf("%s, %d\n", __FILE__, __LINE__);

	for (p= 0; p*I386_VM_PT_ENT_SIZE < pt_size; p++)
	{
		phys_mem= p*PAGE_SIZE;
		entry= phys_mem | I386_VM_USER | I386_VM_WRITE |
			I386_VM_PRESENT;
		if (phys_mem >= vm_mem_high)
			entry= 0;
		phys_put32(vm_pt_base + p*I386_VM_PT_ENT_SIZE, entry);
	}

	for (p= 0; p < I386_VM_DIR_ENTRIES; p++)
	{
		phys_mem= vm_pt_base + p*PAGE_SIZE;
		entry= phys_mem | I386_VM_USER | I386_VM_WRITE |
			I386_VM_PRESENT;
		if (phys_mem >= vm_pt_base + pt_size)
			entry= 0;
		phys_put32(vm_dir_base + p*I386_VM_PT_ENT_SIZE, entry);
	}

kprintf("%s, %d\n", __FILE__, __LINE__);
	vm_set_cr3(vm_dir_base);
	level0(vm_enable_paging);
}

PRIVATE void phys_put32(addr, value)
phys_bytes addr;
u32_t value;
{
#if 0
kprintf("%s, %d: %d bytes from 0x%x to 0x%x\n", __FILE__, __LINE__,
	sizeof(value), vir2phys((vir_bytes)&value), addr);
#endif

	phys_copy(vir2phys((vir_bytes)&value), addr, sizeof(value));
}

PRIVATE u32_t phys_get32(addr)
phys_bytes addr;
{
	u32_t value;

	phys_copy(addr, vir2phys((vir_bytes)&value), sizeof(value));

	return value;
}

PRIVATE void vm_set_cr3(value)
u32_t value;
{
kprintf("%s, %d\n", __FILE__, __LINE__);
	vm_cr3= value;
kprintf("%s, %d\n", __FILE__, __LINE__);
	level0(set_cr3);
kprintf("%s, %d\n", __FILE__, __LINE__);
}

PRIVATE void set_cr3()
{
	write_cr3(vm_cr3);
}

PRIVATE void vm_enable_paging(void)
{
	u32_t cr0;

	cr0= read_cr0();
	write_cr0(cr0 | I386_CR0_PG);
}

PRIVATE void map_range(base, size, offset)
u32_t base;
u32_t size;
u32_t offset;
{
	u32_t curr_pt, curr_pt_addr, entry;
	int dir_ent, pt_ent;

	if (base % PAGE_SIZE != 0)
		panic("map_range: bad base", base);
	if (size % PAGE_SIZE != 0)
		panic("map_range: bad size", size);
	if (offset % PAGE_SIZE != 0)
		panic("map_range: bad offset", offset);

	curr_pt= -1;
	curr_pt_addr= 0;
	while (size != 0)
	{
		dir_ent= (base >> I386_VM_DIR_ENT_SHIFT);
		pt_ent= (base >> I386_VM_PT_ENT_SHIFT) & I386_VM_PT_ENT_MASK;
		if (dir_ent != curr_pt)
		{
			/* Get address of page table */
			curr_pt= dir_ent;
			curr_pt_addr= phys_get32(vm_cr3 +
				dir_ent * I386_VM_PT_ENT_SIZE);
			curr_pt_addr &= I386_VM_ADDR_MASK;
			kprintf("got address 0x%x for page table 0x%x\n",
				curr_pt_addr, curr_pt);
		}
		entry= offset | I386_VM_USER | I386_VM_WRITE |
			I386_VM_PRESENT;
#if 0
		kprintf(
		"putting 0x%x at dir_ent 0x%x, pt_ent 0x%x (addr 0x%x)\n",
			entry,  dir_ent, pt_ent,
			curr_pt_addr + pt_ent * I386_VM_PT_ENT_SIZE);
#endif
		phys_put32(curr_pt_addr + pt_ent * I386_VM_PT_ENT_SIZE, entry);
		offset += PAGE_SIZE;
		base += PAGE_SIZE;
		size -= PAGE_SIZE;
	}
}
